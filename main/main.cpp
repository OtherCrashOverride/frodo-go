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

        
    //ThePrefs.Load(prefs_path);

    ThePrefs.Emul1541Proc = true;
    ThePrefs.DriveType[0] = DRVTYPE_D64;
    strcpy(ThePrefs.DrivePath[0], "/sd/roms/c64/ULTIMA4A.D64");

	// Create and start C64
	TheC64 = new C64;
	if (load_rom_files())
    {
		TheC64->Run();
    }

    printf("load_rom_files failed.\n");

    while(1)
    {
        vTaskDelay(1);
    }
}
