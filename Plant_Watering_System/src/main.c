#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"

#define PUMP_CONTROL_PIN    GPIO_NUM_25
#define BUTTON_PIN          GPIO_NUM_32
#define DEBOUNCE_TIME_MS    50

static const char *TAG = "PUMP";

// FreeRTOS task handle for the button worker task
static TaskHandle_t button_task_handle = NULL;

// Current state of the pump (0: OFF, 1: ON)
static int pump_state = 0;

/**
 * @brief Interrupt Service Routine (ISR) triggered on button press (falling edge).
 *        Runs in ISR context. Disables the GPIO interrupt to prevent bouncing storms
 *        and wakes up the worker task.
 */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(uint32_t)arg;

    // Temporarily disable interrupt on this pin to eliminate contact-bounce storms
    gpio_intr_disable(pin);

    // Notify the worker task to process the press event
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief Worker task that processes button presses in task context.
 *        Sleeps with 0% CPU consumption until an interrupt arrives.
 */
static void button_task(void *pvParameters)
{
    while (1) {
        // Block indefinitely until notified by the ISR (consumes 0% CPU while waiting)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Debounce delay to let physical switch contacts settle
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));

        // Verify the pin is still LOW (genuine human press)
        if (gpio_get_level(BUTTON_PIN) == 0) {
            pump_state = !pump_state;
            gpio_set_level(PUMP_CONTROL_PIN, pump_state);
            ESP_LOGI(TAG, "Button pressed (Interrupt)! Pump is now %s", pump_state ? "ON" : "OFF");

            // Wait until button is released to prevent re-triggering while held
            while (gpio_get_level(BUTTON_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            // Release debounce
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
        }

        // Clear any spurious notifications that arrived during the debounce period
        ulTaskNotifyTake(pdTRUE, 0);

        // Re-enable GPIO interrupt for the next button press
        gpio_intr_enable(BUTTON_PIN);
    }
}

void app_main(void)
{
    // Configure pump control pin as output
    gpio_reset_pin(PUMP_CONTROL_PIN);
    gpio_set_direction(PUMP_CONTROL_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PUMP_CONTROL_PIN, pump_state);

    // Configure Button pin as input with internal pull-up (Active-LOW)
    gpio_reset_pin(BUTTON_PIN);
    gpio_pullup_en(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);

    // Configure interrupt trigger on falling edge (Active-LOW press: 1 -> 0)
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_NEGEDGE);

    ESP_LOGI(TAG, "ESP32 Pump Watering system initialized.");
    ESP_LOGI(TAG, "Pump Control Pin: GPIO %d | Button Pin: GPIO %d", PUMP_CONTROL_PIN, BUTTON_PIN);
    ESP_LOGI(TAG, "Initial Pump State: OFF");

    // Create the button worker task (blocks until interrupt notifies it)
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, &button_task_handle);

    // Install GPIO ISR service and attach handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, (void *)(uint32_t)BUTTON_PIN);

    // app_main exits; button_task continues running purely event-driven
}