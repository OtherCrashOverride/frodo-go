/*
 *  Display.h - C64 graphics display, emulator window handling
 *
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 */

#ifndef _DISPLAY_H
#define _DISPLAY_H

#ifdef __BEOS__
#include <InterfaceKit.h>
#endif

#ifdef AMIGA
#include <graphics/rastport.h>
#endif

#ifdef HAVE_SDL
struct SDL_Surface;
#endif

#ifdef WIN32
#include <ddraw.h>
#endif

#ifdef __riscos__
#include "ROlib.h"
#endif


// Display dimensions
#if defined(SMALL_DISPLAY)
const int DISPLAY_X = 0x168;
const int DISPLAY_Y = 0x110;
#else
const int DISPLAY_X = 0x180;
const int DISPLAY_Y = 0x110;
#endif


class C64Window;
class C64Screen;
class C64;
class Prefs;

// Class for C64 graphics display
class C64Display {
public:
	C64Display(C64 *the_c64);
	~C64Display();

	void Update(void);
	void UpdateLEDs(int l0, int l1, int l2, int l3);
	void Speedometer(int speed);
	uint8 *BitmapBase(void);
	int BitmapXMod(void);
#ifdef __riscos__
	void PollKeyboard(uint8 *key_matrix, uint8 *rev_matrix, uint8 *joystick, uint8 *joystick2);
#else
	void PollKeyboard(uint8 *key_matrix, uint8 *rev_matrix, uint8 *joystick);
#endif
	bool NumLock(void);
	void InitColors(uint8 *colors);
	void NewPrefs(Prefs *prefs);

	C64 *TheC64;



#if 1
	bool quit_requested;
#endif

private:
	int led_state[4];
	int old_led_state[4];



// #ifdef HAVE_SDL
// 	char speedometer_string[16];		// Speedometer text
// 	void draw_string(SDL_Surface *s, int x, int y, const char *str, uint8 front_color, uint8 back_color);
// #endif

// #ifdef __unix
// 	void draw_led(int num, int state);	// Draw one LED
// 	static void pulse_handler(...);		// LED error blinking
// #endif

};


// Exported functions
extern long ShowRequester(char *str, char *button1, char *button2 = NULL);


#endif
