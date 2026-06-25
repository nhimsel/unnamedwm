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

#define lengthof(x) (sizeof x / sizeof x[0])
#define NumlockMask Mod2Mask

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
	
	for (int i=0; i<lengthof(nullmod); i++)
	{
		// hook alt-escape
		XGrabKey(dis, XKeysymToKeycode(dis, XStringToKeysym("Escape")),
				 Mod1Mask | nullmod[i], DefaultRootWindow(dis), True,
				 GrabModeAsync, GrabModeAsync);
		// hook alt-r
		XGrabKey(dis, XKeysymToKeycode(dis, XStringToKeysym("r")),
				 Mod1Mask | nullmod[i], DefaultRootWindow(dis), True,
				 GrabModeAsync, GrabModeAsync);

		// hook alt-mouse1
		XGrabButton(dis, 1, Mod1Mask | nullmod[i], DefaultRootWindow(dis), True,
					ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
					GrabModeAsync, GrabModeAsync, None, None);
		// hook alt-mouse3
		XGrabButton(dis, 3, Mod1Mask | nullmod[i], DefaultRootWindow(dis), True,
					ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
					GrabModeAsync, GrabModeAsync, None, None);
	}
}

int main(int argc, char *argv[])
{
	if (!(dis = XOpenDisplay(0x0)))
		suicide("failed to connect to X\n");

	checkwm();

	XSetErrorHandler(err);
	scr = DefaultScreen(dis);
	XButtonEvent s;
	XEvent e;
	XWindowAttributes atts;
	Window root, parent, *children;
	unsigned int n;

	XQueryTree(dis, DefaultRootWindow(dis), &root, &parent, &children, &n);
	
	for (unsigned int i=0; i<n; i++) {
		XSelectInput(dis, children[i], EnterWindowMask);
	}

	keyhook();

	s.subwindow = None;
	for(;;)
	{
		XNextEvent(dis, &e);
		switch (e.type) {
		case ButtonPress:
			XGetWindowAttributes(dis, e.xbutton.subwindow, &atts);
			s = e.xbutton;
			break;
		case ButtonRelease:
			s.subwindow = None;
			break;
		case ConfigureRequest: {
			XConfigureRequestEvent xcr = e.xconfigurerequest;
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
		case EnterNotify: {
			// compress enter notify events to mose recent one
			while(XCheckTypedEvent(dis, EnterNotify, &e));
			XCrossingEvent *v = &e.xcrossing;
			if (v->mode != NotifyNormal)
				break;
			if (v->detail == NotifyInferior)
				break;

			if (v->window == DefaultRootWindow(dis)) break;

			XRaiseWindow(dis, v->window);
			XSetInputFocus(dis, v->window, RevertToPointerRoot, CurrentTime);
#ifdef DEBUG
			fprintf(stdout, "raise 0x%lx\n", v->window);
			fflush(stdout);
#endif
			break;
		}
		case KeyPress: {
			KeySym k = XLookupKeysym(&e.xkey, 0);

			switch (k) {
			case XK_r:
				if (e.xkey.state & Mod1Mask) exec("rofi -show drun");
				break;
			case XK_Escape:
				suicide("exiting!");
				break;
			}
			break;
		}
		case MapRequest:
			XMapWindow(dis, e.xmaprequest.window);
			XSelectInput(dis, e.xmaprequest.window, EnterWindowMask);
			break;
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
		default:
#ifdef DEBUG
			fprintf(stderr, "unhandled XEvent. type: %s\n", namexevent(e.type));
#endif
			break;
		}
	}

	return 0;
}
