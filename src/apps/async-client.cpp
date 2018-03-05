#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <glut.h>
#include <pthread.h>

#include "Debug.h"
#include "Socket.h"
#include "async.h"
#include "Pixel.h"
#include "ImageWriter.h"

using namespace std;

#define WIDTH  500
#define HEIGHT 500
int width = WIDTH, height = HEIGHT;

ImageWriter image_writer("async_client");

float*       pixels = NULL;
int*         frameids = NULL;
volatile int frame = -1;
pthread_t 	 receiver_tid;
int 				 max_f = -1;
int					 fknt[1000];
bool 				 no_mouse = false;

#if 0
int pixel_count[100];
int packet_count[100];
int max_sender = 0;
#endif

Socket *skt;

int rcvd = 0;

void 
clear()
{
	rcvd = 0;
	for (int i = 0; i < width*height*4; i++)
		pixels[i] = 0.0;
}

void
SendResetCamera()
{
  int op = RESET_CAMERA;
	skt->Send((char *)&op, sizeof(op));
}

void
SendStats()
{
  int op = STATS;
	skt->Send((char *)&op, sizeof(op));
}

void
SendSync()
{
  int op = SYNCHRONIZE;
	skt->Send((char *)&op, sizeof(op));
}

void
SendDebug()
{
  int op = DEBUG;
	skt->Send((char *)&op, sizeof(op));
}
	
void
SendRenderOne()
{
  int op = RENDER_ONE;
	skt->Send((char *)&op, sizeof(op));
}
	
void
SendStart(cam_mode mode, int w, int h, string statefile)
{
	int sz = sizeof(struct start) + statefile.length() + 1;
	struct start *buf = (struct start *)new char[sz];

	buf->op = START;
	buf->mode = mode;
	buf->width = w;
	buf->height = h;
	memcpy(&buf->name[0], statefile.c_str(), statefile.length()+1);

	skt->Send((char *)buf, sz);
	delete[] buf;
}

void
SendMouseDown(int k, int s, float x, float y)
{
	struct mouse_down md;
	md.op = MOUSEDOWN;
	md.k = k;
	md.s = s;
	md.x = x;
	md.y = y;
	skt->Send((char *)&md, sizeof(md));
}

void
SendMouseMotion(float x, float y)
{
	struct mouse_motion mm;
	mm.op = MOUSEMOTION;
	mm.x = x;
	mm.y = y;
	skt->Send((char *)&mm, sizeof(mm));
}

void
SendQuit()
{
	int op = QUIT;
	skt->Send((char *)&op, sizeof(op));

	pthread_join(receiver_tid, NULL);
}

void
draw(void)
{
	if (pixels)
	{
		glDrawPixels(width, height, GL_RGBA, GL_FLOAT, pixels);
		glutSwapBuffers();
	}
}

void
keyboard(unsigned char ch, int x, int y)
{
  switch (ch)
	{
		case 0x63: // c reset camera
			SendResetCamera();
			break;
		case 0x73: // s Sync
			SendSync();
			break;
		case 0x54: // T stats
			SendStats();
			break;
		case 0x51: // Q - print rcvd
			std::cerr << rcvd << "\n";
			break;
		case 0x53: // S - save image
			image_writer.Write(width, height, pixels);
			break;
		case 0x52: // R - render a single 
			clear();
			SendRenderOne();
			break;
		case 0x44:
			for (int i = 0; i <= max_f; i++)
				std::cerr << i << ": " << fknt[i] << "\n";
			SendDebug();
			break;
    case 0x1B: 
			SendQuit();
      exit(0);
		case 0x40:
			glutPostRedisplay();
			break;
  }
}

void
menu(int item)
{
  switch (item) {
		case 0x54: // T stats
			SendStats();
			break;
		case 0x52:
			SendRenderOne();
			break;
    case 0x1B: 
			SendQuit();
      exit(0);
  }
}

void
mousefunc(int k, int s, int x, int y)
{
	if (no_mouse) return;
  if (s == GLUT_DOWN)
		SendMouseDown(k, s, -1.0 + 2.0*(float(x)/width), -1.0 + 2.0*(float(y)/height));
}
  
void
motionfunc(int x, int y)
{
	if (no_mouse) return;
	SendMouseMotion(-1.0 + 2.0*(float(x)/width), -1.0 + 2.0*(float(y)/height));
}

void *
receiver_thread(void *)
{
	char *buf; int n;
	static int frame = 0;
  
	while (true)
	{
		skt->Recv(buf, n);

		char *ptr = buf;
		int knt = *(int *)ptr;
		ptr += sizeof(int);
		int frame = *(int *)ptr;
		ptr += sizeof(int);
		int sender = *(int *)ptr;
		ptr += sizeof(int);
		Pixel *p = (Pixel *)ptr;

		if (frame >= max_f)
		{
			if (frame > max_f)
			{
				float *pix = pixels;
				int *ids = frameids;
				for (int i = 0; i < width*height; i++)
					if (*ids++ < (frame-1))
						*pix++ = 0, *pix++ = 0, *pix++ = 0, *pix++ = 0;
					else
						pix += 4;
			}

			rcvd += knt;

 	 		for (int i = max_f + 1; i <= frame;  i++)
				fknt[i] = 0;

			max_f = frame;
			fknt[frame] += knt;

			for (int i = 0; i < knt; i++, p++)
			{
				size_t offset = (p->y*width + p->x);

				float *pix = pixels + (offset<<2);
				if (frameids[offset] < frame)
				{
					pix[0] = 0;
					pix[1] = 0;
					pix[2] = 0;
					pix[3] = 0;
					frameids[offset] = frame;
				}
				*pix++ += p->r;
				*pix++ += p->g;
				*pix++ += p->b;
				*pix++ += 1.0; // p->o;
			}
		}
		else std::cerr << "S";

		free(buf);
	}

	pthread_exit(NULL);
}

void
syntax(char *a)
{
  cerr << "syntax: " << a << " [options] statefile\n";
  cerr << "options:\n";
  cerr << "  -D         run debugger\n";
	cerr << "  -H host    host (localhost)\n";
	cerr << "  -P port		port (5001)\n";
  cerr << "  -A         wait for attachment\n";
  cerr << "  -s w h     image size (512 x 512)\n";
  cerr << "  -F         ignore mouse movement\n";
  exit(1);
}

int
main(int argc, char *argv[])
{
  bool dbg = false, atch = false;
	string host = "localhost";
	string statefile = "";
	int port = 5001;
	cam_mode mode = OBJECT_CENTER;

  for (int i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "-A")) dbg = true, atch = true;
    else if (!strcmp(argv[i], "-D")) dbg = true, atch = false;
    else if (!strcmp(argv[i], "-H")) host = argv[++i];
    else if (!strcmp(argv[i], "-P")) port = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-F")) no_mouse = true;
    else if (!strcmp(argv[i], "-O")) mode = OBJECT_CENTER;
    else if (!strcmp(argv[i], "-E")) mode = EYE_CENTER;
    else if (!strcmp(argv[i], "-s"))
    {
      width  = atoi(argv[++i]);
      height = atoi(argv[++i]);
    }
    else if (statefile == "")
			statefile = argv[i];
		else
      syntax(argv[0]);
  }

	if (statefile == "")
		syntax(argv[0]);

  Debug *d = dbg ? new Debug(argv[0], atch) : NULL;
	skt = new Socket((char *)host.c_str(), port);

	pixels   = (float *)malloc(width*height*4*sizeof(float));
	frameids = (int *)malloc(width*height*sizeof(int));

	memset(frameids, 0, width*height*sizeof(int));
	for (int i = 0; i < 4*width*height; i++)
			pixels[i] = 0.0;

	pthread_t receiver_tid;
	pthread_create(&receiver_tid, NULL, receiver_thread, NULL);

  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
  glutInitWindowSize(width, height);
  glutCreateWindow("render");
  glutIdleFunc(draw);
  glutDisplayFunc(draw);
  glutMotionFunc(motionfunc);
  glutMouseFunc(mousefunc);
  glutKeyboardFunc(keyboard);
  glutCreateMenu(menu);
  glutAddMenuEntry("Quit", QUIT);
  glutAttachMenu(GLUT_RIGHT_BUTTON);
  glClearDepth(1.0);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glMatrixMode(GL_PROJECTION);
  glOrtho(-1, 1, -1, 1, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glRasterPos2i(-1, -1);
  frame = 1;

	SendStart(mode, width, height, statefile);

  glutMainLoop();

  return 0;
}
