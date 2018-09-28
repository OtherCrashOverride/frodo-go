#include "odroid_keyboard.h"

#include <freertos/FreeRTOS.h>
#include <driver/i2c.h>


#define KEYBOARD_I2C_NUM             I2C_NUM_1
#define KEYBOARD_I2C_SCL_IO          GPIO_NUM_12
#define KEYBOARD_I2C_SDA_IO          GPIO_NUM_15
#define KEYBOARD_I2C_FREQ_HZ         400000
#define KEYBOARD_INT                 GPIO_NUM_4

#define TX_BUF_DISABLE  0
#define RX_BUF_DISABLE  0
#define ACK_CHECK_EN    0x1
#define ACK_CHECK_DIS   0x0
#define ACK_VAL         0x0
#define NACK_VAL        0x1


#define TCA8418_ADDR 0x34

/* TCA8418 hardware limits */
#define TCA8418_MAX_ROWS 8
#define TCA8418_MAX_COLS 10

/* TCA8418 register offsets */
#define REG_CFG 0x01
#define REG_INT_STAT 0x02
#define REG_KEY_LCK_EC 0x03
#define REG_KEY_EVENT_A 0x04
#define REG_KEY_EVENT_B 0x05
#define REG_KEY_EVENT_C 0x06
#define REG_KEY_EVENT_D 0x07
#define REG_KEY_EVENT_E 0x08
#define REG_KEY_EVENT_F 0x09
#define REG_KEY_EVENT_G 0x0A
#define REG_KEY_EVENT_H 0x0B
#define REG_KEY_EVENT_I 0x0C
#define REG_KEY_EVENT_J 0x0D
#define REG_KP_LCK_TIMER 0x0E
#define REG_UNLOCK1 0x0F
#define REG_UNLOCK2 0x10
#define REG_GPIO_INT_STAT1 0x11
#define REG_GPIO_INT_STAT2 0x12
#define REG_GPIO_INT_STAT3 0x13
#define REG_GPIO_DAT_STAT1 0x14
#define REG_GPIO_DAT_STAT2 0x15
#define REG_GPIO_DAT_STAT3 0x16
#define REG_GPIO_DAT_OUT1 0x17
#define REG_GPIO_DAT_OUT2 0x18
#define REG_GPIO_DAT_OUT3 0x19
#define REG_GPIO_INT_EN1 0x1A
#define REG_GPIO_INT_EN2 0x1B
#define REG_GPIO_INT_EN3 0x1C
#define REG_KP_GPIO1 0x1D
#define REG_KP_GPIO2 0x1E
#define REG_KP_GPIO3 0x1F
#define REG_GPI_EM1 0x20
#define REG_GPI_EM2 0x21
#define REG_GPI_EM3 0x22
#define REG_GPIO_DIR1 0x23
#define REG_GPIO_DIR2 0x24
#define REG_GPIO_DIR3 0x25
#define REG_GPIO_INT_LVL1 0x26
#define REG_GPIO_INT_LVL2 0x27
#define REG_GPIO_INT_LVL3 0x28
#define REG_DEBOUNCE_DIS1 0x29
#define REG_DEBOUNCE_DIS2 0x2A
#define REG_DEBOUNCE_DIS3 0x2B
#define REG_GPIO_PULL1 0x2C
#define REG_GPIO_PULL2 0x2D
#define REG_GPIO_PULL3 0x2E

/* TCA8418 bit definitions */
#define CFG_AI 0x80
#define CFG_GPI_E_CFG 0x40
#define CFG_OVR_FLOW_M 0x20
#define CFG_INT_CFG 0x10
#define CFG_OVR_FLOW_IEN 0x08
#define CFG_K_LCK_IEN 0x04
#define CFG_GPI_IEN 0x02
#define CFG_KE_IEN 0x01

#define INT_STAT_CAD_INT 0x10
#define INT_STAT_OVR_FLOW_INT 0x08
#define INT_STAT_K_LCK_INT 0x04
#define INT_STAT_GPI_INT 0x02
#define INT_STAT_K_INT 0x01

/* TCA8418 register masks */
#define KEY_LCK_EC_KEC 0x7
#define KEY_EVENT_CODE 0x7f
#define KEY_EVENT_VALUE 0x80

/* TCA8418 Rows and Columns */
#define ROW0 0x01
#define ROW1 0x02
#define ROW2 0x04
#define ROW3 0x08
#define ROW4 0x10
#define ROW5 0x20
#define ROW6 0x40
#define ROW7 0x80

#define COL0 0x0001
#define COL1 0x0002
#define COL2 0x0004
#define COL3 0x0008
#define COL4 0x0010
#define COL5 0x0020
#define COL6 0x0040
#define COL7 0x0080
#define COL8 0x0100
#define COL9 0x0200


bool odroid_keyboard_initialized = false;
SemaphoreHandle_t i2c_semaphore;
SemaphoreHandle_t i2c_mutex;
SemaphoreHandle_t keyboard_state_mutex;
//uint8_t keystate[8];
odroid_keyboardstate_t keyboard_state;

static bool i2c_init_flag = false;


static void i2c_init()
{
    if (i2c_init_flag) abort();

    int i2c_master_port = KEYBOARD_I2C_NUM;

    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = KEYBOARD_I2C_SDA_IO;
    conf.scl_io_num = KEYBOARD_I2C_SCL_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
    conf.master.clk_speed = KEYBOARD_I2C_FREQ_HZ;
    
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       RX_BUF_DISABLE,
                       TX_BUF_DISABLE, 0);

    i2c_init_flag = true;
}


static void write_byte(uint8_t reg, uint8_t value)
{
    i2c_cmd_handle_t cmd;
    int ret;
    uint8_t buttons;

    cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, TCA8418_ADDR << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, value, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    
    ret = i2c_master_cmd_begin(KEYBOARD_I2C_NUM, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK)
    {
        abort();
    }
}

static uint8_t read_byte(uint8_t reg)
{
    i2c_cmd_handle_t cmd;
    int ret;
    uint8_t result;


    cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, TCA8418_ADDR << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    
    ret = i2c_master_cmd_begin(KEYBOARD_I2C_NUM, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK)
    {
        abort();
    }


    cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, TCA8418_ADDR << 1 | I2C_MASTER_READ, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, &result, NACK_VAL);
    i2c_master_stop(cmd);

    ret = i2c_master_cmd_begin(KEYBOARD_I2C_NUM, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK)
    {
        abort();
    }


    return result;
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    portBASE_TYPE higher_awoken = pdFALSE;
    xSemaphoreGiveFromISR(i2c_semaphore, &higher_awoken);
    
    if (higher_awoken)
    {
        portYIELD_FROM_ISR();
    }
}

void i2c_reset()
{
#if 1
	// https://github.com/espressif/esp-idf/issues/922

	/*
	If the data line (SDA) is stuck LOW, the master should send nine clock pulses. The device
	that held the bus LOW should release it sometime within those nine clocks.
	*/

	gpio_config_t scl_conf = {0};
	scl_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	scl_conf.mode = GPIO_MODE_OUTPUT;
	scl_conf.pin_bit_mask = (1 << KEYBOARD_I2C_SCL_IO);
	scl_conf.pull_down_en = 0;
	scl_conf.pull_up_en = 0;

	gpio_config(&scl_conf);


	gpio_config_t sda_conf = {0};
	sda_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	sda_conf.mode = GPIO_MODE_INPUT;
	sda_conf.pin_bit_mask = (1 << KEYBOARD_I2C_SDA_IO);
	sda_conf.pull_down_en = 0;
	sda_conf.pull_up_en = 0;

	gpio_config(&sda_conf);

	if (!gpio_get_level(KEYBOARD_I2C_SDA_IO))
	{
		printf("%s: Recovering i2c bus.\n", __func__);
		for (int i = 0; i < 9; ++i)
		{
			gpio_set_level(KEYBOARD_I2C_SCL_IO, 1);
			vTaskDelay(5 / portTICK_PERIOD_MS);

			gpio_set_level(KEYBOARD_I2C_SCL_IO, 0);
			vTaskDelay(5 / portTICK_PERIOD_MS);
		}
	}
	else
	{
		printf("%s: Skipping i2c bus recovery.\n", __func__);
	}
#endif
}

void odroid_keyboard_state_key_set(odroid_keyboardstate_t* state, odroid_key_t key, odroid_keystate_t value)
{
    if (key > ODROID_KEY_NONE)
    {

        uint8_t temp = (key - 1);
        uint8_t row = (temp / 10) & 0x07;
        uint8_t col = (temp - (row * 10)) & 0x07;
        uint8_t bit = 1 << col;

        //printf("%s: col=%d, row=%d, bit=%#04x\n", __func__, col, row, bit);

        if (value)
        {
            state->rows[row] |= bit;
        }
        else
        {
            state->rows[row] &= ~bit;
        }
    }
}

odroid_keystate_t odroid_keyboard_state_key_get(odroid_keyboardstate_t* state, odroid_key_t key)
{
    odroid_keystate_t result;

    if (key > ODROID_KEY_NONE)
    {
        uint8_t temp = (key - 1);
        uint8_t row = (temp / 10) & 0x07;
        uint8_t col = (temp - (row * 10)) & 0x07;
        uint8_t bit = 1 << col;

        //printf("%s: col=%d, row=%d, bit=%#04x\n", __func__, col, row, bit);

        result = (state->rows[row] & bit) ? ODROID_KEY_PRESSED : ODROID_KEY_RELEASED;
    }
    else
    {
        result = ODROID_KEY_RELEASED;
    }

    return result;
}

odroid_keyboardstate_t odroid_keyboard_state_get()
{
    odroid_keyboardstate_t result;

    if (!xSemaphoreTake(keyboard_state_mutex, portMAX_DELAY)) abort();
    result = keyboard_state;
    xSemaphoreGive(keyboard_state_mutex);

    return result;
}


static void (*odroid_keyboard_event_fn)(odroid_keystate_t state, odroid_key_t key);

void odroid_keyboard_event_callback_set(void (*callback)(odroid_keystate_t, odroid_key_t))
{
    odroid_keyboard_event_fn = callback;
}

static void odroid_keyboard_task()
{
    printf("%s: Entered.\n", __func__);

    while(true)
    {
        if (xSemaphoreTake(i2c_semaphore, portMAX_DELAY) != pdTRUE)
        {
            abort();
        }
        
        if (!xSemaphoreTake(i2c_mutex, portMAX_DELAY)) abort();

        uint8_t int_stat = read_byte(REG_INT_STAT);
        uint8_t event_reg = read_byte(REG_KEY_LCK_EC);
        //printf("%s: Processing IRQ. int_stat=%#04x, event_reg=%d\n", __func__, int_stat, event_reg);

        uint8_t events = event_reg & 0x0f;
        while(events)
        {
            uint8_t scan = read_byte(REG_KEY_EVENT_A);
            uint8_t key = scan & 0x7f;
            odroid_keystate_t isPressed = scan & 0x80 ? ODROID_KEY_PRESSED : ODROID_KEY_RELEASED;

            printf("%s: key=%#04x isPressed=%d\n", __func__, key, isPressed);

            if (!xSemaphoreTake(keyboard_state_mutex, portMAX_DELAY)) abort();
            odroid_keyboard_state_key_set(&keyboard_state, (odroid_key_t)key, isPressed);
            xSemaphoreGive(keyboard_state_mutex);

            if (odroid_keyboard_event_fn)
            {
                odroid_keyboard_event_fn(isPressed, (odroid_key_t)key);
            }
            --events;
        }

        // clear interrupt
        write_byte(REG_INT_STAT, 0x1f);

        xSemaphoreGive(i2c_mutex);
    }

    printf("%s: Exited.\n", __func__);
}


void odroid_keyboard_init()
{
    i2c_semaphore = xSemaphoreCreateBinary();
    if (!i2c_semaphore) abort();

    i2c_mutex = xSemaphoreCreateMutex();
    if (!i2c_mutex) abort();

    keyboard_state_mutex = xSemaphoreCreateMutex();
    if (!keyboard_state_mutex) abort();


    i2c_reset();


    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << KEYBOARD_INT);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;

    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(KEYBOARD_INT, gpio_isr_handler, (void*)KEYBOARD_INT);


    i2c_init();

    
    write_byte(REG_CFG, CFG_INT_CFG | CFG_KE_IEN);
    write_byte(REG_INT_STAT, 0x1f); // clear all irqs

    write_byte(REG_KP_GPIO1, 0x7f); // ROW0-7
    write_byte(REG_KP_GPIO2, 0xff); // COL0-7
    write_byte(REG_KP_GPIO3, 0x00); // COL8-9

    write_byte(REG_GPIO_DIR1, 0x80); // ROW0-7
    write_byte(REG_GPIO_DIR3, 0x03); // COL8-9



    odroid_keyboard_initialized = true;
    xTaskCreatePinnedToCore(&odroid_keyboard_task, "keyboard_task", 2048, NULL, 5, NULL, 1);
}

odroid_keyboard_led_t odroid_keyboard_leds_get()
{
    if (!xSemaphoreTake(i2c_mutex, portMAX_DELAY)) abort();

    uint8_t result = read_byte(REG_GPIO_DAT_OUT3);
    result |= (read_byte(REG_GPIO_DAT_OUT1) & 0x80) >> 5;

    xSemaphoreGive(i2c_mutex);

    return (odroid_keyboard_led_t)result;
}

void odroid_keyboard_leds_set(odroid_keyboard_led_t value)
{
    if (!xSemaphoreTake(i2c_mutex, portMAX_DELAY)) abort();

    write_byte(REG_GPIO_DAT_OUT3, value & 0x03);
    write_byte(REG_GPIO_DAT_OUT1, (value & ODROID_KEYBOARD_LED_St) ? 0x80 : 0x00);

    xSemaphoreGive(i2c_mutex);
}