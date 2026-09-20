#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_GPIO_RED GPIO_NUM_2
#define LED_GPIO_GREEN GPIO_NUM_18
#define LED_GPIO_BLUE GPIO_NUM_22

void app_main(void)
{
    // Configure GPIO2 as an output
    gpio_set_direction(LED_GPIO_RED, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_GPIO_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_GPIO_BLUE, GPIO_MODE_OUTPUT);

    while (1)
    {
        // Turn RED LED ON
        gpio_set_level(LED_GPIO_RED, 1);

        // Wait 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn RED LED OFF
        gpio_set_level(LED_GPIO_RED, 0);

        // Turn BLUE LED ON
        gpio_set_level(LED_GPIO_BLUE, 1);

        // Wait 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn BLUE LED OFF
        gpio_set_level(LED_GPIO_BLUE, 0);

        // Turn GREEN LED ON
        gpio_set_level(LED_GPIO_GREEN, 1);

        // Wait 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn GREEN LED OFF
        gpio_set_level(LED_GPIO_GREEN, 0);

        // Wait 1 second
        // vTaskDelay(pdMS_TO_TICKS(1000));
    }
}