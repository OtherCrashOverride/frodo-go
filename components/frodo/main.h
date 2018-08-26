#if 0
/*
 *  main.h - Main program
 *
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 */

#ifndef _MAIN_H
#define _MAIN_H


class C64;

// Global variables
//extern char AppDirPath[1024];	// Path of application directory



/*
 *  X specific stuff
 */

#if 1

class Prefs;

class Frodo {
public:
	Frodo();
	void ArgvReceived(int argc, char **argv);
	void ReadyToRun(void);
	static Prefs *reload_prefs(void);

	C64 *TheC64;

private:
	bool load_rom_files(void);

	static char prefs_path[256];	// Pathname of current preferences file
};

#endif

#endif
#endif