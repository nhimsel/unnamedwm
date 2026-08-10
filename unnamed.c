/*
TODO: implement virtual desktops + EWMH for them
TODO: use a config file
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xos.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define name "unnamed"
#define exec(s) if (fork() == 0) {							\
	char _cmd[512];											\
	snprintf(_cmd, sizeof(_cmd), "%s >/dev/null 2>&1", s);	\
	execl("/bin/sh", "sh", "-c", _cmd, (char *)NULL);		\
	_exit(1);												\
}   // execute command s in bash
#define lengthof(x) (sizeof x / sizeof x[0]) // gives length of an array
#define NumlockMask Mod2Mask // move this to config file eventually

typedef void (*eventhandler)(XEvent *);
typedef struct client
{
	Window w;
	struct client *n;
	struct client *p;
} client;

struct atoms
{
	Atom netactivewindow;
	Atom netclientlist;
	Atom netclosewindow;
	Atom netsupported;
	Atom netsupportingwmcheck;
	Atom netwmname;
	Atom netwmwindowtype;
	Atom netwmwindowtypedock;
	Atom netwmwindowtypenormal;
	Atom networkarea;
	Atom utf8string;
} atoms;

Display *dis = NULL;	//x display
int scr = 0;			// x screen no
int x = 0;				// workable x range
int y = 0;				// workable y range
int offx = 0;			// offset from 0 for x
int offy = 0;			// offset from 0 for y
int maxx = 0;			// display's x range
int maxy = 0;			// display's y range
client *chead = NULL;	// head of doubly-linked list of visible windows
client *ctail = NULL;	// tail of doubly-linked list of visible windows
client *cfoc = NULL;	// currently-focused window
client *umap = NULL;	// head of doubly-linked list of unmapped windows
client *dock = NULL;	// dock/taskbar window; limited to one

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
	return 1;
}

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
	// hook necessary keybinds
	// will be rewritten once config file is implemented
	
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

void wmcheckwindow(void)
{
	// creates an invisible, unmapped window for _NET_SUPPORTING_WM_CHECK

	Window w = XCreateSimpleWindow(dis, DefaultRootWindow(dis),
								   -1, -1, 1, 1, 0, 0, 0);

	XChangeProperty(dis, DefaultRootWindow(dis), atoms.netsupportingwmcheck,
					XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
	// is this one required?
	XChangeProperty(dis, w, atoms.netsupportingwmcheck, XA_WINDOW, 32,
					PropModeReplace, (unsigned char *)&w, 1);
	XChangeProperty(dis, w, atoms.netwmname, atoms.utf8string, 8,
					PropModeReplace, (unsigned char *)name, strlen(name));
}

void rootatoms(void)
{
	// init root atoms for EWMH

	atoms.netsupported = XInternAtom(dis, "_NET_SUPPORTED", False);
	Atom a[] = {
		atoms.netactivewindow = XInternAtom(dis, "_NET_ACTIVE_WINDOW", False),
		atoms.netclientlist = XInternAtom(dis, "_NET_CLIENT_LIST", False),
		atoms.netclosewindow = XInternAtom(dis, "_NET_CLOSE_WINDOW", False),
		atoms.netsupportingwmcheck = XInternAtom(
			dis, "_NET_SUPPORTING_WM_CHECK", False),
		atoms.netwmname = XInternAtom(dis, "_NET_WM_NAME", False),
		atoms.netwmwindowtype = XInternAtom(dis, "_NET_WM_WINDOW_TYPE", False),
		atoms.netwmwindowtypedock = XInternAtom(
			dis, "_NET_WM_WINDOW_TYPE_DOCK", False),
		atoms.netwmwindowtypenormal = XInternAtom(
			dis, "_NET_WM_WINDOW_TYPE_NORMAL", False),
		atoms.networkarea = XInternAtom(
			dis, "_NET_WORKAREA", False),
		atoms.utf8string = XInternAtom(dis, "UTF8_STRING", False)
	};

	wmcheckwindow();
	
	XChangeProperty(dis, XDefaultRootWindow(dis), atoms.netsupported, XA_ATOM,
					32, PropModeReplace, (unsigned char*)a, lengthof(a));
}

void notifyclient(client *c)
{
	XWindowAttributes a;
	XGetWindowAttributes(dis, c->w, &a);
	
	XConfigureEvent v = {0};
	v.type = ConfigureNotify;
	v.display = dis;
	v.event = c->w;
	v.window = c->w;
	v.x = a.x;
	v.y = a.y;
	v.width = a.width;
	v.height = a.height;
	v.border_width = a.border_width;
	v.above = DefaultRootWindow(dis);
	v.override_redirect = False;
	XSendEvent(dis, c->w, False, StructureNotifyMask, (XEvent *)&v);
}

void killclient(client *c)
{
	// sends a message to kill the window of client c
	// does not delete windows itself - we wait for destroynotify for that

	if (c->w == DefaultRootWindow(dis))
	{
		// shouldn't happen
#ifdef DEBUG
		fprintf(stderr, "tried to kill root window\n");
#endif
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

client* getumapclient(Window *w)
{
	client *u = umap;
	while (u)
	{
		if (u->w == *w) return u;
		u = u->n;
	}
	return NULL;
}

void resizeclient(client *c)
{
#ifdef DEBUG
	XWindowAttributes a;
	XGetWindowAttributes(dis, c->w, &a);
	fprintf(stdout, "resize 0x%lx: %dx%d+%d+%d -> %dx%d+%d+%d\n",
			c->w, a.width, a.height, a.x, a.y, x, y, offx, offy);
#endif

	XMoveResizeWindow(dis, c->w, offx, offy, x, y);

	notifyclient(c);
}

void focusclient(client *c)
{
	// resizeclient(c);
	
	XRaiseWindow(dis, c->w);
	XSetInputFocus(dis, c->w, RevertToPointerRoot, CurrentTime);
	cfoc = c;

	XChangeProperty(dis, DefaultRootWindow(dis), atoms.netactivewindow,
					XA_WINDOW, 32, PropModeReplace,
					(unsigned char *)&cfoc->w, 1);
}

void killcurrentclient(void)
{
	if (!cfoc)
	{
#ifdef DEBUG
		fprintf(stderr, "there is no client to kill\n");
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

void updatedockoffset(XWindowAttributes a)
{
	// configures x, y, offx, offy given the dock window's attributes

	if (a.width >= a.height)
	{
		if (a.y == 0) offy = a.height;
		else offy = 0;
		y = maxy - a.height;
		
		offx = 0;
		x = maxx;
	}
	else
	{
		if (a.x == 0) offx = a.width;
		else offx = 0;
		x = maxx - a.width;
		
		offy = 0;
		y = maxy;
	}

	// update _NET_WORKAREA
	long wa[] = { offx, offy, x, y };
	XChangeProperty(dis, DefaultRootWindow(dis), atoms.networkarea, XA_CARDINAL,
					32, PropModeReplace, (unsigned char *)wa, 4);
}

Atom getwindowtype(Window *w)
{
	Atom a;
	int f;
	unsigned long n, b;
	Atom *t = NULL;

	if (XGetWindowProperty(dis, *w, atoms.netwmwindowtype, 0, 16, False,
						   XA_ATOM, &a, &f, &n, &b, (unsigned char **)&t)
		!= Success)
		return atoms.netwmwindowtypenormal;

	if (!t||n == 0)
	{
		XFree(t);
		return atoms.netwmwindowtypenormal;
	}

	a = t[0];
	XFree(t);
	return a;
}

void buildclientlist(void)
{
	int c = 0;
	for (client *i = chead; i; i = i->n) c++;

	Window *w = malloc(sizeof(Window) * c);

	c = 0;
	for (client *j = chead; j; j = j->n) w[c++] = j->w;

	XChangeProperty(dis, DefaultRootWindow(dis), atoms.netclientlist,
					XA_WINDOW, 32, PropModeReplace, (unsigned char *)w, c);

	if (!chead)
		XChangeProperty(dis, DefaultRootWindow(dis), atoms.netactivewindow,
						XA_WINDOW, 32, PropModeReplace, NULL, 0);
	free(w);
}

void netactivewindow(Window *w)
{
	client *c = getclient(w);
	if (c) focusclient(c);
#ifdef DEBUG
	else fprintf(stderr, "tried to focus unmanaged window 0x%lx\n", *w);
#endif
}

void netclosewindow(Window *w)
{
	client *c = getclient(w);
	if (c) killclient(c);
#ifdef DEBUG
	else fprintf(stderr, "tried to kill unmanaged window 0x%lx\n", *w);
#endif
}

void clientmessage(XEvent *e)
{
	Atom m = e->xclient.message_type;
	if (m == atoms.netactivewindow)
		netactivewindow(&e->xclient.window);
	else if (m == atoms.netclosewindow)
		netclosewindow(&e->xclient.window);
#ifdef DEBUG
	else
	{
		fprintf(stdout, "clientmessage %lu not handled\n", m);
		fflush(stdout);
	}
#endif
}

void configurerequest(XEvent *e)
{
	Window w = e->xconfigurerequest.window;
	client *l;
	if (!(l = getclient(&w)))
	{
		// allow unmanaged windows to resize themselves
		
		XConfigureRequestEvent xcr = e->xconfigurerequest;
		XWindowChanges c;
		c.x = xcr.x;
		c.y = xcr.y;
		c.width = xcr.width;
		c.height = xcr.height;
		c.border_width = xcr.border_width;
		c.sibling = xcr.above;
		c.stack_mode = xcr.detail;
		XConfigureWindow(dis, xcr.window, xcr.value_mask, &c);

		if (dock && xcr.window == dock->w)
		{
			XWindowAttributes a;
			XGetWindowAttributes(dis, xcr.window, &a);
			updatedockoffset(a);
		}
	}
	else
	{
		// as per ICCCM, tell the window we ignored its request

		notifyclient(l);
	}
}

void destroynotify(XEvent *e)
{
	if (dock && e->xdestroywindow.window == dock->w)
	{
		x = maxx;
		y = maxy;
		offx = offy = 0;
		dock = NULL;
#ifdef DEBUG
		fprintf(stdout, "dock was destroyed\n");
		fflush(stdout);
#endif
		if (cfoc && cfoc->w)
			resizeclient(cfoc);

		return;
	}
	
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
	else if ((c = getumapclient(&e->xdestroywindow.window)))
	{
		if (c->w == DefaultRootWindow(dis)) return;

		if (c->p) c->p->n = c->n;
		else umap = umap->n;
		
		if (c->n) c->n->p = c->p;
	}
	
	free(c);

	buildclientlist();
}

void keypress(XEvent *e)
{
	// note mod is declared in keyhook. will be moved to config once implemented
	KeySym k = XLookupKeysym(&e->xkey, 0);
	int s = e->xkey.state;
	
	switch (k) {
	case XK_r:
		exec("rofi -show drun");
		break;
	case XK_w:
		killcurrentclient();
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

	client *c;
	Window t;
	XGetTransientForHint(dis, e->xmaprequest.window, &t);
	if (t)
	{
		// window is a dialog

		XMapWindow(dis, e->xmaprequest.window);
		XSetInputFocus(dis, e->xmaprequest.window, RevertToPointerRoot, CurrentTime);
		return;
	}
	else if (getclient(&e->xmaprequest.window))
	{
		// client already mapped
		return;
	}
	else if ((c = getumapclient(&e->xmaprequest.window)))
	{
		// client had been unmapped
		if (c == umap) umap = umap->n;
		else {
			if (c->p) c->p->n = c->n;
			else umap = umap->n;
			
			if (c->n) c->n->p = c->p;
		}

		c->n = NULL;
		c->p = NULL;
	}
	else
	{
		// no client exists
		c = malloc(sizeof(*c));
		c->w = e->xmaprequest.window;
		c->n = NULL;
		c->p = NULL;
	}

	Atom w = getwindowtype(&c->w);
	if (w == atoms.netwmwindowtypedock)
	{
		if(dock)
		{
			// replace current dock
			killclient(dock);
			x = XDisplayWidth(dis, scr);
			y = XDisplayHeight(dis, scr);
		}
		dock = c;

		XWindowAttributes a;
		XGetWindowAttributes(dis, c->w, &a);

		updatedockoffset(a);

		XMapWindow(dis, dock->w);

		if (cfoc && cfoc->w)
			resizeclient(cfoc);
	}
	else
	{
		// insert window into list
		
		// maybe make an option to toggle between the behaviours later
		// the behaviors being insert next and insert last
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
			if (cfoc == ctail)
				ctail = c;
		}

		// set WM_STATE
		Atom s = XInternAtom(dis, "WM_STATE", False);
		unsigned long d[] = { NormalState, None };
		XChangeProperty(dis, c->w, s, s, 32, PropModeReplace,
						(unsigned char *)d, 2);
		
		XMapWindow(dis, c->w);
		XSync(dis, False);
		resizeclient(c);
		focusclient(c);

		buildclientlist();
	}

#ifdef DEBUG
	fprintf(stdout, "spawn window 0x%lx\n", c->w);
	fflush(stdout);
#endif
}

void unmapnotify(XEvent *e)
{
	client *c = getclient(&e->xunmap.window);
	if (c)
	{
		if (c == cfoc)
		{
			// focus new window if current window closed
			if (cfoc->p) focusclient(cfoc->p);
			else if (cfoc->n) focusclient(cfoc->n);
			else cfoc=NULL;
		}
		
		// move unmapped client to umap list
		if (!c->p)
		{
			chead = c->n;
			if (chead) chead->p = NULL;
		}
		else
			c->p->n = c->n;
		
		if (!c->n)
		{
			ctail = c->p;
			if (ctail) ctail->n = NULL;
		}
		else
			c->n->p = c->p;

		c->n = NULL;
		c->p = NULL;
		
		if (!umap) umap=c;
		else
		{
			client *u = umap;
			while (u->n) u=u->n;

			u->n=c;
			c->p=u;
		}
	}

	buildclientlist();
}

static eventhandler handler[LASTEvent] = {
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[DestroyNotify] = destroynotify,
	[KeyPress] = keypress,
	[MapRequest] = maprequest,
	[UnmapNotify] = unmapnotify,
};

int main(int argc, char *argv[])
{
	if (!(dis = XOpenDisplay(0x0)))
		suicide("failed to connect to X\n");

	checkwm();

	XSetErrorHandler(err);
	scr = DefaultScreen(dis);
	maxx = x =  DisplayWidth(dis, scr);
	maxy  = y =  DisplayHeight(dis, scr);
	XEvent e;

	keyhook();

	rootatoms();

	for(;;)
	{
		XNextEvent(dis, &e);
		if (e.type >=0 && e.type < LASTEvent && handler[e.type])
			handler[e.type](&e);
	}
	return 0;
}
