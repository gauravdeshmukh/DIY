#include <stdio.h>
#include <stdbool.h>
#include "esp_attr.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

#define IR_RX_GPIO     GPIO_NUM_15
#define IR_TX_GPIO     GPIO_NUM_18
#define BUTTON_GPIO    GPIO_NUM_4
#define MAX_IR_SYMBOLS 128
#define DEBOUNCE_TIME_MS 50

static const char *TAG = "IR_LEARN";

// --- Single Storage Slot in RAM ---
typedef struct {
    rmt_symbol_word_t symbols[MAX_IR_SYMBOLS];
    size_t num_symbols;
    bool is_valid;
} ir_signal_slot_t;

static ir_signal_slot_t stored_signal = {
    .num_symbols = 0,
    .is_valid = false
};

// Queue handle to pass symbols from ISR callback to main task
static QueueHandle_t rx_queue = NULL;

typedef struct {
    rmt_symbol_word_t symbols[MAX_IR_SYMBOLS];
    size_t num_symbols;
} ir_rx_data_t;

// FreeRTOS task handle for the button worker task
static TaskHandle_t button_task_handle = NULL;

rmt_channel_handle_t rx_channel = NULL;
rmt_channel_handle_t tx_channel = NULL;
rmt_encoder_handle_t copy_encoder = NULL;
rmt_transmit_config_t transmit_cfg = {
    .loop_count = 0,
};

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
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            ESP_LOGI(TAG, "Button pressed (Interrupt)! Transmitting signal");

            if (!stored_signal.is_valid) {
                ESP_LOGE(TAG, "Cannot transmit: No IR signal learned yet!");
            } else {
                ESP_LOGI(TAG, "Replaying stored IR signal (%d pulses)...", stored_signal.num_symbols);

                // Temporarily disable RX to prevent local optical feedback
                // rmt_disable(rx_channel);

                // Transmit raw symbols out GPIO 18
                // ESP_ERROR_CHECK(rmt_transmit(
                //     tx_channel,
                //     copy_encoder,
                //     stored_signal.symbols,
                //     sizeof(rmt_symbol_word_t) * stored_signal.num_symbols,
                //     &transmit_cfg
                // ));

                // Create a temporary transmission buffer so we don't destroy raw stored data
                rmt_symbol_word_t tx_symbols[MAX_IR_SYMBOLS];
                memcpy(tx_symbols, stored_signal.symbols, sizeof(rmt_symbol_word_t) * stored_signal.num_symbols);

                // TSOP receiver outputs LOW on active light. 
                // Convert LOW levels to HIGH so the RMT Carrier Modulator fires correctly.
                for (size_t i = 0; i < stored_signal.num_symbols; i++) {
                    tx_symbols[i].level0 = !tx_symbols[i].level0;
                    tx_symbols[i].level1 = !tx_symbols[i].level1;
                }

                // Transmit the inverted levels
                ESP_ERROR_CHECK(rmt_transmit(
                    tx_channel,
                    copy_encoder,
                    tx_symbols,
                    sizeof(rmt_symbol_word_t) * stored_signal.num_symbols,
                    &transmit_cfg
                ));

                // Wait until hardware finishes sending pulses
                ESP_ERROR_CHECK(rmt_tx_wait_all_done(tx_channel, -1));

                vTaskDelay(pdMS_TO_TICKS(100)); // Small optical settling delay

                // Re-enable and re-arm RX channel
                // rmt_enable(rx_channel);
                // ESP_ERROR_CHECK(rmt_receive(rx_channel, rx_buffer, sizeof(rx_buffer), &receive_cfg));

                ESP_LOGI(TAG, "Transmission complete!");
            }

            // Wait until button is released to prevent re-triggering while held
            while (gpio_get_level(BUTTON_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            // Release debounce
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME_MS));
        }

        // Clear any spurious notifications that arrived during the debounce period
        ulTaskNotifyTake(pdTRUE, 0);

        // Re-enable GPIO interrupt for the next button press
        gpio_intr_enable(BUTTON_GPIO);
    }
}

// Callback triggered in ISR when an IR frame finishes or times out
static bool rmt_rx_done_callback(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup = pdFALSE;
    ir_rx_data_t rx_data;
    
    rx_data.num_symbols = edata->num_symbols;
    if (rx_data.num_symbols > 0) {
        size_t copy_size = (rx_data.num_symbols > MAX_IR_SYMBOLS) ? MAX_IR_SYMBOLS : rx_data.num_symbols;
        for (size_t i = 0; i < copy_size; i++) {
            rx_data.symbols[i] = edata->received_symbols[i];
        }
        xQueueSendFromISR(rx_queue, &rx_data, &high_task_wakeup);
    }
    
    return high_task_wakeup == pdTRUE;
}

// --- Initialize Status LED and Pushbutton GPIOs ---
static void init_gpios(void)
{
    // Pushbutton Configuration (with internal pull-up)
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&btn_cfg);
}

void app_main(void)
{
    // Create the button worker task (blocks until interrupt notifies it)
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, &button_task_handle);
    init_gpios();
    rx_queue = xQueueCreate(5, sizeof(ir_rx_data_t));

    ESP_LOGI(TAG, "Configuring RMT RX Driver on GPIO %d...", IR_RX_GPIO);

    rmt_rx_channel_config_t rx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_RX_GPIO,
        .resolution_hz = 1000000, // 1us tick resolution
        .mem_block_symbols = MAX_IR_SYMBOLS,
    };

    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_cfg, &rx_channel));

    // Register callback for transaction completion
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, NULL));

    ESP_ERROR_CHECK(rmt_enable(rx_channel));

    static rmt_symbol_word_t raw_symbols[MAX_IR_SYMBOLS];
    rmt_receive_config_t receive_cfg = {
        .signal_range_min_ns = 1250,     // Reject glitches shorter than 1.25us
        .signal_range_max_ns = 12000000, // Frame timeout (~12ms idle)
    };

    // Arm the first reception
    ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_cfg));
    ESP_LOGI(TAG, "RMT RX Channel Enabled & Armed. Point remote and press button...");

    // -------------------------------------------------------------------------
    // 2. Configure RMT TX Channel (GPIO 18) with 38kHz Carrier
    // -------------------------------------------------------------------------
    rmt_tx_channel_config_t tx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_TX_GPIO,
        .mem_block_symbols = MAX_IR_SYMBOLS,
        .resolution_hz = 1000000, // 1us per tick
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_cfg, &tx_channel));

    // Apply 38 kHz modulation to the TX channel
    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33,
        .flags.polarity_active_low = false, // MUST BE FALSE for NPN Low-Side Driver
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(tx_channel, &carrier_cfg));
    ESP_ERROR_CHECK(rmt_enable(tx_channel));

    // Prepare Copy Encoder for raw symbol transmission
    rmt_copy_encoder_config_t copy_encoder_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_cfg, &copy_encoder));

    // Install GPIO ISR service and attach handler
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, (void *)(uint32_t)BUTTON_GPIO);

    ESP_LOGI(TAG, "System Ready!");
    ESP_LOGI(TAG, " -> Press any TV remote button to store signal into slot");
    ESP_LOGI(TAG, " -> Press GPIO 4 push button to replay stored signal");

    ir_rx_data_t rx_data;

    while (1) {
        // Block until the ISR receives a complete IR pulse frame
        if (xQueueReceive(rx_queue, &rx_data, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "--------------------------------------------------");
            ESP_LOGI(TAG, "Signal captured! Pulse count: %d", rx_data.num_symbols);

            // Overwrite existing slot
            memcpy(stored_signal.symbols, rx_data.symbols, sizeof(rmt_symbol_word_t) * rx_data.num_symbols);
            stored_signal.num_symbols = rx_data.num_symbols;
            stored_signal.is_valid = true;
            
            for (size_t i = 0; i < rx_data.num_symbols; i++) {
                printf("{.duration0 = %d, .level0 = %d, .duration1 = %d, .level1 = %d}, ",
                       rx_data.symbols[i].duration0, rx_data.symbols[i].level0,
                       rx_data.symbols[i].duration1, rx_data.symbols[i].level1);
            }
            printf("\n");

            // Re-arm the receiver for the next frame
            ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_cfg));
        }
    }
}