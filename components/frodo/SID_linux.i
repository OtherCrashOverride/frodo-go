/*
 *  SID_linux.i - 6581 emulation, Linux specific stuff
 *
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 *  Linux sound stuff by Bernd Schmidt
 */

#include <unistd.h>


#include "VIC.h"

extern "C"
{
#include "../odroid/odroid_audio.h"
}

#define SOUND_CHANNEL_COUNT (2)

static int16_t* sampleBuffer;

/*
 *  Initialization
 */

void DigitalRenderer::init_sound(void)
{

#if 1
	odroid_audio_init(SAMPLE_FREQ);

    sndbufsize = (SAMPLE_FREQ / 25);
    
    sound_buffer = (int16_t*)malloc(sndbufsize);
    if (!sound_buffer) abort();

    ready = true;

    sampleBuffer = (int16*)malloc(sndbufsize * sizeof(short) * SOUND_CHANNEL_COUNT);
    if (!sampleBuffer) abort();

    printf("%s: sndbufsize=%d\n", __func__, sndbufsize);
#endif
}


/*
 *  Destructor
 */

DigitalRenderer::~DigitalRenderer()
{
#if 1
    odroid_audio_terminate();
#endif
}


/*
 *  Pause sound output
 */

void DigitalRenderer::Pause(void)
{
}


/*
 * Resume sound output
 */

void DigitalRenderer::Resume(void)
{
}


/*
 * Fill buffer, sample volume (for sampled voice)
 */

void DigitalRenderer::EmulateLine(void)
{
    static int divisor = 0;
    static int to_output = 0;
    static int buffer_pos = 0;

    if (!ready)
	    return;

	sample_buf[sample_in_ptr] = volume;
	sample_in_ptr = (sample_in_ptr + 1) % SAMPLE_BUF_SIZE;

    /*
     * Now see how many samples have to be added for this line
     */
    divisor += SAMPLE_FREQ;
    while (divisor >= 0)
	divisor -= TOTAL_RASTERS*SCREEN_FREQ, to_output++;

    /*
     * Calculate the sound data only when we have enough to fill
     * the buffer entirely.
     */
    if ((buffer_pos + to_output) >= sndbufsize) {
        int datalen = sndbufsize - buffer_pos;
        to_output -= datalen;
        calc_buffer(sound_buffer + buffer_pos, datalen*2);
        //write(devfd, sound_buffer, sndbufsize*2);

        
        for (int i = 0; i < sndbufsize; ++i)
        {
            sampleBuffer[i * 2] = sound_buffer[i];
            sampleBuffer[i * 2 + 1] = sound_buffer[i];
        }

        odroid_audio_submit(sampleBuffer, sndbufsize);

        buffer_pos = 0;
    }    
}
