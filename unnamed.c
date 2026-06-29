#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xos.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define max(x, y) ((x) > (y) ? (x) : (y))
#define exec(s) if (fork() == 0) \
	{execl("/bin/sh", "sh", "-c", s, ">/dev/null", 0); _exit(1);}

#define lengthof(x) (sizeof x / sizeof x[0])
#define NumlockMask Mod2Mask

typedef void (*eventhandler)(XEvent *);

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

int otherwmerr(Display *d, XErrorEvent *e)
{
	suicide("there's already a wm running.\n");
	exit(1);
}

#ifdef DEBUG
const char* namexevent(int type)
{
    switch (type) {
    case KeyPress:        return "KeyPress";
    case KeyRelease:      return "KeyRelease";
    case ButtonPress:     return "ButtonPress";
    case ButtonRelease:   return "ButtonRelease";
    case MotionNotify:    return "MotionNotify";
    case EnterNotify:     return "EnterNotify";
    case LeaveNotify:     return "LeaveNotify";
    case FocusIn:         return "FocusIn";
    case FocusOut:        return "FocusOut";
	case KeymapNotify:    return "KeymapNotify";
    case Expose:          return "Expose";
	case GraphicsExpose:  return "GraphicsExpose";
	case NoExpose:        return "NoExpose";
	case VisibilityNotify:return "VisibilityNotify";
	case CreateNotify:    return "CreateNotify";
	case DestroyNotify:   return "DestroyNotify";
    case UnmapNotify:     return "UnmapNotify";
    case MapNotify:       return "MapNotify";
	case MapRequest:      return "MapRequest";
	case ReparentNotify:  return "ReparentNotify";
	case ConfigureNotify: return "ConfigureNotify";
	case ConfigureRequest:return "ConfigureRequest";
	case GravityNotify:   return "GravityNotify";
	case ResizeRequest:   return "ResizeRequest";
	case CirculateNotify: return "CirculateNotify";
	case CirculateRequest:return "CirculateRequest";
	case PropertyNotify:  return "PropertyNotify";
	case SelectionClear:  return "SelectionClear";
	case SelectionRequest:return "SelectionRequest";
	case SelectionNotify: return "SelectionNotify";
	case ColormapNotify:  return "ColormapNotify";
    case ClientMessage:   return "ClientMessage";
	case MappingNotify:   return "MappingNotify";
	case GenericEvent:    return "GenericEvent";
	case LASTEvent:        return "LASTEvent";
    default: {
		static char buf[256];
		snprintf(buf, sizeof(buf), "Unknown: %d", type);
		return buf;	
	}
    }
}
#endif

void checkwm(void)
{
	XSetErrorHandler(otherwmerr);
	// if this fails, there's already a WM running
	XSelectInput(dis, DefaultRootWindow(dis),
				 SubstructureRedirectMask | SubstructureNotifyMask);
	XSync(dis, False);
}

void keyhook(void)
{
	unsigned int nullmod[] = {0, LockMask, NumlockMask, NumlockMask | LockMask};

	#define mod Mod4Mask
	
	for (int i=0; i<lengthof(nullmod); i++)
	{
		// hook mod-escape
		XGrabKey(dis, XKeysymToKeycode(dis, XStringToKeysym("Escape")),
				 mod | nullmod[i], DefaultRootWindow(dis), True,
				 GrabModeAsync, GrabModeAsync);
		// hook mod-r
		XGrabKey(dis, XKeysymToKeycode(dis, XStringToKeysym("r")),
				 mod | nullmod[i], DefaultRootWindow(dis), True,
				 GrabModeAsync, GrabModeAsync);
		// hook mod-w
		XGrabKey(dis, XKeysymToKeycode(dis, XStringToKeysym("w")),
				 mod | nullmod[i], DefaultRootWindow(dis), True,
				 GrabModeAsync, GrabModeAsync);

		// hook mod-mouse1
		XGrabButton(dis, 1, mod | nullmod[i], DefaultRootWindow(dis), True,
					ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
					GrabModeAsync, GrabModeAsync, None, None);
		// hook mod-mouse3
		XGrabButton(dis, 3, mod | nullmod[i], DefaultRootWindow(dis), True,
					ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
					GrabModeAsync, GrabModeAsync, None, None);
	}
}

void killwin(Window* w)
{
	XEvent e ={0};
	e.xclient.type = ClientMessage;
	e.xclient.window = *w;
	e.xclient.message_type = XInternAtom(dis, "WM_PROTOCOLS", False);
	e.xclient.format = 32;
	e.xclient.data.l[0] = XInternAtom(dis, "WM_DELETE_WINDOW", False);
	e.xclient.data.l[1] = CurrentTime;
	
	XSendEvent(dis, *w, False, NoEventMask, &e);
	XFlush(dis);

#ifdef DEBUG
	fprintf(stdout, "kill 0x%lx\n", *w);
	fflush(stdout);
#endif
}

/*
void buttonpress(XEvent *e)
{
	XWindowAttributes a;
	XGetWindowAttributes(dis, e->xbutton.subwindow, &a);
}
*/

void configurerequest(XEvent *e)
{
	XConfigureRequestEvent xcr = e->xconfigurerequest;
	XWindowChanges cc;
	cc.x = xcr.x;
	cc.y = xcr.y;
	cc.width = xcr.width;
	cc.height = xcr.height;
	cc.border_width = xcr.border_width;
	cc.sibling = xcr.above;
	cc.stack_mode = xcr.detail;
	XConfigureWindow(dis, xcr.window, xcr.value_mask, &cc);
}

void enternotify(XEvent *e)
{
// compress enter notify events to mose recent one
	while(XCheckTypedEvent(dis, EnterNotify, e));
	XCrossingEvent v = e->xcrossing;
	if (v.mode != NotifyNormal)
		return;
	if (v.detail == NotifyInferior)
		return;
	
	if (v.window == DefaultRootWindow(dis)) return;
	
	XRaiseWindow(dis, v.window);
	XSetInputFocus(dis, v.window, RevertToPointerRoot, CurrentTime);
#ifdef DEBUG
	fprintf(stdout, "raise 0x%lx\n", v.window);
	fflush(stdout);
#endif
}

void keypress(XEvent *e)
{
	KeySym k = XLookupKeysym(&e->xkey, 0);
	
	switch (k) {
	case XK_r:
		if (e->xkey.state & mod) exec("rofi -show drun");
		break;
	case XK_w:
		if (e->xkey.state & mod) killwin(&e->xkey.subwindow);
	case XK_Escape:
		suicide("exiting!");
		break;
	}
}

void maprequest(XEvent *e)
{
	Window w = e->xmaprequest.window;

	int x = DisplayWidth(dis, scr);
	int y = DisplayHeight(dis, scr);
	XMoveResizeWindow(dis, w, 0, 0, x, y);
	
	XMapWindow(dis, e->xmaprequest.window);
	XSelectInput(dis, e->xmaprequest.window, EnterWindowMask);
	XSetInputFocus(dis, w, RevertToPointerRoot, CurrentTime);
}

static eventhandler handler[LASTEvent] = {
	[ConfigureRequest] = configurerequest,
	[EnterNotify] = enternotify,
	[KeyPress] = keypress,
	[MapRequest] = maprequest,
};

int main(int argc, char *argv[])
{
	if (!(dis = XOpenDisplay(0x0)))
		suicide("failed to connect to X\n");

	checkwm();

	XSetErrorHandler(err);
	scr = DefaultScreen(dis);
	XEvent e;

	Window root, parent, *children;
	unsigned int n;

	XQueryTree(dis, DefaultRootWindow(dis), &root, &parent, &children, &n);
	
	for (unsigned int i=0; i<n; i++) {
		XSelectInput(dis, children[i], EnterWindowMask);
	}

	keyhook();

	for(;;)
	{
		XNextEvent(dis, &e);
		if (e.type >=0 && e.type < LASTEvent && handler[e.type])
			handler[e.type](&e);
#ifdef DEBUG
		else
			fprintf(stderr, "unhandled XEvent. type: %s\n", namexevent(e.type));
#endif
	}
	return 0;
}
