#ifndef KEYS_H
#define KEYS_H

#include <X11/keysym.h>
#include <X11/Xlib.h>

#define mod Mod4Mask

extern Display *dis;

enum actions
{
	EXIT,
	KILL,
	FOCUS,
	MOVE,
	EXEC
};

struct key {
	char *key;
	int modifier;
	int action;
	char *data;
};

struct key keylist[] = {{"Escape", mod , EXIT, NULL},
					  {"r", mod, EXEC, "rofi -show drun"},
					  {"w", mod , KILL, NULL},
					  {"h", mod , FOCUS, "left"},
					  {"l", mod , FOCUS, "right"},
					  {"H", mod | ShiftMask, MOVE, "left"},
					  {"L", mod | ShiftMask, MOVE, "right"}};
	
#endif
