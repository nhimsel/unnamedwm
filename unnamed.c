/*
TODO: implement compat with ICCCM and EWMH as needed
      at least enough to use rofi for windows and bars like polybar
TODO: use a config file
TODO: handle floating windows
      is this really needed?
TODO: there seems to be a bug when windows are deleted. need to move left/right
      multiple times sometimes, as if there is an invisible window
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xos.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define exec(s) if (fork() == 0) {							\
	char _cmd[512];											\
	snprintf(_cmd, sizeof(_cmd), "%s >/dev/null 2>&1", s);	\
	execl("/bin/sh", "sh", "-c", _cmd, (char *)NULL);		\
	_exit(1);												\
}
#define lengthof(x) (sizeof x / sizeof x[0])
#define NumlockMask Mod2Mask

typedef void (*eventhandler)(XEvent *);
typedef struct client
{
	Window w;
	struct client *n;
	struct client *p;
} client;

Display *dis = NULL;
int scr = 0;
client *chead = NULL;
client *ctail = NULL;
client *cfoc = NULL;

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

#if 0
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
	#define key(x) XKeysymToKeycode(dis, XStringToKeysym(x))

	struct key {
		KeyCode key;
		int shiftmod;
	};

	struct key keys[] = {{key("Escape"), 0}, {key("r"), 0}, {key("w"), 0},
				  {key("h"), 0}, {key("l"), 0},{key("H"), 1}, {key("L"), 1}};
	
	for (int i=0; i<lengthof(nullmod); i++)
	{
		for (int j=0; j<lengthof(keys); j++)
		{
			if (keys[j].shiftmod)
				XGrabKey(dis, keys[j].key, mod | nullmod[i] | ShiftMask,
						 DefaultRootWindow(dis), True,
						 GrabModeAsync, GrabModeAsync);
			else
				XGrabKey(dis, keys[j].key, mod | nullmod[i],
						 DefaultRootWindow(dis), True,
						 GrabModeAsync, GrabModeAsync);
		}
	}
}

void killclient(client *c)
{
	if (c->w == DefaultRootWindow(dis))
	{
		// shouldn't happen, but i suck at coding so it might
		fprintf(stderr, "tried to kill root window\n");
		return;
	}

	if (chead == NULL)
	{
#ifdef DEBUG
		fprintf(stdout, "no clients left to kill\n");
		fflush(stdout);
#endif
		return;
	}

	Window w = c->w;
	XEvent e ={0};
	e.xclient.type = ClientMessage;
	e.xclient.window = w;
	e.xclient.message_type = XInternAtom(dis, "WM_PROTOCOLS", False);
	e.xclient.format = 32;
	e.xclient.data.l[0] = XInternAtom(dis, "WM_DELETE_WINDOW", False);
	e.xclient.data.l[1] = CurrentTime;
	
	XSendEvent(dis, w, False, NoEventMask, &e);
	XFlush(dis);

#ifdef DEBUG
	fprintf(stdout, "kill 0x%lx\n", w);
	fflush(stdout);
#endif
}

client* getclient(Window *w)
{
	client *c = chead;
	while (c)
	{
		if (c->w == *w) return c;
		c= c->n;
	}
	return NULL;
}

void focusclient(client *c)
{
	XRaiseWindow(dis, c->w);
	XSetInputFocus(dis, c->w, RevertToPointerRoot, CurrentTime);
	cfoc = c;
}

void killcurrentclient(void)
{
	if (!cfoc)
	{
#ifdef DEBUG
		fprintf(stdout, "there is no client to kill\n");
		fflush(stdout);
#endif
		return;
	}

	client *t;
	if (cfoc->p) t = cfoc->p;
	else if (cfoc->n) t = cfoc->n;
	else t = NULL;

	killclient(cfoc);

	if (t) focusclient(t);
	else cfoc = NULL;
}

void focusprev(void)
{
	if (!cfoc) return;
	if (cfoc->p) focusclient(cfoc->p);
}

void focusnext(void)
{
	if (!cfoc) return;
	if (cfoc->n) focusclient(cfoc->n);
}

void moveprev(void)
{
	if (!cfoc) return;
	if (cfoc->p)
	{
#ifdef DEBUG
		fprintf(stdout, "moveprev 0x%lx\n", cfoc->w);
		fflush(stdout);
#endif
		cfoc->p->n = cfoc->n;
		if (cfoc->p->p) cfoc->p->p->n = cfoc;
		else chead = cfoc;
		if (cfoc->n) cfoc->n->p = cfoc->p;
		else ctail = cfoc->p;
		cfoc->n = cfoc->p;
		cfoc->p = cfoc->n->p;
		cfoc->n->p = cfoc;
	}
}

void movenext(void)
{
	if (!cfoc) return;
	if (cfoc->n)
	{
#ifdef DEBUG
		fprintf(stdout, "movenext 0x%lx\n", cfoc->w);
		fflush(stdout);
#endif
		cfoc->n->p = cfoc->p;
		if (cfoc->n->n) cfoc->n->n->p = cfoc;
		else ctail = cfoc;
		if (cfoc->p) cfoc->p->n = cfoc->n;
		else chead = cfoc->n;
		cfoc->p = cfoc->n;
		cfoc->n = cfoc->p->n;
		cfoc->p->n = cfoc;
	}
}

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

void destroynotify(XEvent *e)
{
	client* c = getclient(&e->xdestroywindow.window);


	if (c != NULL)
	{
		if (c->w == DefaultRootWindow(dis)) return;
		
		if (c->p) c->p->n = c->n;
		else chead = c->n;
		
		if (c->n) c->n->p = c->p;
		else ctail = c->p;

		if (c == cfoc)
		{
			if (c->p) cfoc = c->p;
			else cfoc = c->n;
		}
	}
#if 0
	else
	{
		fprintf(stderr, "client for window 0x%lx is NULL\n",
				e->xdestroywindow.window);
	}
#endif
	
	free(c);
}

void keypress(XEvent *e)
{
	// note mod is declared in keyhook. will be moved to config once implemented
	KeySym k = XLookupKeysym(&e->xkey, 0);
	int s = e->xkey.state;
	
	switch (k) {
	case XK_r:
		if (e->xkey.state & mod) exec("rofi -show drun");
		break;
	case XK_w:
		if (e->xkey.state & mod) killcurrentclient();
		break;
	case XK_h:
		if (s & ShiftMask) moveprev();
		else focusprev();
		break;
	case XK_l:
		if (s & ShiftMask) movenext();
		else focusnext();
		break;
	case XK_Escape:
		suicide("exiting!");
		break;
	}
}

void maprequest(XEvent *e)
{
	if (e->xmaprequest.parent != DefaultRootWindow(dis))
		return;

	if (getclient(&e->xmaprequest.window))
	{
#ifdef DEBUG
		fprintf(stderr, "client 0x%lx is already mapped\n",
				e->xmaprequest.window);
#endif
		return;
	}
	
	client *c = malloc(sizeof(*c));
	c->w = e->xmaprequest.window;
	c->n = NULL;
	c->p = NULL;

	// maybe make an option to toggle between the behaviours later
	if (!chead)
	{
		chead = c;
		ctail = c;
	}
	else
	{
		/*
		ctail->n = c;
		c->p = ctail;
		ctail = c;
		*/
		c->n = cfoc->n;
		c->p = cfoc;
		if (cfoc->n) cfoc->n->p = c;
		cfoc->n = c;
	}

	int x = DisplayWidth(dis, scr);
	int y = DisplayHeight(dis, scr);
	XMoveResizeWindow(dis, c->w, 0, 0, x, y);
	
	XMapWindow(dis, c->w);
	XSelectInput(dis, c->w, EnterWindowMask);
	focusclient(c);

#ifdef DEBUG
	fprintf(stdout, "spawn window 0x%lx\n", c->w);
	fflush(stdout);
#endif
}

static eventhandler handler[LASTEvent] = {
	[ConfigureRequest] = configurerequest,
	[DestroyNotify] = destroynotify,
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

	keyhook();

	for(;;)
	{
		XNextEvent(dis, &e);
		if (e.type >=0 && e.type < LASTEvent && handler[e.type])
			handler[e.type](&e);
#if 0
		else
			fprintf(stderr, "unhandled XEvent. type: %s\n", namexevent(e.type));
#endif
	}
	return 0;
}
