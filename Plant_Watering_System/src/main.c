#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"

#define PUMP_GPIO             GPIO_NUM_25
#define PUMP_RUN_TIME_MS      10000                     // 10 seconds
#define SLEEP_TIME_US         (50ULL * 1000000ULL)      // 50 seconds in microseconds

static const char *TAG = "PUMP_CONTROLLER";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Woke up!");

    // 1. Reset and configure GPIO 25 as output
    gpio_reset_pin(PUMP_GPIO);
    gpio_set_direction(PUMP_GPIO, GPIO_MODE_OUTPUT);

    // 2. Turn Pump ON
    gpio_set_level(PUMP_GPIO, 1);
    ESP_LOGI(TAG, "Pump turned ON. Running for 10 seconds...");

    // 3. Keep pump running for 10 seconds
    vTaskDelay(pdMS_TO_TICKS(PUMP_RUN_TIME_MS));

    // 4. Turn Pump OFF
    gpio_set_level(PUMP_GPIO, 0);
    ESP_LOGI(TAG, "Pump turned OFF.");

    // 5. Enable Timer Wakeup
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_US);
    ESP_LOGI(TAG, "Entering Deep Sleep for 50 seconds...");

    // 6. Enter Deep Sleep
    esp_deep_sleep_start();
}