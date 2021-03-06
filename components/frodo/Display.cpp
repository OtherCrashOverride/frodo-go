/*
 *  Display.cpp - C64 graphics display, emulator window handling
 *
 *  Frodo (C) 1994-1997,2002 Christian Bauer
 */

#include "sysdeps.h"

#include "Display.h"
#include "main.h"
#include "Prefs.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// LED states
enum {
	LED_OFF,		// LED off
	LED_ON,			// LED on (green)
	LED_ERROR_ON,	// LED blinking (red), currently on
	LED_ERROR_OFF	// LED blinking, currently off
};


#undef USE_THEORETICAL_COLORS

#ifdef USE_THEORETICAL_COLORS

// C64 color palette (theoretical values)
const uint8 palette_red[16] = {
	0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0x80, 0xff, 0x40, 0x80, 0x80, 0x80, 0xc0
};

const uint8 palette_green[16] = {
	0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x80, 0x40, 0x80, 0x40, 0x80, 0xff, 0x80, 0xc0
};

const uint8 palette_blue[16] = {
	0x00, 0xff, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x80, 0x40, 0x80, 0x80, 0xff, 0xc0
};

#else

// C64 color palette (more realistic looking colors)
const uint8 palette_red[16] = {
	0x00, 0xff, 0x99, 0x00, 0xcc, 0x44, 0x11, 0xff, 0xaa, 0x66, 0xff, 0x40, 0x80, 0x66, 0x77, 0xc0
};

const uint8 palette_green[16] = {
	0x00, 0xff, 0x00, 0xff, 0x00, 0xcc, 0x00, 0xff, 0x55, 0x33, 0x66, 0x40, 0x80, 0xff, 0x77, 0xc0
};

const uint8 palette_blue[16] = {
	0x00, 0xff, 0x00, 0xcc, 0xcc, 0x44, 0x99, 0x00, 0x00, 0x00, 0x66, 0x40, 0x80, 0x66, 0xff, 0xc0
};

#endif


/*
 *  Update drive LED display (deferred until Update())
 */

void C64Display::UpdateLEDs(int l0, int l1, int l2, int l3)
{
	led_state[0] = l0;
	led_state[1] = l1;
	led_state[2] = l2;
	led_state[3] = l3;
}



#include "C64.h"
#include "SAM.h"
#include "Version.h"

#include <stdint.h>
#include <queue>

extern "C"
{
#include "../odroid/odroid_display.h"
#include "../odroid/odroid_keyboard.h"
#include "../odroid/odroid_input.h"
#include "../odroid/odroid_audio.h"
#include "../odroid/odroid_system.h"

#include "../../main/image_keyboard_error.h"
}



// Display surface
uint8* framebuffer = NULL;
uint16_t display_palette16[16];
QueueHandle_t vidQueue;


// Keyboard
static bool num_locked = false;

// For LED error blinking
static C64Display *c64_disp;
static struct sigaction pulse_sa;
static itimerval pulse_tv;

// Colors for speedometer/drive LEDs
enum {
	black = 0,
	white = 1,
	fill_gray = 16,
	shine_gray = 17,
	shadow_gray = 18,
	red = 19,
	green = 20,
	PALETTE_SIZE = 21
};

/*
  C64 keyboard matrix:

    Bit 7   6   5   4   3   2   1   0
  0    CUD  F5  F3  F1  F7 CLR RET DEL
  1    SHL  E   S   Z   4   A   W   3
  2     X   T   F   C   6   D   R   5
  3     V   U   H   B   8   G   Y   7
  4     N   O   K   M   0   J   I   9
  5     ,   @   :   .   -   L   P   +
  6     /   ^   =  SHR HOM  ;   *   �
  7    R/S  Q   C= SPC  2  CTL  <-  1
*/

#define MATRIX(a,b) (((a) << 3) | (b))


void videoTask(void *arg)
{
    while(1)
    {
        uint8_t* param;
        xQueuePeek(vidQueue, &param, portMAX_DELAY);
        //
        // if (param == (uint16_t*)1)
        //     break;

        //ili9341_write_frame_rectangleLE(0, 0, 320, 240, (uint16_t*)framebuffer);

        //memcpy(framebuffer, param, sizeof(framebuffer));
        ili9341_write_frame_c64(framebuffer, display_palette16);

        xQueueReceive(vidQueue, &param, portMAX_DELAY);
    }

    odroid_display_lock();

    // Draw hourglass
    odroid_display_show_hourglass();

    odroid_display_unlock();

    vTaskDelete(NULL);

    while (1) {}
}

/*
 *  Open window
 */

int init_graphics(void)
{

    framebuffer = (uint8*)malloc(DISPLAY_X * DISPLAY_Y * sizeof(uint8));
    if (!framebuffer) abort();

	printf("%s: framebuffer=%p\n", __func__, framebuffer);

	vidQueue = xQueueCreate(1, sizeof(uint16_t*));
	xTaskCreatePinnedToCore(&videoTask, "videoTask", 1024 * 4, NULL, 5, NULL, 1);

	return 1;
}

SemaphoreHandle_t keyboardMutex;
std::queue<odroid_keyboard_event_t> keyboardQueue;

void keyboard_callback(odroid_keystate_t state, odroid_key_t key)
{
	odroid_keyboard_event_t event;
	event.state = state;
	event.key = key;
	
	xSemaphoreTake(keyboardMutex, portMAX_DELAY);
	keyboardQueue.push(event);
	xSemaphoreGive(keyboardMutex);
}

/*
 *  Display constructor
 */

C64Display::C64Display(C64 *the_c64) : TheC64(the_c64)
{
	quit_requested = false;
	//speedometer_string[0] = 0;

	// LEDs off
	for (int i=0; i<4; i++)
		led_state[i] = old_led_state[i] = LED_OFF;

	// Start timer for LED error blinking
	c64_disp = this;
	// pulse_sa.sa_handler = (void (*)(int))pulse_handler;
	// pulse_sa.sa_flags = 0;
	// sigemptyset(&pulse_sa.sa_mask);
	// sigaction(SIGALRM, &pulse_sa, NULL);
	// pulse_tv.it_interval.tv_sec = 0;
	// pulse_tv.it_interval.tv_usec = 400000;
	// pulse_tv.it_value.tv_sec = 0;
	// pulse_tv.it_value.tv_usec = 400000;
	// setitimer(ITIMER_REAL, &pulse_tv, NULL);

	init_graphics();

	keyboardMutex = xSemaphoreCreateMutex();
	if(keyboardMutex == NULL) abort();

	odroid_keyboard_event_callback_set(&keyboard_callback);
	if (!odroid_keyboard_init())
	{
		printf("ERROR: Keyboard not found.\n");

		ili9341_write_frame_rectangleLE(0, 0, 320, 240, (uint16_t*)image_keyboard_error.pixel_data);

		while(1)
		{
			// loop forever
			vTaskDelay(1);
		}
	}

	odroid_input_battery_level_init();
}


/*
 *  Display destructor
 */

C64Display::~C64Display()
{
	//SDL_Quit();
}


/*
 *  Prefs may have changed
 */

void C64Display::NewPrefs(Prefs *prefs)
{
}


/*
 *  Redraw bitmap
 */

//uint16_t* tempfb;
uint32_t frameCount;
uint totalElapsedTime = 0;
uint renderFrames = 0;
uint startTime = 0;

void C64Display::Update(void)
{
	//uint startTime = xthal_get_ccount();

	if (!(frameCount & 1))
	{
#if 0
		if (!tempfb)
		{
			tempfb = (uint16_t*)heap_caps_malloc(320 * 240 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
			if (!tempfb) abort();		
		}


		//const int FB_SIZE = DISPLAY_X * DISPLAY_Y;
		int offX = (DISPLAY_X - 320) / 2;
		int offY = (DISPLAY_Y - 240) / 2;

		for (int y = 0; y < 240; ++y)
		{
			for (int x = 0; x < 320; ++x)
			{
				uint8 idx = framebuffer[((y + offY) * DISPLAY_X) + (x + offX)];
				tempfb[y * 320 + x] = display_palette16[idx];
			}
		}

		ili9341_write_frame_rectangleLE(0, 0, 320, 240, (uint16_t*)tempfb);
#else
		//ili9341_write_frame_c64(framebuffer, display_palette16);

#if 1
		uint8_t* fb = framebuffer;
		xQueueSend(vidQueue, &fb, portMAX_DELAY);
#else
		ili9341_write_frame_c64(framebuffer, display_palette16);
#endif

		++renderFrames;
#endif
		//printf("%s: done.\n", __func__);
	}



	++frameCount;

	if (frameCount == 50)
	{
		uint stopTime = xthal_get_ccount();

		odroid_battery_state battery;
		odroid_input_battery_level_read(&battery);

		int elapsedTime;
		if (stopTime >= startTime)
			elapsedTime = (stopTime - startTime);
		else
			elapsedTime = ((uint64_t)stopTime + (uint64_t)0xffffffff) - (startTime);


		float seconds = elapsedTime / (CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000.0f);
		float fps = frameCount / seconds;
		float renderFps = renderFrames / seconds;

		//printf("CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ=%d, totalElapsedTime=%d, seconds=%f\n", CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ, totalElapsedTime, seconds);

		printf("HEAP:0x%x (%#08x), SIM:%f, REN:%f, BATT:%d [%d%%]\n",
			esp_get_free_heap_size(),
			heap_caps_get_free_size(MALLOC_CAP_DMA),
			fps,
			renderFps,
			battery.millivolts,
			battery.percentage);

		frameCount = 0;
		renderFrames = 0;
		startTime = stopTime;
	}
}


/*
 *  Draw string into surface using the C64 ROM font
 */

// void C64Display::draw_string(SDL_Surface *s, int x, int y, const char *str, uint8 front_color, uint8 back_color)
// {
// 	// uint8 *pb = (uint8 *)s->pixels + s->pitch*y + x;
// 	// char c;
// 	// while ((c = *str++) != 0) {
// 	// 	uint8 *q = TheC64->Char + c*8 + 0x800;
// 	// 	uint8 *p = pb;
// 	// 	for (int y=0; y<8; y++) {
// 	// 		uint8 v = *q++;
// 	// 		p[0] = (v & 0x80) ? front_color : back_color;
// 	// 		p[1] = (v & 0x40) ? front_color : back_color;
// 	// 		p[2] = (v & 0x20) ? front_color : back_color;
// 	// 		p[3] = (v & 0x10) ? front_color : back_color;
// 	// 		p[4] = (v & 0x08) ? front_color : back_color;
// 	// 		p[5] = (v & 0x04) ? front_color : back_color;
// 	// 		p[6] = (v & 0x02) ? front_color : back_color;
// 	// 		p[7] = (v & 0x01) ? front_color : back_color;
// 	// 		p += s->pitch;
// 	// 	}
// 	// 	pb += 8;
// 	// }
// }


/*
 *  LED error blink
 */

// void C64Display::pulse_handler(...)
// {
// 	for (int i=0; i<4; i++)
// 		switch (c64_disp->led_state[i]) {
// 			case LED_ERROR_ON:
// 				c64_disp->led_state[i] = LED_ERROR_OFF;
// 				break;
// 			case LED_ERROR_OFF:
// 				c64_disp->led_state[i] = LED_ERROR_ON;
// 				break;
// 		}
// }


/*
 *  Draw speedometer
 */

void C64Display::Speedometer(int speed)
{
	static int delay = 0;

	if (delay >= 20) {
		delay = 0;
		//sprintf(speedometer_string, "%d%%", speed);
	} else
		delay++;
}


/*
 *  Return pointer to bitmap data
 */

uint8 *C64Display::BitmapBase(void)
{
	//return (uint8 *)screen->pixels;
	return framebuffer;
}


/*
 *  Return number of bytes per row
 */

int C64Display::BitmapXMod(void)
{
	//return screen->pitch;
	return DISPLAY_X;
}


bool func_flag = false;
bool gamepad_disabled = false;

enum VKEY
{
	VKEY_UP_ARROW = 0x100,
	VKEY_DOWN_ARROW,
	VKEY_LEFT_ARROW,
	VKEY_RIGHT_ARROW,

	VKEY_F1,
	VKEY_F2,
	VKEY_F3,
	VKEY_F4,
	VKEY_F5,
	VKEY_F6,
	VKEY_F7,
	VKEY_F8,

};


/*
 *  Poll the keyboard
 */

static void translate_key(int key, bool key_up, uint8 *key_matrix, uint8 *rev_matrix, uint8 *joystick)
{
#if 1
	int c64_key = -1;
	switch (key) {
		case ODROID_KEY_A: c64_key = MATRIX(1,2); break;
		case ODROID_KEY_B: c64_key = MATRIX(3,4); break;
		case ODROID_KEY_C: c64_key = MATRIX(2,4); break;
		case ODROID_KEY_D: c64_key = MATRIX(2,2); break;
		case ODROID_KEY_E: c64_key = MATRIX(1,6); break;
		case ODROID_KEY_F: c64_key = MATRIX(2,5); break;
		case ODROID_KEY_G: c64_key = MATRIX(3,2); break;
		case ODROID_KEY_H: c64_key = MATRIX(3,5); break;
		case ODROID_KEY_I: c64_key = MATRIX(4,1); break;
		case ODROID_KEY_J: c64_key = MATRIX(4,2); break;
		case ODROID_KEY_K: c64_key = MATRIX(4,5); break;
		case ODROID_KEY_L: c64_key = MATRIX(5,2); break;
		case ODROID_KEY_M: c64_key = MATRIX(4,4); break;
		case ODROID_KEY_N: c64_key = MATRIX(4,7); break;
		case ODROID_KEY_O: c64_key = MATRIX(4,6); break;
		case ODROID_KEY_P: c64_key = MATRIX(5,1); break;
		case ODROID_KEY_Q: c64_key = MATRIX(7,6); break;
		case ODROID_KEY_R: c64_key = MATRIX(2,1); break;
		case ODROID_KEY_S: c64_key = MATRIX(1,5); break;
		case ODROID_KEY_T: c64_key = MATRIX(2,6); break;
		case ODROID_KEY_U: c64_key = MATRIX(3,6); break;
		case ODROID_KEY_V: c64_key = MATRIX(3,7); break;
		case ODROID_KEY_W: c64_key = MATRIX(1,1); break;
		case ODROID_KEY_X: c64_key = MATRIX(2,7); break;
		case ODROID_KEY_Y: c64_key = MATRIX(3,1); break;
		case ODROID_KEY_Z: c64_key = MATRIX(1,4); break;

		case ODROID_KEY_0: c64_key = MATRIX(4,3); break;
		case ODROID_KEY_1: 
			c64_key = func_flag ? MATRIX(0,4) : MATRIX(7,0); 
			break;
		case ODROID_KEY_2: 
			c64_key = func_flag ? MATRIX(0,4) | 0x80 : MATRIX(7,3); 
			break;
		case ODROID_KEY_3: 
			c64_key = func_flag ? MATRIX(0,5) : MATRIX(1,0); 
			break;
		case ODROID_KEY_4:
			c64_key = func_flag ? MATRIX(0,5) | 0x80 : MATRIX(1,3); 
			break;
		case ODROID_KEY_5:
			c64_key = func_flag ? MATRIX(0,6) : MATRIX(2,0);
			break;
		case ODROID_KEY_6:
			c64_key = func_flag ? MATRIX(0,6) | 0x80 : MATRIX(2,3);
			break;
		case ODROID_KEY_7:
			c64_key = func_flag ? MATRIX(0,3) : MATRIX(3,0);
			break;
		case ODROID_KEY_8: 
			c64_key = func_flag ? MATRIX(0,3) | 0x80 : MATRIX(3,3); 
			break;
		case ODROID_KEY_9:
			c64_key = func_flag ? MATRIX(6,0) : MATRIX(4,0); 
			break;

		case ODROID_KEY_SPACE: c64_key = MATRIX(7,4); break;
		case ODROID_KEY_GRAVE_ACCENT:
			c64_key = func_flag ? MATRIX(6,3) : MATRIX(7,1); 
			break;
		case ODROID_KEY_BACKSLASH:
			c64_key = func_flag ? MATRIX(6,6) : MATRIX(6,5);
			break;
		case ODROID_KEY_COMMA: c64_key = MATRIX(5,7); break;
		case ODROID_KEY_PERIOD: c64_key = MATRIX(5,4); break;
		case ODROID_KEY_MINUS: c64_key = MATRIX(5,0); break;
		case ODROID_KEY_EQUALS: c64_key = MATRIX(5,3); break;
		case ODROID_KEY_LEFTBRACKET: c64_key = MATRIX(5,6); break;
		case ODROID_KEY_RIGHTBRACKET: c64_key = MATRIX(6,1); break;
		case ODROID_KEY_SEMICOLON: c64_key = MATRIX(5,5); break;
		case ODROID_KEY_APOSTROPHE: c64_key = MATRIX(6,2); break;
		case ODROID_KEY_SLASH: c64_key = MATRIX(6,7); break;

		case ODROID_KEY_ESCAPE: c64_key = MATRIX(7,7); break;
		case ODROID_KEY_ENTER: c64_key = MATRIX(0,1); break;
		case ODROID_KEY_BACKSPACE: c64_key = MATRIX(0,0); break;
		//case ODROID_KEY_Insert: c64_key = MATRIX(6,3); break;
		//case ODROID_KEY_Home: c64_key = MATRIX(6,3); break;
		//case ODROID_KEY_End: c64_key = MATRIX(6,0); break;
		//case ODROID_KEY_PageUp: c64_key = MATRIX(6,0); break;
		//case ODROID_KEY_PageDown: c64_key = MATRIX(6,5); break;

		case ODROID_KEY_CONTROL: /*case ODROID_KEY_Tab:*/ c64_key = MATRIX(7,2); break;
		//case ODROID_KEY_RightControl: c64_key = MATRIX(7,5); break;
		case ODROID_KEY_SHIFT:
			c64_key = func_flag ? MATRIX(6,4) : MATRIX(1,7);
			break;
		//case ODROID_KEY_RightShift: c64_key = MATRIX(6,4); break;
		case ODROID_KEY_ALTERNATE: /*case ODROID_KEY_LMETA:*/ c64_key = MATRIX(7,5); break;
		//case ODROID_KEY_RightAlt: /*case ODROID_KEY_RMETA:*/ c64_key = MATRIX(7,5); break;

		case VKEY_UP_ARROW: c64_key = MATRIX(0,7)| 0x80; break;
		case VKEY_DOWN_ARROW: c64_key = MATRIX(0,7); break;
		case VKEY_LEFT_ARROW: c64_key = MATRIX(0,2) | 0x80; break;
		case VKEY_RIGHT_ARROW: c64_key = MATRIX(0,2); break;

		case VKEY_F1: c64_key = MATRIX(0,4); break;
		case VKEY_F2: c64_key = MATRIX(0,4) | 0x80; break;
		case VKEY_F3: c64_key = MATRIX(0,5); break;
		case VKEY_F4: c64_key = MATRIX(0,5) | 0x80; break;
		case VKEY_F5: c64_key = MATRIX(0,6); break;
		case VKEY_F6: c64_key = MATRIX(0,6) | 0x80; break;
		case VKEY_F7: c64_key = MATRIX(0,3); break;
		case VKEY_F8: c64_key = MATRIX(0,3) | 0x80; break;

		// case OdroidKey_KP0: case OdroidKey_KP5: c64_key = 0x10 | 0x40; break;
		// case OdroidKey_KP1: c64_key = 0x06 | 0x40; break;
		// case OdroidKey_KP2: c64_key = 0x02 | 0x40; break;
		// case OdroidKey_KP3: c64_key = 0x0a | 0x40; break;
		// case OdroidKey_KP4: c64_key = 0x04 | 0x40; break;
		// case OdroidKey_KP6: c64_key = 0x08 | 0x40; break;
		// case OdroidKey_KP7: c64_key = 0x05 | 0x40; break;
		// case OdroidKey_KP8: c64_key = 0x01 | 0x40; break;
		// case OdroidKey_KP9: c64_key = 0x09 | 0x40; break;

		// case OdroidKey_KP_DIVIDE: c64_key = MATRIX(6,7); break;
		// case OdroidKey_KP_ENTER: c64_key = MATRIX(0,1); break;

		default:
			break;
	}

	if (c64_key < 0)
		return;

	// // Handle joystick emulation
	// if (c64_key & 0x40) {
	// 	c64_key &= 0x1f;
	// 	if (key_up)
	// 		*joystick |= c64_key;
	// 	else
	// 		*joystick &= ~c64_key;
	// 	return;
	// }

	// Handle other keys
	bool shifted = c64_key & 0x80;
	int c64_byte = (c64_key >> 3) & 7;
	int c64_bit = c64_key & 7;
	if (key_up) {
		if (shifted) {
			key_matrix[6] |= 0x10;
			rev_matrix[4] |= 0x40;
		}
		key_matrix[c64_byte] |= (1 << c64_bit);
		rev_matrix[c64_bit] |= (1 << c64_byte);
	} else {
		if (shifted) {
			key_matrix[6] &= 0xef;
			rev_matrix[4] &= 0xbf;
		}
		key_matrix[c64_byte] &= ~(1 << c64_bit);
		rev_matrix[c64_bit] &= ~(1 << c64_byte);
	}
#endif
}



odroid_gamepad_state prev_gamepad;
int activeLEDs = 0;

void C64Display::PollKeyboard(uint8 *key_matrix, uint8 *rev_matrix, uint8 *joystick)
{
	odroid_gamepad_state gamepad;
	odroid_input_gamepad_read(&gamepad);

	if (gamepad_disabled)
	{
		*joystick = 0xff;

		if (!prev_gamepad.values[ODROID_INPUT_UP] &&
				gamepad.values[ODROID_INPUT_UP])
		{
			keyboard_callback(ODROID_KEY_PRESSED, (odroid_key_t)VKEY_UP_ARROW);
		}
		else if (prev_gamepad.values[ODROID_INPUT_UP] &&
				!gamepad.values[ODROID_INPUT_UP])
		{
			keyboard_callback(ODROID_KEY_RELEASED, (odroid_key_t)VKEY_UP_ARROW);
		}
		
		if (!prev_gamepad.values[ODROID_INPUT_DOWN] &&
				gamepad.values[ODROID_INPUT_DOWN])
		{
			keyboard_callback(ODROID_KEY_PRESSED, (odroid_key_t)VKEY_DOWN_ARROW);
		}
		else if (prev_gamepad.values[ODROID_INPUT_DOWN] &&
				!gamepad.values[ODROID_INPUT_DOWN])
		{
			keyboard_callback(ODROID_KEY_RELEASED, (odroid_key_t)VKEY_DOWN_ARROW);
		}

		if (!prev_gamepad.values[ODROID_INPUT_LEFT] &&
				gamepad.values[ODROID_INPUT_LEFT])
		{
			keyboard_callback(ODROID_KEY_PRESSED, (odroid_key_t)VKEY_LEFT_ARROW);
		}
		else if (prev_gamepad.values[ODROID_INPUT_LEFT] &&
				!gamepad.values[ODROID_INPUT_LEFT])
		{
			keyboard_callback(ODROID_KEY_RELEASED, (odroid_key_t)VKEY_LEFT_ARROW);
		}

		if (!prev_gamepad.values[ODROID_INPUT_RIGHT] &&
				gamepad.values[ODROID_INPUT_RIGHT])
		{
			keyboard_callback(ODROID_KEY_PRESSED, (odroid_key_t)VKEY_RIGHT_ARROW);
		}
		else if (prev_gamepad.values[ODROID_INPUT_RIGHT] &&
				!gamepad.values[ODROID_INPUT_RIGHT])
		{
			keyboard_callback(ODROID_KEY_RELEASED, (odroid_key_t)VKEY_RIGHT_ARROW);
		}

		if (!prev_gamepad.values[ODROID_INPUT_A] &&
				gamepad.values[ODROID_INPUT_A])
		{
			keyboard_callback(ODROID_KEY_PRESSED, ODROID_KEY_SPACE);
		}
		else if (prev_gamepad.values[ODROID_INPUT_A] &&
				!gamepad.values[ODROID_INPUT_A])
		{
			keyboard_callback(ODROID_KEY_RELEASED, ODROID_KEY_SPACE);
		}

		if (!prev_gamepad.values[ODROID_INPUT_B] &&
				gamepad.values[ODROID_INPUT_B])
		{
			keyboard_callback(ODROID_KEY_PRESSED, (odroid_key_t)VKEY_F1);
		}
		else if (prev_gamepad.values[ODROID_INPUT_B] &&
				!gamepad.values[ODROID_INPUT_B])
		{
			keyboard_callback(ODROID_KEY_RELEASED, (odroid_key_t)VKEY_F1);
		}
	}
	else
	{
		if (gamepad.values[ODROID_INPUT_UP])
			*joystick &= ~0x01;
		else
			*joystick |= 0x01;

		if (gamepad.values[ODROID_INPUT_DOWN])
			*joystick &= ~0x02;
		else
			*joystick |= 0x02;

		if (gamepad.values[ODROID_INPUT_LEFT])
			*joystick &= ~0x04;
		else
			*joystick |= 0x04;

		if (gamepad.values[ODROID_INPUT_RIGHT])
			*joystick &= ~0x08;
		else
			*joystick |= 0x08;

		if (gamepad.values[ODROID_INPUT_A])
			*joystick &= ~0x10;
		else
			*joystick |= 0x10; 
	}

	if (prev_gamepad.values[ODROID_INPUT_SELECT] &&
		!gamepad.values[ODROID_INPUT_SELECT])
	{
		func_flag = !func_flag;				
	}

	if (prev_gamepad.values[ODROID_INPUT_START] &&
		!gamepad.values[ODROID_INPUT_START])
	{
		gamepad_disabled = !gamepad_disabled;

		//odroid_system_led_set(gamepad_disabled);
		printf("%s: gamepad_disabled=%d\n", __func__, gamepad_disabled);		
	}

	if (!prev_gamepad.values[ODROID_INPUT_VOLUME] &&
		gamepad.values[ODROID_INPUT_VOLUME])
	{
		odroid_audio_volume_change();
		printf("%s: Volume=%d\n", __func__, odroid_audio_volume_get());
	}

	prev_gamepad = gamepad;

/*
	LED_OFF,		// LED off
	LED_ON,			// LED on (green)
	LED_ERROR_ON,	// LED blinking (red), currently on
	LED_ERROR_OFF	// LED blinking, currently off
*/

	int leds = func_flag ? ODROID_KEYBOARD_LED_Fn : 0;
	//leds |= (led_state[0] && frameCount < 25) ? 2 : 0;

	switch(led_state[0])
	{
		case LED_OFF:
		case LED_ERROR_OFF:
			break;

		case LED_ON:
		case LED_ERROR_ON:
			leds |= ODORID_KEYBOARD_LED_Aa;
			break;
	}
	
	leds |= gamepad_disabled ? ODROID_KEYBOARD_LED_St : 0;


	if (activeLEDs != leds)
	{
		odroid_keyboard_leds_set((odroid_keyboard_led_t)leds);
		activeLEDs = leds;
	}


	odroid_keyboard_event_t event;

	while(true)
	{
		xSemaphoreTake(keyboardMutex, portMAX_DELAY);
		
		size_t count = keyboardQueue.size();
		if (count > 0)
		{
			event = keyboardQueue.front();
			keyboardQueue.pop();
		}
		
		xSemaphoreGive(keyboardMutex);

		if (count < 1) break;

		switch(event.state)
		{
			case ODROID_KEY_PRESSED:
				switch (event.key)
				{
					// case OdroidKey_F10:	// F10: Quit
					// 	quit_requested = true;
					// 	break;

					// case OdroidKey_F11:	// F11: Reset
					//	TheC64->Reset();
					// 	break;

					case ODROID_KEY_0:	
						if (func_flag)// RESTORE key when Fn lock. Reset emulator if Grave Accent also pressed.
						{
                                                        odroid_keyboardstate_t kbstate = odroid_keyboard_state_get();
                                                        if (odroid_keyboard_state_key_get(&kbstate, ODROID_KEY_GRAVE_ACCENT) == ODROID_KEY_PRESSED) {
                                                                TheC64->Reset();
                                                                func_flag = false;
                                                        }
                                                        else
                                                        {
                                                                TheC64->NMI();
                                                        }
							break;
						}
						// fall through

					default:
						translate_key(event.key, false, key_matrix, rev_matrix, joystick);
						break;
				}
				break;

			case ODROID_KEY_RELEASED:
				translate_key(event.key, true, key_matrix, rev_matrix, joystick);
				break;
		}
	}
#if 0
	KeyEvent event;
	while(Keyboard_PollEvent(&event))
	{
		switch(event.State)
		{
			case KEYSTATE_DOWN:
				switch (event.KeyboardKey)
				{
					case OdroidKey_F10:	// F10: Quit
						quit_requested = true;
						break;

					case OdroidKey_F11:	// F11: NMI (Restore)
						TheC64->NMI();
						break;

					case OdroidKey_F12:	// F12: Reset
						TheC64->Reset();
						break;
					
					default:
						translate_key(event.KeyboardKey, false, key_matrix, rev_matrix, joystick);
						break;
				}
				break;

			case KEYSTATE_UP:
				translate_key(event.KeyboardKey, true, key_matrix, rev_matrix, joystick);
				break;
		}
	}
#endif

#if 0
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {

			// Key pressed
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {

					case SDLK_F9:	// F9: Invoke SAM
						SAM(TheC64);
						break;

					case SDLK_F10:	// F10: Quit
						quit_requested = true;
						break;

					case SDLK_F11:	// F11: NMI (Restore)
						TheC64->NMI();
						break;

					case SDLK_F12:	// F12: Reset
						TheC64->Reset();
						break;

					case SDLK_NUMLOCK:
						num_locked = true;
						break;

					case SDLK_KP_PLUS:	// '+' on keypad: Increase SkipFrames
						ThePrefs.SkipFrames++;
						break;

					case SDLK_KP_MINUS:	// '-' on keypad: Decrease SkipFrames
						if (ThePrefs.SkipFrames > 1)
							ThePrefs.SkipFrames--;
						break;

					case SDLK_KP_MULTIPLY:	// '*' on keypad: Toggle speed limiter
						ThePrefs.LimitSpeed = !ThePrefs.LimitSpeed;
						break;

					default:
						translate_key(event.key.keysym.sym, false, key_matrix, rev_matrix, joystick);
						break;
				}
				break;

			// Key released
			case SDL_KEYUP:
				if (event.key.keysym.sym == SDLK_NUMLOCK)
					num_locked = false;
				else
					translate_key(event.key.keysym.sym, true, key_matrix, rev_matrix, joystick);
				break;

			// Quit Frodo
			case SDL_QUIT:
				quit_requested = true;
				break;
		}
	}
#endif
}


/*
 *  Check if NumLock is down (for switching the joystick keyboard emulation)
 */

bool C64Display::NumLock(void)
{
	return num_locked;
}


/*
 *  Allocate C64 colors
 */

void C64Display::InitColors(uint8 *colors)
{
	for(int i = 0; i < 16; ++i)
	{
		uint8 r = palette_red[i];
		uint8 g = palette_green[i];
		uint8 b = palette_blue[i];

		//rrrr rggg gggb bbbb
        uint16_t rgb565 = ((r << 8) & 0xf800) | ((g << 3) & 0x07e0) | (b >> 3);
        display_palette16[i] = rgb565;
	}

	for (int i=0; i<256; i++)
		colors[i] = i & 0x0f;
}


/*
 *  Show a requester (error message)
 */

long int ShowRequester(char *a,char *b,char *)
{
	printf("%s: %s\n", a, b);
	return 1;
}

