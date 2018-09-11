#pragma once

#include <stdint.h>


typedef enum {
    ODROID_KEY_NONE = 0,
    
    ODROID_KEY_GRAVE_ACCENT = 1,
    ODROID_KEY_1,
    ODROID_KEY_2,
    ODROID_KEY_3,
    ODROID_KEY_4,
    ODROID_KEY_5,
    ODROID_KEY_6,
    ODROID_KEY_7,

    ODROID_KEY_8 = 11,
    ODROID_KEY_9,
    ODROID_KEY_0,
    ODROID_KEY_ESCAPE,
    ODROID_KEY_Q,
    ODROID_KEY_W,
    ODROID_KEY_E,
    ODROID_KEY_R,

    ODROID_KEY_T = 21,
    ODROID_KEY_Y,
    ODROID_KEY_U,
    ODROID_KEY_I,
    ODROID_KEY_O,
    ODROID_KEY_P,
    ODROID_KEY_CONTROL,
    ODROID_KEY_A,

    ODROID_KEY_S = 31,
    ODROID_KEY_D,
    ODROID_KEY_F,
    ODROID_KEY_G,
    ODROID_KEY_H,
    ODROID_KEY_J,
    ODROID_KEY_K,
    ODROID_KEY_L,

    ODROID_KEY_BACKSPACE = 41,
    ODROID_KEY_ALTERNATE,
    ODROID_KEY_Z,
    ODROID_KEY_X,
    ODROID_KEY_C,
    ODROID_KEY_V,
    ODROID_KEY_B,
    ODROID_KEY_N,

    ODROID_KEY_M = 51,
    ODROID_KEY_BACKSLASH,
    ODROID_KEY_ENTER,
    ODROID_KEY_SHIFT,
    ODROID_KEY_SEMICOLON,
    ODROID_KEY_APOSTROPHE,
    ODROID_KEY_MINUS,
    ODROID_KEY_EQUALS,
    
    ODROID_KEY_SPACE = 61,
    ODROID_KEY_COMMA,
    ODROID_KEY_PERIOD,
    ODROID_KEY_SLASH,
    ODROID_KEY_LEFTBRACKET,
    ODROID_KEY_RIGHTBRACKET,

} odroid_key_t;

typedef enum
{
    ODROID_KEY_RELEASED = 0,
    ODROID_KEY_PRESSED
} odroid_keystate_t;

typedef struct
{
    uint8_t rows[8];
} odroid_keyboardstate_t;

typedef enum
{
    ODROID_KEYBOARD_LED_NONE = 0,
    ODROID_KEYBOARD_LED_Fn = (1 << 0),
    ODORID_KEYBOARD_LED_Aa = (1 << 1),
    ODROID_KEYBOARD_LED_St = (1 << 2)
} odroid_keyboard_led_t;

typedef struct
{
    odroid_keystate_t state;
    odroid_key_t key;
} odroid_keyboard_event_t;




void odroid_keyboard_init();
void odroid_keyboard_state_key_set(odroid_keyboardstate_t* state, odroid_key_t key, odroid_keystate_t value);
odroid_keystate_t odroid_keyboard_state_key_get(odroid_keyboardstate_t* state, odroid_key_t key);
odroid_keyboardstate_t odroid_keyboard_state_get();
odroid_keyboard_led_t odroid_keyboard_leds_get();
void odroid_keyboard_leds_set(odroid_keyboard_led_t value);
void odroid_keyboard_event_callback_set(void (*callback)(odroid_keystate_t, odroid_key_t));
