#pragma once

#include "sysdeps.h"

#include "SID.h"
#include "Prefs.h"

/**
 **  Renderer for digital SID emulation (SIDTYPE_DIGITAL)
 **/

#if defined(AMIGA) || defined(__riscos__)
const uint32 SAMPLE_FREQ = 22050;	// Sample output frequency in Hz
#else
const uint32 SAMPLE_FREQ = 32000;	// Sample output frequency in Hz
#endif
const uint32 SID_FREQ = 985248;		// SID frequency in Hz
const uint32 CALC_FREQ = 50;			// Frequency at which calc_buffer is called in Hz (should be 50Hz)
const uint32 SID_CYCLES = SID_FREQ/SAMPLE_FREQ;	// # of SID clocks per sample frame
const int SAMPLE_BUF_SIZE = 0x138*2;// Size of buffer for sampled voice (double buffered)

// SID waveforms (some of them :-)
enum {
	WAVE_NONE,
	WAVE_TRI,
	WAVE_SAW,
	WAVE_TRISAW,
	WAVE_RECT,
	WAVE_TRIRECT,
	WAVE_SAWRECT,
	WAVE_TRISAWRECT,
	WAVE_NOISE
};

// EG states
enum {
	EG_IDLE,
	EG_ATTACK,
	EG_DECAY,
	EG_RELEASE
};

// Filter types
enum {
	FILT_NONE,
	FILT_LP,
	FILT_BP,
	FILT_LPBP,
	FILT_HP,
	FILT_NOTCH,
	FILT_HPBP,
	FILT_ALL
};

// Structure for one voice
struct DRVoice {
	int wave;		// Selected waveform
	int eg_state;	// Current state of EG
	DRVoice *mod_by;	// Voice that modulates this one
	DRVoice *mod_to;	// Voice that is modulated by this one

	uint32 count;	// Counter for waveform generator, 8.16 fixed
	uint32 add;		// Added to counter in every frame

	uint16 freq;		// SID frequency value
	uint16 pw;		// SID pulse-width value

	uint32 a_add;	// EG parameters
	uint32 d_sub;
	uint32 s_level;
	uint32 r_sub;
	uint32 eg_level;	// Current EG level, 8.16 fixed

	uint32 noise;	// Last noise generator output value

	bool gate;		// EG gate bit
	bool ring;		// Ring modulation bit
	bool test;		// Test bit
	bool filter;	// Flag: Voice filtered

					// The following bit is set for the modulating
					// voice, not for the modulated one (as the SID bits)
	bool sync;		// Sync modulation bit
};

// Renderer class
class DigitalRenderer : public SIDRenderer {
public:
	DigitalRenderer();
	virtual ~DigitalRenderer();

	virtual void Reset(void);
	virtual void EmulateLine(void);
	virtual void WriteRegister(uint16 adr, uint8 byte);
	virtual void NewPrefs(Prefs *prefs);
	virtual void Pause(void);
	virtual void Resume(void);

private:
	void init_sound(void);
	void calc_filter(void);

	void calc_buffer(int16 *buf, long count);

	bool ready;						// Flag: Renderer has initialized and is ready
	uint8 volume;					// Master volume
	bool v3_mute;					// Voice 3 muted
    uint8 pad00;

	static uint16 TriTable[0x1000*2];	// Tables for certain waveforms
	static const uint16 TriSawTable[0x100];
	static const uint16 TriRectTable[0x100];
	static const uint16 SawRectTable[0x100];
	static const uint16 TriSawRectTable[0x100];
	static const uint32 EGTable[16];	// Increment/decrement values for all A/D/R settings
	static const uint8 EGDRShift[256]; // For exponential approximation of D/R
	static const int16 SampleTab[16]; // Table for sampled voice

	DRVoice voice[3];				// Data for 3 voices

	uint8 f_type;					// Filter type
	uint8 f_freq;					// SID filter frequency (upper 8 bits)
	uint8 f_res;					// Filter resonance (0..15)
    uint8 pad01;

#ifdef USE_FIXPOINT_MATHS
	FixPoint f_ampl;
	FixPoint d1, d2, g1, g2;
	int32 xn1, xn2, yn1, yn2;		// can become very large
	FixPoint sidquot;
#ifdef PRECOMPUTE_RESONANCE
	FixPoint resonanceLP[256];
	FixPoint resonanceHP[256];
#endif
#else
	float f_ampl;					// IIR filter input attenuation
	float d1, d2, g1, g2;			// IIR filter coefficients
	float xn1, xn2, yn1, yn2;		// IIR filter previous input/output signal
#ifdef PRECOMPUTE_RESONANCE
	float resonanceLP[256];			// shortcut for calc_filter
	float resonanceHP[256];
#endif
#endif

	uint8 sample_buf[SAMPLE_BUF_SIZE]; // Buffer for sampled voice
	int sample_in_ptr;				// Index in sample_buf for writing



#if 1
	int devfd, sndbufsize, buffer_rate;
	int16* sound_buffer;//[SAMPLE_FREQ / 25];
#endif

};
