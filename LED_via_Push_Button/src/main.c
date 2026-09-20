#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_PIN             GPIO_NUM_25
#define BUTTON_PIN          GPIO_NUM_32
#define DEBOUNCE_TIME_MS    50

static const char *TAG = "BUTTON_LED";

/**
 * @brief Checks if a button (active-LOW) was pressed.
 *        Handles software debouncing and waits for release.
 *
 * @param pin The GPIO pin connected to the button.
 * @return true if a valid button press occurred, false otherwise.
 */
bool is_button_pressed(gpio_num_t pin)
{
    if (gpio_get_level(pin) == 0) {
        // Debounce delay
        ESP_LOGI(TAG, "ESP32 waiting on debounce.");
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));

        // Confirm button is still pressed
        if (gpio_get_level(pin) == 0) {
            ESP_LOGI(TAG, "ESP32 again got 0");
            // Wait for release to prevent repeated triggers
            while (gpio_get_level(pin) == 0) {
                ESP_LOGI(TAG, "ESP32 waiting inside while loop");
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            // Debounce on release
            ESP_LOGI(TAG, "ESP32 waiting outside while loop.");
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
            return true;
        }
    }
    return false;
}

void app_main(void)
{
    // Configure LED pin as output
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    // Configure Button pin as input (gpio_reset_pin already enables internal pull-up)
    gpio_reset_pin(BUTTON_PIN);
    gpio_pullup_en(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);

    // Initial LED state: OFF
    int led_state = 0;
    gpio_set_level(LED_PIN, led_state);

    ESP_LOGI(TAG, "ESP32 Button-controlled LED initialized.");
    ESP_LOGI(TAG, "LED Pin: GPIO %d | Button Pin: GPIO %d", LED_PIN, BUTTON_PIN);
    ESP_LOGI(TAG, "Initial LED State: OFF");

    while (1) {
        if (is_button_pressed(BUTTON_PIN)) {
            led_state = !led_state;
            gpio_set_level(LED_PIN, led_state);
            ESP_LOGI(TAG, "Button pressed! LED is now %s", led_state ? "ON" : "OFF");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}