#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_partition.h>
#include <driver/i2s.h>
#include <esp_spiffs.h>
#include <nvs_flash.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <esp_ota_ops.h>

extern "C"
{
#include "../components/odroid/odroid_settings.h"
#include "../components/odroid/odroid_audio.h"
#include "../components/odroid/odroid_input.h"
#include "../components/odroid/odroid_system.h"
#include "../components/odroid/odroid_display.h"
#include "../components/odroid/odroid_sdcard.h"

#include "../components/ugui/ugui.h"
}


// Frodo
#include "../components/frodo/Prefs.h"
#include "../components/frodo/C64.h"


#include <dirent.h>
#include <string.h>
#include <ctype.h>

#define ESP32_PSRAM (0x3f800000)
const char* SD_BASE_PATH = "/sd";

#define AUDIO_SAMPLE_RATE (32000)

// QueueHandle_t vidQueue;



// void videoTask(void *arg)
// {
//     while(1)
//     {
//         uint8_t* param;
//         xQueuePeek(vidQueue, &param, portMAX_DELAY);
//         //
//         // if (param == (uint16_t*)1)
//         //     break;

//         ili9341_write_frame_rectangleLE(0, 0, 320, 240, (uint16_t*)framebuffer);

//         //memcpy(framebuffer, param, sizeof(framebuffer));
//         //ili9341_write_frame_atari7800(framebuffer, display_palette16);

//         xQueueReceive(vidQueue, &param, portMAX_DELAY);

//     }

//     odroid_display_lock_sms_display();

//     // Draw hourglass
//     odroid_display_show_hourglass();

//     odroid_display_unlock_sms_display();

//     vTaskDelete(NULL);

//     while (1) {}
// }

#define BASIC_ROM_FILE	"/sd/roms/c64/Basic.rom"
#define KERNAL_ROM_FILE	"/sd/roms/c64/Kernal.rom"
#define CHAR_ROM_FILE	"/sd/roms/c64/Char.rom"
#define FLOPPY_ROM_FILE	"/sd/roms/c64/1541.rom"

C64* TheC64;

bool load_rom_files(void)
{
	FILE *file;

	// Load Basic ROM
	if ((file = fopen(BASIC_ROM_FILE, "rb")) != NULL) {
		if (fread(TheC64->Basic, 1, 0x2000, file) != 0x2000) {
            abort();
			//ShowRequester("Can't read 'Basic ROM'.", "Quit");
			return false;
		}
		fclose(file);
	} else {
        abort();
		//ShowRequester("Can't find 'Basic ROM'.", "Quit");
		return false;
	}

	// Load Kernal ROM
	if ((file = fopen(KERNAL_ROM_FILE, "rb")) != NULL) {
		if (fread(TheC64->Kernal, 1, 0x2000, file) != 0x2000) {
			//ShowRequester("Can't read 'Kernal ROM'.", "Quit");
			return false;
		}
		fclose(file);
	} else {
		//ShowRequester("Can't find 'Kernal ROM'.", "Quit");
		return false;
	}

	// Load Char ROM
	if ((file = fopen(CHAR_ROM_FILE, "rb")) != NULL) {
		if (fread(TheC64->Char, 1, 0x1000, file) != 0x1000) {
			//ShowRequester("Can't read 'Char ROM'.", "Quit");
			return false;
		}
		fclose(file);
	} else {
		//ShowRequester("Can't find 'Char ROM'.", "Quit");
		return false;
	}

	// Load 1541 ROM
	if ((file = fopen(FLOPPY_ROM_FILE, "rb")) != NULL) {
		if (fread(TheC64->ROM1541, 1, 0x4000, file) != 0x4000) {
			//ShowRequester("Can't read '1541 ROM'.", "Quit");
			return false;
		}
		fclose(file);
	} else {
		//ShowRequester("Can't find '1541 ROM'.", "Quit");
		return false;
	}

	return true;
}

void emu_init()
{
    // framebuffer = (uint8_t*)malloc(320 * 240); // heap_caps_malloc(VIDEO_WIDTH * VIDEO_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    // if (!framebuffer) abort();
    
    //ThePrefs.Load(prefs_path);

	// Create and start C64
	TheC64 = new C64;
	if (load_rom_files())
		TheC64->Run();
}


void emu_step(odroid_gamepad_state* gamepad)
{

    //odroid_audio_submit((int16_t*)sampleBuffer, length);
}


bool RenderFlag;
extern "C" void app_main()
{
    printf("frodo-go started.\n");

    printf("HEAP:0x%x (%#08x)\n", esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_DMA));


    nvs_flash_init();

    odroid_system_init();
    odroid_input_gamepad_init();
    odroid_input_battery_level_init();

    ili9341_prepare();

    ili9341_init();
    ili9341_clear(0x0000);

    //vTaskDelay(500 / portTICK_RATE_MS);

    // Open SD card
    esp_err_t r = odroid_sdcard_open(SD_BASE_PATH);
    if (r != ESP_OK)
    {
        odroid_display_show_sderr(ODROID_SD_ERR_NOCARD);
        abort();
    }


    //const char* romfile = ChooseFile();
    //printf("%s: filename='%s'\n", __func__, romfile);


    ili9341_clear(0x0000);
  
    odroid_audio_init(AUDIO_SAMPLE_RATE);

    emu_init();

    // vidQueue = xQueueCreate(1, sizeof(uint16_t*));
    // xTaskCreatePinnedToCore(&videoTask, "videoTask", 1024 * 4, NULL, 5, NULL, 1);


    // uint startTime;
    // uint stopTime;
    // uint totalElapsedTime = 0;
    // int frame = 0;
    // int renderFrames = 0;
    // uint16_t muteFrameCount = 0;
    // uint16_t powerFrameCount = 0;

    // odroid_gamepad_state last_gamepad;
    // odroid_input_gamepad_read(&last_gamepad);

    while(1)
    {
        // startTime = xthal_get_ccount();


        // odroid_gamepad_state gamepad;
        // odroid_input_gamepad_read(&gamepad);

        // if (last_gamepad.values[ODROID_INPUT_MENU] &&
        //     !gamepad.values[ODROID_INPUT_MENU])
        // {
        //     esp_restart();
        // }

        // if (!last_gamepad.values[ODROID_INPUT_VOLUME] &&
        //     gamepad.values[ODROID_INPUT_VOLUME])
        // {
        //     odroid_audio_volume_change();
        //     printf("%s: Volume=%d\n", __func__, odroid_audio_volume_get());
        // }


        // emu_step(&gamepad);
        // //printf("stepped.\n");


        // if (RenderFlag)
        // {
        //     uint8_t* fb = framebuffer;
        //     xQueueSend(vidQueue, &fb, portMAX_DELAY);

        //     ++renderFrames;
        // }

        // last_gamepad = gamepad;


        // // end of frame
        // stopTime = xthal_get_ccount();


        // odroid_battery_state battery;
        // odroid_input_battery_level_read(&battery);


        // int elapsedTime;
        // if (stopTime > startTime)
        //     elapsedTime = (stopTime - startTime);
        // else
        //     elapsedTime = ((uint64_t)stopTime + (uint64_t)0xffffffff) - (startTime);

        // totalElapsedTime += elapsedTime;
        // ++frame;

        // if (frame == 60)
        // {
        //     float seconds = totalElapsedTime / (CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000.0f);
        //     float fps = frame / seconds;
        //     float renderFps = renderFrames / seconds;

        //     printf("HEAP:0x%x (%#08x), SIM:%f, REN:%f, BATTERY:%d [%d]\n",
        //         esp_get_free_heap_size(),
        //         heap_caps_get_free_size(MALLOC_CAP_DMA),
        //         fps,
        //         renderFps,
        //         battery.millivolts,
        //         battery.percentage);

        //     frame = 0;
        //     renderFrames = 0;
        //     totalElapsedTime = 0;
        // }
    }
}
