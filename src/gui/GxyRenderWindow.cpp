#include <iostream>

#include <QInputEvent>

static int xx = 0;

#include "GxyRenderWindow.hpp"

#include <QJsonDocument>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QVBoxLayout>

static long
my_time()
{
  timespec s;
  clock_gettime(CLOCK_REALTIME, &s);
  return 1000000000*s.tv_sec + s.tv_nsec;
}

GxyOpenGLWidget::GxyOpenGLWidget(QWidget *parent) : QOpenGLWidget(parent)
{
  std::cerr << "new GxyOpenGLWidget " << ((long)this) << "\n";

  pthread_mutex_init(&lock, NULL);

  kill_threads = false;
  rcvr_tid = NULL;
  ager_tid = NULL;

  pthread_mutex_init(&lock, NULL);
}

GxyOpenGLWidget::~GxyOpenGLWidget()
{
  std::cerr << "GxyOpenGLWidget dtor: " << ((long)this) << "\n";
  if (rcvr_tid)
  {
    kill_threads = true;
    pthread_join(rcvr_tid, NULL);
    rcvr_tid = NULL;
  }

  if (ager_tid)
  {
    pthread_join(ager_tid, NULL);
    ager_tid = NULL;
  }

  if (pixels) free(pixels);
  negative_pixels = NULL;

  if (negative_pixels) free(negative_pixels);
  negative_pixels = NULL;

  if (frameids) free(frameids);
  frameids = NULL;

  if (negative_frameids) free(negative_frameids);
  negative_frameids = NULL;

  if (frame_times) free(frame_times);
  frame_times = NULL;
}

void 
GxyOpenGLWidget::manageThreads(bool state)
{
  if (state)
  {
    if (rcvr_tid != NULL)
      std::cerr << "threads are running at time of connection\n";
    else
    {
      kill_threads = false;
      pthread_create(&rcvr_tid, NULL, pixel_receiver_thread, (void *)this);
      pthread_create(&ager_tid, NULL, pixel_ager_thread, (void *)this);
    }
  }
  else
  {
    if (rcvr_tid == NULL)
      std::cerr << "threads are NOT running at time of disconnection\n";
    else
    {
      kill_threads = true;
      pthread_join(rcvr_tid, NULL);
      pthread_join(ager_tid, NULL);
      rcvr_tid = ager_tid = NULL;
    }
  }
}

void
GxyOpenGLWidget::initializeGL()
{
  initializeOpenGLFunctions();

  glClearDepth(1.0);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glMatrixMode(GL_PROJECTION);
  glOrtho(-1, 1, -1, 1, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glRasterPos2i(-1, -1);
}

void
GxyOpenGLWidget::paintGL()
{
  float r = xx & 0x1 ? 1.0 : 0.5;
  float g = xx & 0x2 ? 1.0 : 0.5;
  float b = xx & 0x4 ? 1.0 : 0.5;
  xx ++;

  glClearColor(r, g, b, 1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  std::cerr << "paint\n";

  // sleep(1);
  // update();
}

void
GxyOpenGLWidget::resizeGL(int w, int h)
{
  if (width != w || height != h)
  {
    width = w;
    height = h;

    resize(w, h);

    if (pixels) free(pixels);
    if (negative_pixels) free(negative_pixels);
    if (frameids) free(frameids);
    if (negative_frameids) free(negative_frameids);
    if (frame_times) free(frame_times);

    pixels             = (float *)malloc(width*height*4*sizeof(float));
    negative_pixels    = (float *)malloc(width*height*4*sizeof(float));
    frameids           = (int *)malloc(width*height*sizeof(int));
    negative_frameids  = (int *)malloc(width*height*sizeof(int));
    frame_times        = (long *)malloc(width*height*sizeof(long));

    memset(frameids, 0, width*height*sizeof(int));
    memset(negative_frameids, 0, width*height*sizeof(int));

    long now = my_time();
    for (int i = 0; i < width*height; i++)
      frame_times[i] = now;

    for (int i = 0; i < 4*width*height; i++)
    {
      pixels[i] = (i & 0x3) == 0x3 ? 1.0 : 0.0;
      negative_pixels[i] = (i & 0x3) == 0x3 ? 1.0 : 0.0;
    }
  }
}

void
GxyOpenGLWidget::addPixels(gxy::Pixel *p, int n, int frame)
{
  std::cerr << "addPixels! " << width << " " << height << "\n";

  pthread_mutex_lock(&lock);

  long now = my_time();

  if (frame >= current_frame)
  {
    //  If frame is strictly greater than current_frame then
    //  updating it will kick the ager to begin aging
    //  any pixels from prior frames.   We want to start the
    //  aging process from the arrival of the first contribution
    //  to the new frame rather than its updated time.
		
    if (frame > current_frame)
    {
      current_frame = frame;

      for (int offset = 0; offset < width*height; offset++)
        frame_times[offset] = now;
    }

    // Only bump current frame IF this is a positive pixel

    for (int i = 0; i < n; i++, p++)
    {
      if (p->x < 0 || p->x >= width || p->y < 0 || p->y >= height)
      {
        std::cerr << "pixel error: pixel is (" << p->x << "," << p->y << ") window is (" << width << "," << height << ")\n";
        exit(1);
      }

      size_t offset = p->y*width + p->x;
      float *pix = pixels + (offset<<2);
      float *npix = negative_pixels + (offset<<2);

      // If its a sample from the current frame, add it in.
      // 
      // If its a sample from a LATER frame then two possibilities:
      // its a negative sample, in which case we DON'T want to update
      // the visible pixel, so we stash it. If its a POSITIVE sample
      // we stuff the visible pixel with the sample and any stashed
      // negative samples.

      if (frameids[offset] == frame)
      {
        *pix++ += p->r;
        *pix++ += p->g;
        *pix++ += p->b;
      }
      else
      {
        if (p->r < 0.0 || p->g < 0.0 || p->b < 0.0)
        {
          // If its a NEGATIVE sample and...

          if (negative_frameids[offset] == frame)
          {
            // from the current frame, we add it to the stash
            *npix++ += p->r;
            *npix++ += p->g;
            *npix++ += p->b;
          }
          else if (negative_frameids[offset] < frame)
          {
            // otherwise its from a LATER frame, so we init the stash so if
            // we receive any POSITIVE samples we can add it in.
            negative_frameids[offset] = frame;
            *npix++ = p->r;
            *npix++ = p->g;
            *npix++ = p->b;
          }
        }
        else
        {
          // its a POSITIVE sample from a NEW frame, so we stuff the visible
          // pixel with the new sample and any negative samples that arrived 
          // first

          frameids[offset] = frame;
          if (current_frame == negative_frameids[offset])
          {
            *pix++ = (*npix + p->r);
					  *npix = 0.0;
					  npix++;
            *pix++ = (*npix + p->g);
					  *npix = 0.0;
					  npix++;
            *pix++ = (*npix + p->b);
					  *npix = 0.0;
					  npix++;
          }
          else
          {
            *pix++ = p->r;
            *pix++ = p->g;
            *pix++ = p->b;
          }
        }
      }
    }
  }

  pthread_mutex_unlock(&lock);
}

void *
GxyOpenGLWidget::pixel_receiver_thread(void *d)
{
  GxyOpenGLWidget *oglWidget = (GxyOpenGLWidget *)d;
  GxyConnectionMgr *connection = getTheGxyConnectionMgr();

  std::cerr << "pixel_receiver_thread running\n";

  while (! oglWidget->kill_threads)
  {
    if (connection->DWait(0.1))
    {
      char *buf; int n;
      connection->DRecv(buf, n);

      char *ptr = buf;
      int knt = *(int *)ptr;
      ptr += sizeof(int);
      int frame = *(int *)ptr;
      ptr += sizeof(int);
      int sndr = *(int *)ptr;
      ptr += sizeof(int);
      gxy::Pixel *p = (gxy::Pixel *)ptr;

      std::cerr << "received " << knt << " pixels\n";
      oglWidget->addPixels(p, knt, frame);

      free(buf);
    }
  }

  std::cerr << "pixel_receiver_thread stopping\n";

  pthread_exit(NULL);
}

void *
GxyOpenGLWidget::pixel_ager_thread(void *d)
{
  GxyOpenGLWidget *oglWidget = (GxyOpenGLWidget *)d;
  
  while (! oglWidget->kill_threads)
  {
    struct timespec rm, tm = {0, 100000000};
    nanosleep(&tm, &rm);
    oglWidget->FrameBufferAger();
  }

  pthread_exit(NULL);
}

void
GxyOpenGLWidget::FrameBufferAger()
{
  if (pixels && frameids && frame_times)
  {
    pthread_mutex_lock(&lock);

    long now = my_time();

    for (int offset = 0; offset < width*height; offset++)
    {
      int fid = frameids[offset];
      if (fid > 0 && fid < current_frame)
      {
        long tm = frame_times[offset];
        float sec = (now - tm) / 1000000000.0;

        float *pix = pixels + (offset*4);
        if (sec > max_age)
        {
          if (sec > (max_age + fadeout))
          {
            frame_times[offset] = now;
            frameids[offset] = current_frame;
            *pix++ = 0.0, *pix++ = 0.0, *pix++ = 0.0, *pix++ = 1.0;
          }
          else
            *pix++ *= 0.9, *pix++ *= 0.9, *pix++ *= 0.9, *pix++ = 1.0;
        }
      }
    }

    pthread_mutex_unlock(&lock);
  }
}


GxyRenderWindow::GxyRenderWindow()
{
  resize(512, 512);
  setFocusPolicy(Qt::StrongFocus);

  QWidget *w = new QWidget(this);
  QVBoxLayout *l = new QVBoxLayout;
  w->setLayout(l);
  
  QScrollArea *sa = new QScrollArea(w);
  l->addWidget(sa);
  
  oglWidget = new GxyOpenGLWidget(sa);
  sa->setWidget(oglWidget);

  std::cerr << "new GxyRenderWindow " << ((long)this) << " " << ((long)oglWidget) << "\n";

  setCentralWidget(w);
}

GxyRenderWindow::~GxyRenderWindow()
{
  std::cerr << "GxyRenderWindow dtor: " << ((long)this) << " " << ((long)oglWidget) << "\n";
}

void
GxyRenderWindow::onCameraChanged(Camera& c)
{
  camera = c;
  gxy::vec2i size = c.getSize();
  std::cerr << "onCameraChanged " << size.x << " " << size.y << "\n";
  oglWidget->resize(size.x, size.y);
}


void
GxyRenderWindow::onLightingChanged(LightingEnvironment& l)
{
  lighting = l;
}

void
GxyRenderWindow::onVisUpdate(std::shared_ptr<GxyVis> v)
{
  if (v)
  {
    Visualization[v->get_origin()] = v;
    for (auto vis : Visualization)
    {
      std::cerr << vis.first << " =======================\n";
      vis.second->print();
    }
  }
}

void
GxyRenderWindow::onVisRemoved(std::string id)
{
  Visualization.erase(id);
}

std::string
GxyRenderWindow::cameraString()
{
  QJsonObject cameraJson;
  cameraJson["Camera"] = camera.save();
  QJsonDocument doc(cameraJson);
  QByteArray bytes = doc.toJson(QJsonDocument::Compact);
  QString s = QLatin1String(bytes);
  return s.toStdString();
}

std::string
GxyRenderWindow::visualizationString()
{
  QJsonObject visualizationJson;

  QJsonObject v;
  v["Lighting"] = lighting.save();

  QJsonArray operatorsJson;
  for (auto vis : Visualization)
  {
    QJsonObject operatorJson;
    vis.second->save(operatorJson);
    operatorsJson.push_back(operatorJson);
  }
  v["operators"] = operatorsJson;

  visualizationJson["Visualization"] = v;

  QJsonDocument doc(visualizationJson);
  QByteArray bytes = doc.toJson(QJsonDocument::Compact);
  QString s = QLatin1String(bytes);
  return s.toStdString();
}

void
GxyRenderWindow::mousePressEvent(QMouseEvent *event) 
{
  grabMouse();
  
  switch(event->button())
  {
    case Qt::LeftButton:   button = 0; break;
    case Qt::MiddleButton: button = 1; break;
    case Qt::RightButton:  button = 2; break;
    default: button = 3;
  }

#if 0
  x0 = x1 = -1.0 + 2.0*(float(event->x())/width);
  y0 = y1 = -1.0 + 2.0*(float(event->y())/height);
#endif
}

void
GxyRenderWindow::mouseMoveEvent(QMouseEvent *event) 
{
  std::cerr << "m " << event->x() << " " << event->y() << "\n";
#if 0
  x1 = -1.0 + 2.0*(float(event->x())/width);
  y1 = -1.0 + 2.0*(float(event->y())/height);

  if (button == 0)
  {
    trackball.spin(x0, y0, x1, y1);

    frame_direction = trackball.rotate_vector(current_direction);
    frame_up = trackball.rotate_vector(current_up);
    frame_center = current_center;
  }
  else if (button == 1)
  {
    frame_direction = current_direction;
    frame_up = current_up;

    gxy::vec3f step = current_direction;
    gxy::scale((y1 - y0), step);
    frame_center = current_center = current_center + step;
  }
  
  paintGL();
#endif
}

void
GxyRenderWindow::mouseReleaseEvent(QMouseEvent *event) 
{
  if (button == 0)
  std::cerr << "up\n";
  button = -1;
  releaseMouse();
}

void
GxyRenderWindow::keyPressEvent(QKeyEvent *event) 
{
  std::string key = event->text().toStdString();
  std::cerr << key << "\n";
}

