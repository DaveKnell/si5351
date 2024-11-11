#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "si5351.h"

#include "driver/i2c_master.h"

#define I2C_MASTER_SCL (12)
#define I2C_MASTER_SDA (13)

static const char *TAG = "SI5351TEST";

#define BLINK_GPIO CONFIG_BLINK_GPIO

static i2c_master_bus_handle_t i2c_bus_handle;
static i2c_master_dev_handle_t i2c_si5351_handle;

static void i2c_init(void)
{
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = I2C_MASTER_SCL,
        .sda_io_num = I2C_MASTER_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle));
    ESP_LOGI(TAG, "I2C initialised.");

    // Check SI5351
    i2c_device_config_t si5351_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x60,
        .scl_speed_hz = 100000,
    };

    ESP_ERROR_CHECK(i2c_master_probe(i2c_bus_handle, si5351_cfg.device_address, -1));
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &si5351_cfg, &i2c_si5351_handle));
    ESP_LOGI(TAG, "I2C SI5351 initialised.");
}

int si5351_write(void *dev, uint8_t reg, uint8_t val)
{
    uint8_t data[2] = {reg, val};
    ESP_LOGI(TAG, "Write: %02x %02x", reg, val);
    i2c_master_transmit(dev, data, 2, -1);
    return 0;
}

void si5351_log(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 256, fmt, args);
    va_end(args);
    ESP_LOGI("SI5351", "%s", buf);
}

void app_main(void)
{
    uint8_t s_led_state = 0;

    // Setup LEDs
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    i2c_init();

    si5351_t si5351;
    si5351_init(&si5351, i2c_si5351_handle, SI5351_CRYSTAL_FREQ_27MHZ, SI5351_CRYSTAL_LOAD_8PF, &si5351_write, &si5351_log);
    si5351_set(&si5351, 0, SI5351_PLL_A, 2000000, 0, false, true);
    si5351_set(&si5351, 1, SI5351_PLL_A, 2000000, 0, true, false);

    while (1) {
        // Blink LED
        gpio_set_level(BLINK_GPIO, s_led_state);
        s_led_state = !s_led_state;
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}
