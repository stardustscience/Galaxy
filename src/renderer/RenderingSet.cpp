

#include "Application.h"
#include "RenderingSet.h"
#include "RayQManager.h"

namespace pvol
{
WORK_CLASS_TYPE(RenderingSet::SaveImagesMsg);

#ifdef PVOL_SYNCHRONOUS
WORK_CLASS_TYPE(RenderingSet::PropagateStateMsg);
WORK_CLASS_TYPE(RenderingSet::SynchronousCheckMsg);
WORK_CLASS_TYPE(RenderingSet::ResetMsg);
WORK_CLASS_TYPE(RenderingSet::DumpStateMsg);
#endif // PVOL_SYNCHRONOUS

KEYED_OBJECT_TYPE(RenderingSet)

void
RenderingSet::Register()
{
  RegisterClass();

	SaveImagesMsg::Register();

#ifdef PVOL_SYNCHRONOUS
	PropagateStateMsg::Register();
	SynchronousCheckMsg::Register();
	ResetMsg::Register();
	DumpStateMsg::Register();
#endif // PVOL_SYNCHRONOUS
}

RenderingSet::~RenderingSet()
{
#ifdef PVOL_SYNCHRONOUS
	pthread_mutex_destroy(&lck);
	pthread_cond_destroy(&w8);
	pthread_mutex_destroy(&local_lock);
#endif // PVOL_SYNCHRONOUS
}

#ifdef PVOL_SYNCHRONOUS
#ifdef PRODUCE_STATUS_MESSAGES
void
RenderingSet::DumpState()
{
	DumpStateMsg *msg = new DumpStateMsg(this->getkey());
	msg->Broadcast(true, true);
}
#endif // PRODUCE_STATUS_MESSAGES
#endif // PVOL_SYNCHRONOUS
	

void
RenderingSet::initialize()
{
	// state_counter = 0;
	
	current_frame = -1;
	next_frame = 0;

#ifdef PVOL_SYNCHRONOUS

	activeCameraCount = 0;

	pthread_mutex_init(&local_lock, NULL);

	int r = GetTheApplication()->GetRank();
  int s = GetTheApplication()->GetSize();
  
  if (r > 0)
    parent = (r - 1) >> 1;
  else
    parent = -1;
    
  left_id = (r << 1) + 1;
  if (left_id >= s) left_id = -1;
  
  right_id = (r << 1) + 2;
  if (right_id >= s) right_id = -1;

	pthread_mutex_init(&lck, NULL);
	pthread_cond_init(&w8, NULL);

  local_raylist_count  = 0;
  local_inflight_count  = 0;

	local_reset();

#ifdef PRODUCE_STATUS_MESSAGES
	InitializeState();
#endif // PRODUCE_STATUS_MESSAGES

#endif // PVOL_SYNCHRONOUS
}

#ifdef PVOL_SYNCHRONOUS

void
RenderingSet::SetInitialState(int local_ray_count, int left_state, int right_state)
{
	currently_busy = local_ray_count > 0;
	left_busy  = left_state == 1;
	right_busy = right_state == 1;
	last_busy = (local_ray_count > 0) | left_busy | right_busy;
}

void
RenderingSet::Reset()
{
	ResetMsg *msg = new ResetMsg(this);
	msg->Broadcast(true, true);
}

void
RenderingSet::local_reset()
{
	// Count of pixels sent to rendering (everywhere) and pixels received
	// (on owner)

	n_pix_sent			= 0;
	n_pix_received	= 0;

  // Initially there are no ray messages anywhere, so the kids are idle.
  // However, we don't want anything happening until the kids positively
  // assert the are idle.   Also - any kid that doesn't exist is idle.

  left_busy = left_id == -1 ? false : true;
  right_busy = right_id == -1 ? false : true;


  // This process will initially be set to busy, though currently there
	// are no ray lists alive.  This ensures that the first update sent
	// up the tree will be busy->idle

	last_busy				= true; 
	done 						= false;

	for (auto r : renderings)
		r->local_reset();

	first_async_completion_test_done = false;
}

void
RenderingSet::WaitForDone()
{
	Lock();
	while (!done)
		Wait();
	Unlock();

#if defined(EVENT_TRACKING)

	class FinishedRenderingEvent : public Event
	{
	public:
		FinishedRenderingEvent(Key r) : rset(r) {};

	protected:
		void print(ostream& o)
		{
			Event::print(o);
			o << "finished rendering rset " << rset;
		}

	private:
		Key rset;
	};

	GetTheEventTracker()->Add(new FinishedRenderingEvent(getkey()));
#endif
}

bool RenderingSet::Busy()
{
	return !done;
}

void
RenderingSet::InitializeState()
{
	left_busy  = (left_id != -1);
	right_busy = (right_id != -1);
	last_busy  = true;
	activeCameraCount = 0;
}

class CheckStateActionEvent : public Event
{
public:
  enum CheckStateActionEventFlags
  {
    INIT_SYNC_CHECK,
    SYNC_CHECK_BUSY,
    SYNC_CHECK_IDLE,
    RECEIVED_IDLE_LEFT,
    RECEIVED_IDLE_RIGHT,
    RECEIVED_BUSY_LEFT,
    RECEIVED_BUSY_RIGHT,
    SENT_IDLE_UP,
    SENT_BUSY_UP
  };
    
public:
  CheckStateActionEvent(CheckStateActionEventFlags f) : flag(f) {}

protected:
  void print(ostream& o)
  {
    Event::print(o);
    o  << ((flag == INIT_SYNC_CHECK) ? "starting sync check" :
          (flag == SYNC_CHECK_BUSY) ? "sync check result is BUSY" :
          (flag == SYNC_CHECK_IDLE) ? "sync check result is IDLE" :
          (flag == RECEIVED_IDLE_LEFT) ? "received IDLE from LEFT" :
          (flag == RECEIVED_BUSY_LEFT) ? "received BUSY from LEFT" :
          (flag == RECEIVED_IDLE_RIGHT) ? "received IDLE from RIGHT" :
          (flag == RECEIVED_BUSY_RIGHT) ? "received BUSY from RIGHT" :
          (flag == SYNC_CHECK_IDLE) ? "sync check result is IDLE" :
          (flag == SENT_IDLE_UP) ? "passed IDLE up" :
          (flag == SENT_BUSY_UP) ? "passed BUSY up" : 
          "unknown");
  }

private:
  CheckStateActionEventFlags flag;
};

class CheckStateEntryEvent : public Event
{
public:

public:
  CheckStateEntryEvent(bool lb, bool cb, int k, bool lft, bool rt) :
      last_busy(lb), currently_busy(cb),  count(k), left_busy(lft), right_busy(rt) {}

protected:
  void print(ostream& o)
  {
    Event::print(o);
	  o << "CheckLocalState entry"
					<< " last_busy " << (last_busy ? "busy" : "idle") 
					<< " currently_busy " << (currently_busy ? "busy" : "idle") 
					<< " count " << count 
					<< " left_busy: " << (left_busy ? "busy" : "idle") 
					<< " right_busy: " << (right_busy ? "busy" : "idle");
  }

private:
  bool last_busy, currently_busy, left_busy, right_busy;
  int count;
};


void
RenderingSet::CheckLocalState()
{
	// Don't do anything unless any children have notified me at
  // least once - initially, everyone is marked busy

  // The subtree rooted here is busy if any there is an active ray list
  // locally or in either subtree or if the camera is actively generating initial rays

  bool currently_busy = (local_raylist_count != 0);
	currently_busy = currently_busy || left_busy;
	currently_busy = currently_busy || right_busy;
	// currently_busy = currently_busy || CameraIsActive();

  // If the busy-state has changed, then we need to do something: send a message
  // up the tree to inform the parent of the state change, or (if this is the root)
  // send out a broadcast to synchronously check that everyone is done.
  //
  
	if (first_async_completion_test_done)
		std::cerr << GetTheApplication()->GetRank() << ": " << currently_busy << " " <<  last_busy << "\n";

  if (currently_busy != last_busy)
  {
		last_busy = currently_busy;

    if (!currently_busy && (GetTheApplication()->GetRank() == 0))
    {
#if defined(EVENT_TRACKING)
      GetTheEventTracker()->Add(new CheckStateActionEvent(CheckStateActionEvent::INIT_SYNC_CHECK));
#endif

			SynchronousCheckMsg *msg = new SynchronousCheckMsg(getkey());
			msg->Broadcast(true, true);
    }
    else if (parent != -1)
    {
#if LOGGING
      APP_LOG(<< "CheckLocalState " << getkey() << " sending " << 
						(currently_busy ? "busy" : "idle") << " to " << parent);
#endif

#if defined(EVENT_TRACKING)
      GetTheEventTracker()->Add(new CheckStateActionEvent(currently_busy ? CheckStateActionEvent::SENT_BUSY_UP : CheckStateActionEvent::SENT_IDLE_UP));
#endif

      PropagateStateMsg msg(this, currently_busy);
      msg.Send(parent);
    }
		else
		{
#if LOGGING
      APP_LOG(<< "CheckLocalState " << getkey() << " on root is BUSY");
#endif
		}
  }
	else 
		last_busy = currently_busy;
}

void
RenderingSet::UpdateChildState(bool busy, int child)
{
  // Receive state info from a child

#if defined(EVENT_TRACKING)
  GetTheEventTracker()->Add(new CheckStateActionEvent((busy && child == left_id) ? CheckStateActionEvent::RECEIVED_BUSY_LEFT :
                                                (!busy && child == left_id) ? CheckStateActionEvent::RECEIVED_IDLE_LEFT :
                                                (busy && child == right_id) ? CheckStateActionEvent::RECEIVED_BUSY_RIGHT :
                                                CheckStateActionEvent::RECEIVED_IDLE_RIGHT));
#endif

  if (child == left_id) left_busy = busy;
  else right_busy = busy;

#if LOGGING
  APP_LOG(<< "CheckLocalState from UpdateChildState");
#endif

  CheckLocalState();
}

void
RenderingSet::IncrementInFlightCount()
{
	pthread_mutex_lock(&local_lock);
  local_inflight_count++;
	pthread_mutex_unlock(&local_lock);
}
	
void
RenderingSet::DecrementInFlightCount()
{
	pthread_mutex_lock(&local_lock);
  local_inflight_count--;
	pthread_mutex_unlock(&local_lock);
}
	
void
RenderingSet::IncrementRayListCount(bool silent)
{
	pthread_mutex_lock(&local_lock);
	// Lock();

  int old = local_raylist_count;
  local_raylist_count++;

	if (local_raylist_count == 1 && first_async_completion_test_done)
		std::cerr << GetTheApplication()->GetRank() << ": lrc bumped from 0\n";

  // std::cerr << "+ " << local_raylist_count << "\n";

#if LOGGING
	APP_LOG(<< "RenderingSet (" << ((long)getkey()) << ") IncrementRayListCount  " << local_raylist_count << " silent " << (silent ? "SILENT" : "NOT SILENT"));
#endif

#if 0
  if (local_raylist_count > 100000)
    APP_PRINT(<< "RenderingSet::IncrementRayListCount error? " << local_raylist_count);
#endif


	if (! silent)
	{
		// If its was 0, then this process has gone
		// from the idle to busy state.  See if we need
		// to tell our parent about it.

		if (old == 0)
		{
#if LOGGING
			APP_LOG(<< "CheckLocalState from IncrementRayListCount");
#endif

			CheckLocalState();
		}
	}


	pthread_mutex_unlock(&local_lock);
  // Unlock();
}

void
RenderingSet::DecrementRayListCount()
{
	pthread_mutex_lock(&local_lock);
  // Lock();

  int old = local_raylist_count;
  local_raylist_count--;

	if (local_raylist_count == 0 && first_async_completion_test_done)
		std::cerr << GetTheApplication()->GetRank() << ": lrc to zero\n";

#if LOGGING
		APP_LOG(<< "RenderingSet (" << ((long)getkey()) << ")  DecrementRayListCount  " << local_raylist_count);


  if (local_raylist_count < 0)
    APP_LOG(<< "RayQManager::local_raylist_count is negative?");
#endif

  // If its was 1, its now 0, and his process has gone
  // from the busy to idle state.  See if we need
  // to tell our parent about it.

  if (old == 1)
  {
#if LOGGING
    APP_LOG(<< "CheckLocalState from DecrementRayListCount");
#endif

    CheckLocalState();
  }

  // Unlock();
	pthread_mutex_unlock(&local_lock);
}

void
RenderingSet::CheckGlobalState()
{
	SynchronousCheckMsg * msg = new SynchronousCheckMsg(getkey());
	msg->Broadcast(true, false);
}

bool
RenderingSet::SynchronousCheckMsg::CollectiveAction(MPI_Comm c, bool isRoot)
{
	Key rsk = *(Key *)contents->get();
	RenderingSetP rs = GetByKey(rsk);

	rs->first_async_completion_test_done = true;

  rs->Lock();

  int global_counts[4];
	int local_counts[] = {rs->get_local_raylist_count(), rs->get_number_of_pixels_sent(), rs->get_number_of_pixels_received(), rs->CameraIsActive() ? 1 : 0};

  MPI_Allreduce(local_counts, global_counts, 4, MPI_INT, MPI_SUM, c);

	// If no raylists exist anywhere, and the number of received pixels matches the number of sent pixels,
	// and there are no camera rays currently being generated, this rendering is done.

  // if (global_counts[0] == 0 && (global_counts[1] == global_counts[2]) && global_counts[3] == 0)
  if (global_counts[0] == 0 && global_counts[3] == 0)
	{
		rs->Finalize();
  }
  else
	{

#ifdef PRODUCE_STATUS_MESSAGES
		rs->_dumpState(c, "completion_test");
#endif

		if (GetTheApplication()->GetRank() == 0)
		{
			if (global_counts[0] != 0 && global_counts[3] != 0)
				 std::cerr << "not done due to ray lists AND cameras\n";
			else if (global_counts[0] != 0)
				 std::cerr << "not done due to ray lists\n";
			else std::cerr << "not done due to cameras\n";
		}

    rs->last_busy = true;
	}

  rs->Unlock();

  return false;
}

void
RenderingSet::DecrementActiveCameraCount(int rayknt)
{
	pthread_mutex_lock(&local_lock);
	activeCameraCount --;
	spawnedRayCount += rayknt;

	if (activeCameraCount == 0)
	{
		// Then we have completed spawning all initial rays for this rendering set frame.
		// If so, we might already be done.

		CheckLocalState();
	}
	pthread_mutex_unlock(&local_lock);
}

void
RenderingSet::IncrementActiveCameraCount()
{
	pthread_mutex_lock(&local_lock);
	activeCameraCount ++;
	pthread_mutex_unlock(&local_lock);
}

bool
RenderingSet::PropagateStateMsg::Action(int sender)
{
  unsigned char *ptr = contents->get();

	Key rsk = *(Key *)ptr;
	RenderingSetP rs = GetByKey(rsk);

	if (! rs || rs->IsDone())
	{
#if LOGGING
		APP_LOG(<< "RenderingSet (" << rsk << ") PropagateStateMsg DROPPED");
#endif
		return false;
	}

	ptr += sizeof(Key);
  bool busy = *(bool *)ptr;

#if LOGGING
  APP_LOG(<< "RenderingSet (" << rsk << ") PropagateStateMsg::Action from " << 
					sender << " is " << (busy ? "BUSY" : "IDLE"));
#endif

  // Its possible that this message was in flight when a 
	// previous check caused an synchronous check that
  // succeeded - in which case we drop this one on the floor

  rs->UpdateChildState(busy, sender);

  return false;
}

#endif // PVOL_SYNCHRONOUS

int
RenderingSet::serialSize()
{
	return KeyedObject::serialSize() + sizeof(int) + renderings.size()*sizeof(Key);
}

unsigned char*
RenderingSet::deserialize(unsigned char *ptr)
{
	renderings.clear();

	int n = *(int *)ptr;
	ptr += sizeof(int);

	for (int i = 0; i < n; i++)
	{
		RenderingP r = Rendering::GetByKey(*(Key *)ptr);
		AddRendering(r);
		ptr += sizeof(Key);
	}

	return ptr;
}

unsigned char*
RenderingSet::serialize(unsigned char *ptr)
{
	*(int *)ptr = renderings.size();
	ptr += sizeof(int);

	for (auto r : renderings)
	{
		*(Key *)ptr = r->getkey();
		ptr += sizeof(Key);
	}

	return ptr;
}

void
RenderingSet::SaveImages(string basename)
{
	SaveImagesMsg *msg = new SaveImagesMsg(this, basename);
	msg->Broadcast(true, true);
}

RenderingSet::SaveImagesMsg::SaveImagesMsg(RenderingSet *r, string basename) 
		: SaveImagesMsg(sizeof(Key) + basename.length() + 1)
{
	unsigned char *p = (unsigned char *)contents->get();
	*(Key *)p = r->getkey();
	p += sizeof(Key);
	memcpy(p, basename.c_str(), basename.length()+1);
}

bool
RenderingSet::SaveImagesMsg::CollectiveAction(MPI_Comm c, bool isRoot)
{
	char *ptr = (char *)contents->get();
	Key key = *(Key *)ptr;
	ptr += sizeof(Key);
	string basename(ptr);

	RenderingSetP rs = GetByKey(key);
	
	for (int i = 0; i < rs->GetNumberOfRenderings(); i++)
		if (rs->GetRendering(i)->IsLocal())
			rs->GetRendering(i)->SaveImage(basename, i);

	return false;
}

#ifdef PVOL_SYNCHRONOUS

RenderingSet::ResetMsg::ResetMsg(RenderingSet *r) : ResetMsg(sizeof(Key))
{
	unsigned char *p = (unsigned char *)contents->get();
#if LOGGING
APP_LOG(<< "RenderingSet::ResetMsg::ResetMsg : " << r->getkey());
#endif
	*(Key *)p = r->getkey();
}

bool
RenderingSet::ResetMsg::CollectiveAction(MPI_Comm c, bool isRoot)
{
	Key key = *(Key *)contents->get();
#if LOGGING
APP_LOG(<< "RenderingSet::ResetMsg::CollectiveActionResetMsg : " << key);
#endif
	RenderingSetP rs = GetByKey(key);
	rs->local_reset();
	return false;
}

#endif // PVOL_SYNCHRONOUS

void
RenderingSet::AddRendering(RenderingP r)
{
	renderings.push_back(r);
}

int  
RenderingSet::GetNumberOfRenderings() 
{
	return renderings.size();
}

RenderingP
RenderingSet::GetRendering(int i)
{
	return renderings[i];
}
void
RenderingSet::Enqueue(RayList *rl, bool silent)
{
#if LOGGING
	APP_LOG(<< "RenderingSet   enqueing " << std::hex << rl);
#endif

	if (rl->GetFrame() >= current_frame)
	{
		current_frame = rl->GetFrame();
	
		RayQManager::GetTheRayQManager()->Enqueue(rl);

#ifdef PVOL_SYNCHRONOUS
		IncrementRayListCount(silent);
#endif // PVOL_SYNCHRONOUS
	}
	else
	{
		// std::cerr << "RenderingSet::Enqueue: dropping ray list from wrong frame\n";
		delete rl;
	}
}
bool
RenderingSet::NeedInitialRays()
{
	next_frame ++;
	if (next_frame >= current_frame)
	{
		current_frame = next_frame;
		return true;
	}
	else
		return false;
}

bool
RenderingSet::KeepRays(RayList *rl)
{
	if (rl->GetFrame() >= current_frame)
	{
		current_frame = rl->GetFrame();
		return true;
	}
	else
		return false;
}

#ifdef PVOL_SYNCHRONOUS

#ifdef PRODUCE_STATUS_MESSAGES

void 
RenderingSet::_initStateTimer()
{
		state_timer_start = GetTheEventTracker()->gettime();
}

void
RenderingSet::_dumpState(MPI_Comm c, const char *base)
{
	MPI_Barrier(c);

  int snddata[5];
  int *rcvdata = (int *)malloc(GetTheApplication()->GetSize() * 5 * sizeof(int));

	snddata[0] = CameraIsActive() ;
  snddata[1] = get_local_raylist_count();
  snddata[2] = get_local_inflight_count();

  RayQManager::GetTheRayQManager()->GetQueuedRayCount(snddata[3], snddata[4]);

	MPI_Gather((const void *)snddata, 5, MPI_INT, (void *)rcvdata, 5, MPI_INT, 0, c);

  if (GetTheApplication()->GetRank() == 0)
	{
		int rlk = 0, ifk = 0;
		for (int i = 0; i < GetTheApplication()->GetSize(); i++)
			rlk += rcvdata[i*5+1], ifk += rcvdata[i*5+2];

		if (rlk == 0 && ifk == 0)
			std::cerr << "no raylists enqueued or alive!\n";

		double t = GetTheEventTracker()->gettime();

		stringstream fname;
		fname << base << "_" << (t - state_timer_start);

		fstream of;
		of.open(fname.str(), ios::out | ios::binary);
		of.write((char *)rcvdata, GetTheApplication()->GetSize() * 5 * sizeof(int));
		of.close();
	}
}
#endif

bool 
RenderingSet::DumpStateMsg::CollectiveAction(MPI_Comm c, bool root)
{
	Key rsk = *(Key *)contents->get();
	RenderingSetP rs = GetByKey(rsk);
#ifdef PRODUCE_STATUS_MESSAGES
	rs->_dumpState(c, "status");
#endif
	return false;
}
#endif

}

