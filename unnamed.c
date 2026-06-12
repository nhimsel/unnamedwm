#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xos.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define max(x, y) ((x) > (y) ? (x) : (y))
#define exec(s) if (fork() == 0) {execl("/bin/sh", "sh", "-c", s, 0); _exit(1);}

Display *dis;
int scr;

void suicide(char *s)
{
	fprintf(stderr, "%s\n", s);
	exit(1);
}

int err(Display *d, XErrorEvent *e)
{
	fprintf(stderr, "x error: %d\n", e->error_code);
	return 1;
}

int main(int argc, char *argv[])
{
	if (!(dis = XOpenDisplay(0x0)))
	{
		fprintf(stderr, "failed to connect to X\n");
		return 1;
	}

	XSetErrorHandler(err);
	scr = DefaultScreen(dis);
	XButtonEvent s;
	XEvent e;
	XWindowAttributes atts;

	// hook alt-escape
	XGrabKey(dis, XKeysymToKeycode(dis, XStringToKeysym("Escape")), Mod1Mask,
			 DefaultRootWindow(dis), True, GrabModeAsync, GrabModeAsync);
	// hook alt-r
	XGrabKey(dis, XKeysymToKeycode(dis, XStringToKeysym("r")), Mod1Mask,
			 DefaultRootWindow(dis), True, GrabModeAsync, GrabModeAsync);

	XGrabButton(dis, 1, Mod1Mask, DefaultRootWindow(dis), True,
				ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
				GrabModeAsync, GrabModeAsync, None, None);
	XGrabButton(dis, 3, Mod1Mask, DefaultRootWindow(dis), True,
				ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
				GrabModeAsync, GrabModeAsync, None, None);

	s.subwindow = None;
	for(;;)
	{
		XNextEvent(dis, &e);
		switch (e.type) {
		case KeyPress: {
			KeySym k = XLookupKeysym(&e.xkey, 0);

			switch (k) {
			case XK_r:
				if (e.xkey.state & Mod1Mask) exec("rofi -show drun");
				break;
			case XK_Escape:
				suicide("you killed me! you faggot!");
				break;
			}
			break;
		case ButtonPress:
			XGetWindowAttributes(dis, e.xbutton.subwindow, &atts);
			s = e.xbutton;
			break;
		}
		case MotionNotify:
			// compress motion events to most recent one
			while(XCheckTypedEvent(dis, MotionNotify, &e));
			int dx = e.xbutton.x_root - s.x_root;
			int dy = e.xbutton.y_root - s.y_root;
			XMoveResizeWindow(dis, s.subwindow,
							  atts.x + (s.button==1 ? dx : 0),
							  atts.y + (s.button==1 ? dy : 0),
							  max(1, atts.width + (s.button==3 ? dx : 0)),
							  max(1, atts.height + (s.button==3 ? dy : 0)));
			break;
		case ButtonRelease:
			s.subwindow = None;
			break;
		}
	}

	return 0;
}
