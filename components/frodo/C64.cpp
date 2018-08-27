/*
 *  C64.cpp - Put the pieces together
 *
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 */

#include "sysdeps.h"

#include "C64.h"
#include "CPUC64.h"
#include "CPU1541.h"
#include "VIC.h"
#include "SID.h"
#include "CIA.h"
#include "REU.h"
#include "IEC.h"
#include "1541job.h"
#include "Display.h"
#include "Prefs.h"


#include <esp_heap_caps.h>
#include <esp_system.h>

extern "C"
{
#include "../odroid/odroid_input.h"
#include "../odroid/odroid_display.h"
#include "../odroid/odroid_audio.h"
}

#include "../../main/ui.h"



#ifdef FRODO_SC
bool IsFrodoSC = true;
#else
bool IsFrodoSC = false;
#endif


/*
 *  Constructor: Allocate objects and memory
 */

C64::C64()
{
	printf("HEAP:0x%x (%#08x)\n", esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_DMA));

	int i,j;
	uint8 *p;

	// The thread is not yet running
	thread_running = false;
	quit_thyself = false;
	have_a_break = false;

	// System-dependent things
	c64_ctor1();

	// Open display
	TheDisplay = new C64Display(this);

	// Allocate RAM/ROM memory
	RAM = (uint8*)heap_caps_malloc(0x10000, MALLOC_CAP_SPIRAM);// new uint8[0x10000];
	Basic = (uint8*)heap_caps_malloc(0x2000, MALLOC_CAP_SPIRAM);//new uint8[0x2000];
	Kernal = (uint8*)heap_caps_malloc(0x2000, MALLOC_CAP_SPIRAM);//new uint8[0x2000];
	Char = (uint8*)heap_caps_malloc(0x1000, MALLOC_CAP_SPIRAM);//new uint8[0x1000];
	Color = (uint8*)heap_caps_malloc(0x0400, MALLOC_CAP_SPIRAM);//new uint8[0x0400];
	RAM1541 = (uint8*)heap_caps_malloc(0x0800, MALLOC_CAP_SPIRAM);//new uint8[0x0800];
	ROM1541 = (uint8*)heap_caps_malloc(0x4000, MALLOC_CAP_SPIRAM);//new uint8[0x4000];


	// Create the chips
	TheCPU = new MOS6510(this, RAM, Basic, Kernal, Char, Color);

	TheJob1541 = new Job1541(RAM1541);
	TheCPU1541 = new MOS6502_1541(this, TheJob1541, TheDisplay, RAM1541, ROM1541);

	TheVIC = TheCPU->TheVIC = new MOS6569(this, TheDisplay, TheCPU, RAM, Char, Color);
	TheSID = TheCPU->TheSID = new MOS6581(this);
	

	TheCIA1 = TheCPU->TheCIA1 = new MOS6526_1(TheCPU, TheVIC);
	TheCIA2 = TheCPU->TheCIA2 = TheCPU1541->TheCIA2 = new MOS6526_2(TheCPU, TheVIC, TheCPU1541);

	TheIEC = TheCPU->TheIEC = new IEC(TheDisplay);

	TheREU = TheCPU->TheREU = new REU(TheCPU);


	// Initialize RAM with powerup pattern
	for (i=0, p=RAM; i<512; i++) {
		for (j=0; j<64; j++)
			*p++ = 0;
		for (j=0; j<64; j++)
			*p++ = 0xff;
	}

	// Initialize color RAM with random values
	for (i=0, p=Color; i<1024; i++)
		*p++ = rand() & 0x0f;

	// Clear 1541 RAM
	memset(RAM1541, 0, 0x800);

	// Open joystick drivers if required
	open_close_joysticks(false, false, ThePrefs.Joystick1On, ThePrefs.Joystick2On);
	joykey = 0xff;

#ifdef FRODO_SC
	CycleCounter = 0;
#endif

	// System-dependent things
	c64_ctor2();
}


/*
 *  Destructor: Delete all objects
 */

C64::~C64()
{
	open_close_joysticks(ThePrefs.Joystick1On, ThePrefs.Joystick2On, false, false);

	delete TheJob1541;
	delete TheREU;
	delete TheIEC;
	delete TheCIA2;
	delete TheCIA1;
	delete TheSID;
	delete TheVIC;
	delete TheCPU1541;
	delete TheCPU;
	delete TheDisplay;

	delete[] RAM;
	delete[] Basic;
	delete[] Kernal;
	delete[] Char;
	delete[] Color;
	delete[] RAM1541;
	delete[] ROM1541;

	c64_dtor();
}


/*
 *  Reset C64
 */

void C64::Reset(void)
{
	TheCPU->AsyncReset();
	TheCPU1541->AsyncReset();
	TheSID->Reset();
	TheCIA1->Reset();
	TheCIA2->Reset();
	TheIEC->Reset();
}


/*
 *  NMI C64
 */

void C64::NMI(void)
{
	TheCPU->AsyncNMI();
}


/*
 *  The preferences have changed. prefs is a pointer to the new
 *   preferences, ThePrefs still holds the previous ones.
 *   The emulation must be in the paused state!
 */

void C64::NewPrefs(Prefs *prefs)
{
	open_close_joysticks(ThePrefs.Joystick1On, ThePrefs.Joystick2On, prefs->Joystick1On, prefs->Joystick2On);
	PatchKernal(prefs->FastReset, prefs->Emul1541Proc);

	TheDisplay->NewPrefs(prefs);

#ifdef __riscos__
	// Changed order of calls. If 1541 mode hasn't changed the order is insignificant.
	if (prefs->Emul1541Proc) {
		// New prefs have 1541 enabled ==> if old prefs had disabled free drives FIRST
		TheIEC->NewPrefs(prefs);
		TheJob1541->NewPrefs(prefs);
	} else {
		// New prefs has 1541 disabled ==> if old prefs had enabled free job FIRST
		TheJob1541->NewPrefs(prefs);
		TheIEC->NewPrefs(prefs);
	}
#else
	TheIEC->NewPrefs(prefs);
	TheJob1541->NewPrefs(prefs);
#endif

	TheREU->NewPrefs(prefs);
	TheSID->NewPrefs(prefs);

	// Reset 1541 processor if turned on
	if (!ThePrefs.Emul1541Proc && prefs->Emul1541Proc)
		TheCPU1541->AsyncReset();
}


/*
 *  Patch kernal IEC routines
 */

void C64::PatchKernal(bool fast_reset, bool emul_1541_proc)
{
	if (fast_reset) {
		Kernal[0x1d84] = 0xa0;
		Kernal[0x1d85] = 0x00;
	} else {
		Kernal[0x1d84] = orig_kernal_1d84;
		Kernal[0x1d85] = orig_kernal_1d85;
	}

	if (emul_1541_proc) {
		Kernal[0x0d40] = 0x78;
		Kernal[0x0d41] = 0x20;
		Kernal[0x0d23] = 0x78;
		Kernal[0x0d24] = 0x20;
		Kernal[0x0d36] = 0x78;
		Kernal[0x0d37] = 0x20;
		Kernal[0x0e13] = 0x78;
		Kernal[0x0e14] = 0xa9;
		Kernal[0x0def] = 0x78;
		Kernal[0x0df0] = 0x20;
		Kernal[0x0dbe] = 0xad;
		Kernal[0x0dbf] = 0x00;
		Kernal[0x0dcc] = 0x78;
		Kernal[0x0dcd] = 0x20;
		Kernal[0x0e03] = 0x20;
		Kernal[0x0e04] = 0xbe;
	} else {
		Kernal[0x0d40] = 0xf2;	// IECOut
		Kernal[0x0d41] = 0x00;
		Kernal[0x0d23] = 0xf2;	// IECOutATN
		Kernal[0x0d24] = 0x01;
		Kernal[0x0d36] = 0xf2;	// IECOutSec
		Kernal[0x0d37] = 0x02;
		Kernal[0x0e13] = 0xf2;	// IECIn
		Kernal[0x0e14] = 0x03;
		Kernal[0x0def] = 0xf2;	// IECSetATN
		Kernal[0x0df0] = 0x04;
		Kernal[0x0dbe] = 0xf2;	// IECRelATN
		Kernal[0x0dbf] = 0x05;
		Kernal[0x0dcc] = 0xf2;	// IECTurnaround
		Kernal[0x0dcd] = 0x06;
		Kernal[0x0e03] = 0xf2;	// IECRelease
		Kernal[0x0e04] = 0x07;
	}

	// 1541
	ROM1541[0x2ae4] = 0xea;		// Don't check ROM checksum
	ROM1541[0x2ae5] = 0xea;
	ROM1541[0x2ae8] = 0xea;
	ROM1541[0x2ae9] = 0xea;
	ROM1541[0x2c9b] = 0xf2;		// DOS idle loop
	ROM1541[0x2c9c] = 0x00;
	ROM1541[0x3594] = 0x20;		// Write sector
	ROM1541[0x3595] = 0xf2;
	ROM1541[0x3596] = 0xf5;
	ROM1541[0x3597] = 0xf2;
	ROM1541[0x3598] = 0x01;
	ROM1541[0x3b0c] = 0xf2;		// Format track
	ROM1541[0x3b0d] = 0x02;
}


/*
 *  Save RAM contents
 */

void C64::SaveRAM(char *filename)
{
	FILE *f;

	if ((f = fopen(filename, "wb")) == NULL)
		ShowRequester("RAM save failed.", "OK", NULL);
	else {
		fwrite((void*)RAM, 1, 0x10000, f);
		fwrite((void*)Color, 1, 0x400, f);
		if (ThePrefs.Emul1541Proc)
			fwrite((void*)RAM1541, 1, 0x800, f);
		fclose(f);
	}
}


/*
 *  Save CPU state to snapshot
 *
 *  0: Error
 *  1: OK
 *  -1: Instruction not completed
 */

int C64::SaveCPUState(FILE *f)
{
	MOS6510State state;
	TheCPU->GetState(&state);

	if (!state.instruction_complete)
		return -1;

	int i = fwrite(RAM, 0x10000, 1, f);
	i += fwrite(Color, 0x400, 1, f);
	i += fwrite((void*)&state, sizeof(state), 1, f);

	return i == 3;
}


/*
 *  Load CPU state from snapshot
 */

bool C64::LoadCPUState(FILE *f)
{
	MOS6510State state;

	int i = fread(RAM, 0x10000, 1, f);
	i += fread(Color, 0x400, 1, f);
	i += fread((void*)&state, sizeof(state), 1, f);

	if (i == 3) {
		TheCPU->SetState(&state);
		return true;
	} else
		return false;
}


/*
 *  Save 1541 state to snapshot
 *
 *  0: Error
 *  1: OK
 *  -1: Instruction not completed
 */

int C64::Save1541State(FILE *f)
{
	MOS6502State state;
	TheCPU1541->GetState(&state);

	if (!state.idle && !state.instruction_complete)
		return -1;

	int i = fwrite(RAM1541, 0x800, 1, f);
	i += fwrite((void*)&state, sizeof(state), 1, f);

	return i == 2;
}


/*
 *  Load 1541 state from snapshot
 */

bool C64::Load1541State(FILE *f)
{
	MOS6502State state;

	int i = fread(RAM1541, 0x800, 1, f);
	i += fread((void*)&state, sizeof(state), 1, f);

	if (i == 2) {
		TheCPU1541->SetState(&state);
		return true;
	} else
		return false;
}


/*
 *  Save VIC state to snapshot
 */

bool C64::SaveVICState(FILE *f)
{
	MOS6569State state;
	TheVIC->GetState(&state);
	return fwrite((void*)&state, sizeof(state), 1, f) == 1;
}


/*
 *  Load VIC state from snapshot
 */

bool C64::LoadVICState(FILE *f)
{
	MOS6569State state;

	if (fread((void*)&state, sizeof(state), 1, f) == 1) {
		TheVIC->SetState(&state);
		return true;
	} else
		return false;
}


/*
 *  Save SID state to snapshot
 */

bool C64::SaveSIDState(FILE *f)
{
	MOS6581State state;
	TheSID->GetState(&state);
	return fwrite((void*)&state, sizeof(state), 1, f) == 1;
}


/*
 *  Load SID state from snapshot
 */

bool C64::LoadSIDState(FILE *f)
{
	MOS6581State state;

	if (fread((void*)&state, sizeof(state), 1, f) == 1) {
		TheSID->SetState(&state);
		return true;
	} else
		return false;
}


/*
 *  Save CIA states to snapshot
 */

bool C64::SaveCIAState(FILE *f)
{
	MOS6526State state;
	TheCIA1->GetState(&state);

	if (fwrite((void*)&state, sizeof(state), 1, f) == 1) {
		TheCIA2->GetState(&state);
		return fwrite((void*)&state, sizeof(state), 1, f) == 1;
	} else
		return false;
}


/*
 *  Load CIA states from snapshot
 */

bool C64::LoadCIAState(FILE *f)
{
	MOS6526State state;

	if (fread((void*)&state, sizeof(state), 1, f) == 1) {
		TheCIA1->SetState(&state);
		if (fread((void*)&state, sizeof(state), 1, f) == 1) {
			TheCIA2->SetState(&state);
			return true;
		} else
			return false;
	} else
		return false;
}


/*
 *  Save 1541 GCR state to snapshot
 */

bool C64::Save1541JobState(FILE *f)
{
	Job1541State state;
	TheJob1541->GetState(&state);
	return fwrite((void*)&state, sizeof(state), 1, f) == 1;
}


/*
 *  Load 1541 GCR state from snapshot
 */

bool C64::Load1541JobState(FILE *f)
{
	Job1541State state;

	if (fread((void*)&state, sizeof(state), 1, f) == 1) {
		TheJob1541->SetState(&state);
		return true;
	} else
		return false;
}


#define SNAPSHOT_HEADER "FrodoSnapshot"
#define SNAPSHOT_1541 1

#define ADVANCE_CYCLES	\
	TheVIC->EmulateCycle(); \
	TheCIA1->EmulateCycle(); \
	TheCIA2->EmulateCycle(); \
	TheCPU->EmulateCycle(); \
	if (ThePrefs.Emul1541Proc) { \
		TheCPU1541->CountVIATimers(1); \
		if (!TheCPU1541->Idle) \
			TheCPU1541->EmulateCycle(); \
	}


/*
 *  Save snapshot (emulation must be paused and in VBlank)
 *
 *  To be able to use SC snapshots with SL, SC snapshots are made thus that no
 *  partially dealt with instructions are saved. Instead all devices are advanced
 *  cycle by cycle until the current instruction has been finished. The number of
 *  cycles this takes is saved in the snapshot and will be reconstructed if the
 *  snapshot is loaded into FrodoSC again.
 */

void C64::SaveSnapshot(char *filename)
{
	FILE *f;
	uint8 flags;
	uint8 delay;
	int stat;

	if ((f = fopen(filename, "wb")) == NULL) {
		ShowRequester("Unable to open snapshot file", "OK", NULL);
		return;
	}

	fprintf(f, "%s%c", SNAPSHOT_HEADER, 10);
	fputc(0, f);	// Version number 0
	flags = 0;
	if (ThePrefs.Emul1541Proc)
		flags |= SNAPSHOT_1541;
	fputc(flags, f);
	SaveVICState(f);
	SaveSIDState(f);
	SaveCIAState(f);

#ifdef FRODO_SC
	delay = 0;
	do {
		if ((stat = SaveCPUState(f)) == -1) {	// -1 -> Instruction not finished yet
			ADVANCE_CYCLES;	// Advance everything by one cycle
			delay++;
		}
	} while (stat == -1);
	fputc(delay, f);	// Number of cycles the saved CPUC64 lags behind the previous chips
#else
	SaveCPUState(f);
	fputc(0, f);		// No delay
#endif

	if (ThePrefs.Emul1541Proc) {
		fwrite(ThePrefs.DrivePath[0], 256, 1, f);
#ifdef FRODO_SC
		delay = 0;
		do {
			if ((stat = Save1541State(f)) == -1) {
				ADVANCE_CYCLES;
				delay++;
			}
		} while (stat == -1);
		fputc(delay, f);
#else
		Save1541State(f);
		fputc(0, f);	// No delay
#endif
		Save1541JobState(f);
	}
	fclose(f);

#ifdef __riscos__
	TheWIMP->SnapshotSaved(true);
#endif
}


/*
 *  Load snapshot (emulation must be paused and in VBlank)
 */

bool C64::LoadSnapshot(char *filename)
{
	FILE *f;

	if ((f = fopen(filename, "rb")) != NULL) {
		char Header[] = SNAPSHOT_HEADER;
		char *b = Header, c = 0;
		uint8 delay, i;

		// For some reason memcmp()/strcmp() and so forth utterly fail here.
		while (*b > 32) {
			if ((c = fgetc(f)) != *b++) {
				b = NULL;
				break;
			}
		}
		if (b != NULL) {
			uint8 flags;
			bool error = false;
#ifndef FRODO_SC
			long vicptr;	// File offset of VIC data
#endif

			while (c != 10)
				c = fgetc(f);	// Shouldn't be necessary
			if (fgetc(f) != 0) {
				ShowRequester("Unknown snapshot format", "OK", NULL);
				fclose(f);
				return false;
			}
			flags = fgetc(f);
#ifndef FRODO_SC
			vicptr = ftell(f);
#endif

			error |= !LoadVICState(f);
			error |= !LoadSIDState(f);
			error |= !LoadCIAState(f);
			error |= !LoadCPUState(f);

			delay = fgetc(f);	// Number of cycles the 6510 is ahead of the previous chips
#ifdef FRODO_SC
			// Make the other chips "catch up" with the 6510
			for (i=0; i<delay; i++) {
				TheVIC->EmulateCycle();
				TheCIA1->EmulateCycle();
				TheCIA2->EmulateCycle();
			}
#endif
			if ((flags & SNAPSHOT_1541) != 0) {
				Prefs *prefs = new Prefs(ThePrefs);
	
				// First switch on emulation
				error |= (fread(prefs->DrivePath[0], 256, 1, f) != 1);
				prefs->Emul1541Proc = true;
				NewPrefs(prefs);
				ThePrefs = *prefs;
				delete prefs;
	
				// Then read the context
				error |= !Load1541State(f);
	
				delay = fgetc(f);	// Number of cycles the 6502 is ahead of the previous chips
#ifdef FRODO_SC
				// Make the other chips "catch up" with the 6502
				for (i=0; i<delay; i++) {
					TheVIC->EmulateCycle();
					TheCIA1->EmulateCycle();
					TheCIA2->EmulateCycle();
					TheCPU->EmulateCycle();
				}
#endif
				Load1541JobState(f);
#ifdef __riscos__
				TheWIMP->ThePrefsToWindow();
#endif
			} else if (ThePrefs.Emul1541Proc) {	// No emulation in snapshot, but currently active?
				Prefs *prefs = new Prefs(ThePrefs);
				prefs->Emul1541Proc = false;
				NewPrefs(prefs);
				ThePrefs = *prefs;
				delete prefs;
#ifdef __riscos__
				TheWIMP->ThePrefsToWindow();
#endif
			}

#ifndef FRODO_SC
			fseek(f, vicptr, SEEK_SET);
			LoadVICState(f);	// Load VIC data twice in SL (is REALLY necessary sometimes!)
#endif
			fclose(f);
	
			if (error) {
				ShowRequester("Error reading snapshot file", "OK", NULL);
				Reset();
				return false;
			} else
				return true;
		} else {
			fclose(f);
			ShowRequester("Not a Frodo snapshot file", "OK", NULL);
			return false;
		}
	} else {
		ShowRequester("Can't open snapshot file", "OK", NULL);
		return false;
	}
}



/*
 *  C64_x.i - Put the pieces together, X specific stuff
 *
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 *  Unix stuff by Bernd Schmidt/Lutz Vieweg
 */

#include "main.h"


static struct timeval tv_start;

#ifndef HAVE_USLEEP
/*
 *  NAME:
 *      usleep     -- This is the precision timer for Test Set
 *                    Automation. It uses the select(2) system
 *                    call to delay for the desired number of
 *                    micro-seconds. This call returns ZERO
 *                    (which is usually ignored) on successful
 *                    completion, -1 otherwise.
 *
 *  ALGORITHM:
 *      1) We range check the passed in microseconds and log a
 *         warning message if appropriate. We then return without
 *         delay, flagging an error.
 *      2) Load the Seconds and micro-seconds portion of the
 *         interval timer structure.
 *      3) Call select(2) with no file descriptors set, just the
 *         timer, this results in either delaying the proper
 *         ammount of time or being interupted early by a signal.
 *
 *  HISTORY:
 *      Added when the need for a subsecond timer was evident.
 *
 *  AUTHOR:
 *      Michael J. Dyer                   Telephone:   AT&T 414.647.4044
 *      General Electric Medical Systems        GE DialComm  8 *767.4044
 *      P.O. Box 414  Mail Stop 12-27         Sect'y   AT&T 414.647.4584
 *      Milwaukee, Wisconsin  USA 53201                      8 *767.4584
 *      internet:  mike@sherlock.med.ge.com     GEMS WIZARD e-mail: DYER
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/types.h>

int usleep(unsigned long int microSeconds)
{
        unsigned int            Seconds, uSec;
        int                     nfds, readfds, writefds, exceptfds;
        struct  timeval         Timer;

        nfds = readfds = writefds = exceptfds = 0;

        if( (microSeconds == (unsigned long) 0)
                || microSeconds > (unsigned long) 4000000 )
        {
                errno = ERANGE;         /* value out of range */
                perror( "usleep time out of range ( 0 -> 4000000 ) " );
                return -1;
        }

        Seconds = microSeconds / (unsigned long) 1000000;
        uSec    = microSeconds % (unsigned long) 1000000;

        Timer.tv_sec            = Seconds;
        Timer.tv_usec           = uSec;

        if( select( nfds, &readfds, &writefds, &exceptfds, &Timer ) < 0 )
        {
                perror( "usleep (select) failed" );
                return -1;
        }

        return 0;
}
#endif


/*
 *  Constructor, system-dependent things
 */

void C64::c64_ctor1(void)
{
	// Initialize joystick variables
	//joyfd[0] = joyfd[1] = -1;
	joy_minx = joy_miny = 32767;
	joy_maxx = joy_maxy = -32768;

	// we need to create a potential GUI subprocess here, because we don't want
	// it to inherit file-descriptors (such as for the audio-device and alike..)
#if defined(__svgalib__)
	gui = 0;
#else
// 	// try to start up Tk gui.
// 	gui = new CmdPipe("wish", "TkGui.tcl");
// 	if (gui) {
// 		if (gui->fail) {
// 			delete gui; gui = 0;
// 		}
// 	}
// 	// wait until the GUI process responds (if it does...)
// 	if (gui) {
// 		if (5 != gui->ewrite("ping\n",5)) {
// 			delete gui; gui = 0;
// 		} else {
// 			char c;
// 			fd_set set;
// 			FD_ZERO(&set);
// 			FD_SET(gui->get_read_fd(), &set);
//  			struct timeval tv;
// 			tv.tv_usec = 0;
// 			tv.tv_sec = 5;
// // Use the following commented line for HP-UX < 10.20
// //			if (select(FD_SETSIZE, (int *)&set, (int *)NULL, (int *)NULL, &tv) <= 0) {
// 			if (select(FD_SETSIZE, &set, NULL, NULL, &tv) <= 0) {
// 				delete gui; gui = 0;
// 			} else {
// 				if (1 != gui->eread(&c, 1)) {
// 					delete gui; gui = 0;
// 				} else {
// 					if (c != 'o') {
// 						delete gui; gui = 0;
// 					}
// 				}
// 			}
// 		}
// 	}
#endif // __svgalib__
}

void C64::c64_ctor2(void)
{
#ifndef  __svgalib__  
//    if (!gui) {
//    	fprintf(stderr,"Alas, master, no preferences window will be available.\n"
//    	               "If you wish to see one, make sure the 'wish' interpreter\n"
//    	               "(Tk version >= 4.1) is installed in your path.\n"
//    	               "You can still use Frodo, though. Use F10 to quit, \n"
//    	               "F11 to cause an NMI and F12 to reset the C64.\n"
//    	               "You can change the preferences by editing ~/.frodorc\n");
//    }
#endif // SVGAlib
  
	gettimeofday(&tv_start, NULL);
}


/*
 *  Destructor, system-dependent things
 */

void C64::c64_dtor(void)
{
}


/*
 *  Start main emulation thread
 */

void C64::Run(void)
{
	// Reset chips
	TheCPU->Reset();
	TheSID->Reset();
	TheCIA1->Reset();
	TheCIA2->Reset();
	TheCPU1541->Reset();

	// Patch kernal IEC routines
	orig_kernal_1d84 = Kernal[0x1d84];
	orig_kernal_1d85 = Kernal[0x1d85];
	PatchKernal(ThePrefs.FastReset, ThePrefs.Emul1541Proc);

	quit_thyself = false;
	thread_func();
}


/*
 *  Vertical blank: Poll keyboard and joysticks, update window
 */

void C64::VBlank(bool draw_frame)
{
	// Poll keyboard
	TheDisplay->PollKeyboard(TheCIA1->KeyMatrix, TheCIA1->RevMatrix, &joykey);
	if (TheDisplay->quit_requested)
		quit_thyself = true;

	// Poll joysticks
	TheCIA1->Joystick1 = poll_joystick(0);
	TheCIA1->Joystick2 = poll_joystick(1);

	if (ThePrefs.JoystickSwap) {
		uint8 tmp = TheCIA1->Joystick1;
		TheCIA1->Joystick1 = TheCIA1->Joystick2;
		TheCIA1->Joystick2 = tmp;
	}

	// Joystick keyboard emulation
	//if (TheDisplay->NumLock())
	//	TheCIA1->Joystick1 &= joykey;
	//else
		TheCIA1->Joystick2 &= joykey;
       
	// Count TOD clocks
	TheCIA1->CountTOD();
	TheCIA2->CountTOD();

	// Update window if needed
	//if (draw_frame) {
    	TheDisplay->Update();

		// // Calculate time between VBlanks, display speedometer
		// struct timeval tv;
		// gettimeofday(&tv, NULL);
		// if ((tv.tv_usec -= tv_start.tv_usec) < 0) {
		// 	tv.tv_usec += 1000000;
		// 	tv.tv_sec -= 1;
		// }
		// tv.tv_sec -= tv_start.tv_sec;
		// double elapsed_time = (double)tv.tv_sec * 1000000 + tv.tv_usec;
		// speed_index = 20000 / (elapsed_time + 1) * ThePrefs.SkipFrames * 100;

		// // Limit speed to 100% if desired
		// if ((speed_index > 100) && ThePrefs.LimitSpeed) {
		// 	usleep((unsigned long)(ThePrefs.SkipFrames * 20000 - elapsed_time));
		// 	speed_index = 100;
		// }

		// gettimeofday(&tv_start, NULL);

		// TheDisplay->Speedometer((int)speed_index);
	//}
}


/*
 *  Open/close joystick drivers given old and new state of
 *  joystick preferences
 */

void C64::open_close_joysticks(bool oldjoy1, bool oldjoy2, bool newjoy1, bool newjoy2)
{
#ifdef HAVE_LINUX_JOYSTICK_H
	if (oldjoy1 != newjoy1) {
		joy_minx = joy_miny = 32767;	// Reset calibration
		joy_maxx = joy_maxy = -32768;
		if (newjoy1) {
			joyfd[0] = open("/dev/js0", O_RDONLY);
			if (joyfd[0] < 0)
				fprintf(stderr, "Couldn't open joystick 1\n");
		} else {
			close(joyfd[0]);
			joyfd[0] = -1;
		}
	}

	if (oldjoy2 != newjoy2) {
		joy_minx = joy_miny = 32767;	// Reset calibration
		joy_maxx = joy_maxy = -32768;
		if (newjoy2) {
			joyfd[1] = open("/dev/js1", O_RDONLY);
			if (joyfd[1] < 0)
				fprintf(stderr, "Couldn't open joystick 2\n");
		} else {
			close(joyfd[1]);
			joyfd[1] = -1;
		}
	}
#endif
}


/*
 *  Poll joystick port, return CIA mask
 */

uint8 C64::poll_joystick(int port)
{
#ifdef HAVE_LINUX_JOYSTICK_H
	JS_DATA_TYPE js;
	uint8 j = 0xff;

	if (joyfd[port] >= 0) {
		if (read(joyfd[port], &js, JS_RETURN) == JS_RETURN) {
			if (js.x > joy_maxx)
				joy_maxx = js.x;
			if (js.x < joy_minx)
				joy_minx = js.x;
			if (js.y > joy_maxy)
				joy_maxy = js.y;
			if (js.y < joy_miny)
				joy_miny = js.y;

			if (joy_maxx-joy_minx < 100 || joy_maxy-joy_miny < 100)
				return 0xff;

			if (js.x < (joy_minx + (joy_maxx-joy_minx)/3))
				j &= 0xfb;							// Left
			else if (js.x > (joy_minx + 2*(joy_maxx-joy_minx)/3))
				j &= 0xf7;							// Right

			if (js.y < (joy_miny + (joy_maxy-joy_miny)/3))
				j &= 0xfe;							// Up
			else if (js.y > (joy_miny + 2*(joy_maxy-joy_miny)/3))
				j &= 0xfd;							// Down

			if (js.buttons & 1)
				j &= 0xef;							// Button
		}
	}
	return j;
#else
	return 0xff;
#endif
}


/*
 * The emulation's main loop
 */

void C64::thread_func(void)
{
	int linecnt = 0;

#ifdef FRODO_SC
	while (!quit_thyself) {

		// The order of calls is important here
		if (TheVIC->EmulateCycle())
			TheSID->EmulateLine();
		TheCIA1->CheckIRQs();
		TheCIA2->CheckIRQs();
		TheCIA1->EmulateCycle();
		TheCIA2->EmulateCycle();
		TheCPU->EmulateCycle();

		if (ThePrefs.Emul1541Proc) {
			TheCPU1541->CountVIATimers(1);
			if (!TheCPU1541->Idle)
				TheCPU1541->EmulateCycle();
		}
		CycleCounter++;
#else
	while (!quit_thyself) {

		// The order of calls is important here
		int cycles = TheVIC->EmulateLine();
		TheSID->EmulateLine();
#if !PRECISE_CIA_CYCLES
		TheCIA1->EmulateLine(ThePrefs.CIACycles);
		TheCIA2->EmulateLine(ThePrefs.CIACycles);
#endif

		if (ThePrefs.Emul1541Proc) {
			int cycles_1541 = ThePrefs.FloppyCycles;
			TheCPU1541->CountVIATimers(cycles_1541);

			if (!TheCPU1541->Idle) {
				// 1541 processor active, alternately execute
				//  6502 and 6510 instructions until both have
				//  used up their cycles
				while (cycles >= 0 || cycles_1541 >= 0)
					if (cycles > cycles_1541)
						cycles -= TheCPU->EmulateLine(1);
					else
						cycles_1541 -= TheCPU1541->EmulateLine(1);
			} else
				TheCPU->EmulateLine(cycles);
		} else
			// 1541 processor disabled, only emulate 6510
			TheCPU->EmulateLine(cycles);
#endif
		linecnt++;

		//printf("%s: linecnt=%d\n", __func__, linecnt);

#if 0
		if ((linecnt & 0xfff) == 0 && gui) {

			// check for command from GUI process
		// fprintf(stderr,":");
			while (gui->probe()) {
				char c;
				if (gui->eread(&c, 1) != 1) {
					delete gui;
					gui = 0;
					fprintf(stderr,"Oops, GUI process died...\n");
				} else {
               // fprintf(stderr,"%c",c);
					switch (c) {
						case 'q':
							quit_thyself = true;
							break;
						case 'r':
							Reset();
							break;
						case 'p':{
							Prefs *np = Frodo::reload_prefs();
							NewPrefs(np);
							ThePrefs = *np;
							break;
						}
						default:
							break;
					}
				}
			}
		}
#else
		if (linecnt & 0xfff)
		{
			odroid_gamepad_state gamepad;
			odroid_input_gamepad_read(&gamepad);

			if (gamepad.values[ODROID_INPUT_MENU])
			{
				// Wait for LCD to finish
				odroid_display_lock();
				odroid_display_unlock();

				// Silence audio
				odroid_audio_submit_zero();

				// Display menu
				const char* filename = ui_choosefile();
				if (filename)
				{
					Prefs *np = new Prefs();
					*np = ThePrefs;
					
					np->DriveType[0] = DRVTYPE_D64;
    				strcpy(np->DrivePath[0], filename);

					NewPrefs(np);
					ThePrefs = *np;
					
					delete np;
					
					free((void*)filename);
				}
			}
		}
#endif
	}
#if 0
	if (gui) {
		gui->ewrite("quit\n",5);
	}
#endif
}


