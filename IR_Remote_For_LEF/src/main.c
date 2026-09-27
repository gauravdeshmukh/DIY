#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "IR_RGB_CTRL";

// --- Pin Definitions ---
#define IR_RX_GPIO          GPIO_NUM_19
#define RGB_RED_GPIO        GPIO_NUM_25
#define RGB_GREEN_GPIO      GPIO_NUM_26
#define RGB_BLUE_GPIO       GPIO_NUM_27

// --- NVS Storage Constants ---
#define NVS_NAMESPACE       "ir_codes"
#define KEY_RED             "code_red"
#define KEY_GREEN           "code_green"
#define KEY_BLUE            "code_blue"

// --- RMT Symbol Constants ---
#define RMT_RESOLUTION_HZ   1000000 // 1 tick = 1 microsecond
#define MARGIN_US           200     // Tolerance margin in microseconds

// --- Button Storage ---
typedef enum {
    CMD_RED = 0,
    CMD_GREEN,
    CMD_BLUE,
    CMD_COUNT
} rgb_cmd_t;

static uint32_t captured_nec_codes[CMD_COUNT] = {0};
static const char *cmd_names[CMD_COUNT] = {"RED", "GREEN", "BLUE"};
static const char *nvs_keys[CMD_COUNT]  = {KEY_RED, KEY_GREEN, KEY_BLUE};

// Active state tracker
static gpio_num_t current_active_led = GPIO_NUM_NC;

// RMT Event Structure
typedef struct {
    rmt_rx_done_event_data_t rx_data;
} rmt_rx_event_t;

static QueueHandle_t rmt_rx_queue = NULL;

// --- Function Declarations ---
static bool parse_nec_frame(const rmt_symbol_word_t *symbols, size_t num_symbols, uint32_t *out_nec_code);
static void print_nec_as_pronto_hex(uint32_t nec_code);
static bool load_codes_from_nvs(void);
static void save_codes_to_nvs(void);
static void set_exclusive_led(gpio_num_t target_pin);

// ============================================================================
// RMT RX Callback (ISR Context)
// ============================================================================
static bool rmt_rx_done_cb(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data) {
    BaseType_t high_task_wakeup = pdFALSE;
    rmt_rx_event_t evt = {
        .rx_data = *edata
    };
    xQueueSendFromISR(rmt_rx_queue, &evt, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

// ============================================================================
// Exclusive + Toggle LED Switching Helper
// ============================================================================
static void set_exclusive_led(gpio_num_t target_pin) {
    // Case 1: Same button pressed while LED is ON -> Toggle OFF
    if (current_active_led == target_pin && target_pin != GPIO_NUM_NC) {
        gpio_set_level(RGB_RED_GPIO, 0);
        gpio_set_level(RGB_GREEN_GPIO, 0);
        gpio_set_level(RGB_BLUE_GPIO, 0);
        current_active_led = GPIO_NUM_NC;
        ESP_LOGI(TAG, "Action: Toggled LED OFF");
        return;
    }

    // Case 2: Turn off all LEDs first
    gpio_set_level(RGB_RED_GPIO, 0);
    gpio_set_level(RGB_GREEN_GPIO, 0);
    gpio_set_level(RGB_BLUE_GPIO, 0);

    // Case 3: Turn on target LED
    if (target_pin != GPIO_NUM_NC) {
        gpio_set_level(target_pin, 1);
        current_active_led = target_pin;
    } else {
        current_active_led = GPIO_NUM_NC;
    }
}

// ============================================================================
// NVS Storage Handlers
// ============================================================================
static bool load_codes_from_nvs(void) {
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_h);
    if (err != ESP_OK) return false;

    bool all_found = true;
    for (int i = 0; i < CMD_COUNT; i++) {
        uint32_t code = 0;
        err = nvs_get_u32(nvs_h, nvs_keys[i], &code);
        if (err == ESP_OK && code != 0) {
            captured_nec_codes[i] = code;
            ESP_LOGI(TAG, "Loaded [%s] from NVS -> 0x%08" PRIX32, cmd_names[i], code);
        } else {
            all_found = false;
            break;
        }
    }

    nvs_close(nvs_h);
    return all_found;
}

static void save_codes_to_nvs(void) {
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) return;

    for (int i = 0; i < CMD_COUNT; i++) {
        nvs_set_u32(nvs_h, nvs_keys[i], captured_nec_codes[i]);
    }

    nvs_commit(nvs_h);
    nvs_close(nvs_h);
    ESP_LOGI(TAG, "Successfully saved all 3 remote codes to NVS!");
}

// ============================================================================
// NEC Protocol Parser
// ============================================================================
static bool is_pulse_in_range(uint32_t duration, uint32_t expected) {
    return (duration >= (expected - MARGIN_US)) && (duration <= (expected + MARGIN_US));
}

static bool parse_nec_frame(const rmt_symbol_word_t *symbols, size_t num_symbols, uint32_t *out_nec_code) {
    if (num_symbols < 33) return false;

    if (!is_pulse_in_range(symbols[0].duration0, 9000) || !is_pulse_in_range(symbols[0].duration1, 4500)) {
        return false;
    }

    uint32_t raw_data = 0;
    for (int i = 0; i < 32; i++) {
        const rmt_symbol_word_t *sym = &symbols[i + 1];

        if (!is_pulse_in_range(sym->duration0, 560)) return false;

        if (is_pulse_in_range(sym->duration1, 1690)) {
            raw_data |= (1UL << i);
        } else if (!is_pulse_in_range(sym->duration1, 560)) {
            return false;
        }
    }

    uint8_t command     = (raw_data >> 16) & 0xFF;
    uint8_t command_bar = (raw_data >> 24) & 0xFF;

    if ((command ^ command_bar) != 0xFF) return false;

    *out_nec_code = raw_data;
    return true;
}

static void print_nec_as_pronto_hex(uint32_t nec_code) {
    printf("\n--- Pronto Hex Output ---\n");
    printf("0000 006D 0022 0000 0157 00AC ");
    for (int i = 0; i < 32; i++) {
        if ((nec_code >> i) & 0x01) {
            printf("0015 0041 ");
        } else {
            printf("0015 0015 ");
        }
    }
    printf("0015 0E6C\n-------------------------\n\n");
}

// ============================================================================
// Main Application
// ============================================================================
void app_main(void) {
    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RGB_RED_GPIO) | (1ULL << RGB_GREEN_GPIO) | (1ULL << RGB_BLUE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    set_exclusive_led(GPIO_NUM_NC);

    // 3. Create FreeRTOS Queue for RMT RX Events
    rmt_rx_queue = xQueueCreate(10, sizeof(rmt_rx_event_t));

    // 4. Configure RMT Receiver
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .gpio_num = IR_RX_GPIO,
    };
    rmt_channel_handle_t rx_channel = NULL;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &rx_channel));

    // Register Callbacks
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_cb,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, NULL));

    // Enable RMT channel
    ESP_ERROR_CHECK(rmt_enable(rx_channel));

    // Allocate buffer for RX symbols
    rmt_symbol_word_t raw_symbols[64];
    rmt_receive_config_t receive_cfg = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };

    // 5. Load or Learn
    bool has_saved_codes = load_codes_from_nvs();

    if (!has_saved_codes) {
        ESP_LOGI(TAG, "=== Starting IR Remote Setup Mode ===");

        for (int i = 0; i < CMD_COUNT; i++) {
            ESP_LOGI(TAG, "Press remote button for: [%s]", cmd_names[i]);

            while (1) {
                ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_cfg));

                rmt_rx_event_t evt;
                if (xQueueReceive(rmt_rx_queue, &evt, portMAX_DELAY) == pdTRUE) {
                    uint32_t decoded_code = 0;
                    if (parse_nec_frame(evt.rx_data.received_symbols, evt.rx_data.num_symbols, &decoded_code)) {
                        captured_nec_codes[i] = decoded_code;
                        ESP_LOGI(TAG, "Captured [%s] -> NEC Code: 0x%08" PRIX32, cmd_names[i], decoded_code);
                        print_nec_as_pronto_hex(decoded_code);

                        gpio_num_t target_pin = (i == CMD_RED) ? RGB_RED_GPIO : (i == CMD_GREEN) ? RGB_GREEN_GPIO : RGB_BLUE_GPIO;
                        
                        // Briefly blink selected LED for feedback during setup
                        gpio_set_level(target_pin, 1);
                        vTaskDelay(pdMS_TO_TICKS(300));
                        gpio_set_level(target_pin, 0);
                        break;
                    }
                }
            }
        }
        save_codes_to_nvs();
    } else {
        ESP_LOGI(TAG, "=== Valid Remote Codes Restored from NVS ===");
        for (int i = 0; i < CMD_COUNT; i++) {
            print_nec_as_pronto_hex(captured_nec_codes[i]);
        }
    }

    ESP_LOGI(TAG, "=== Entering Control Mode ===");

    // 6. Runtime Control Loop (With Toggle Logic)
    while (1) {
        ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_cfg));

        rmt_rx_event_t evt;
        if (xQueueReceive(rmt_rx_queue, &evt, portMAX_DELAY) == pdTRUE) {
            uint32_t incoming_code = 0;

            if (parse_nec_frame(evt.rx_data.received_symbols, evt.rx_data.num_symbols, &incoming_code)) {
                ESP_LOGI(TAG, "Received Code: 0x%08" PRIX32, incoming_code);

                if (incoming_code == captured_nec_codes[CMD_RED]) {
                    set_exclusive_led(RGB_RED_GPIO);
                } else if (incoming_code == captured_nec_codes[CMD_GREEN]) {
                    set_exclusive_led(RGB_GREEN_GPIO);
                } else if (incoming_code == captured_nec_codes[CMD_BLUE]) {
                    set_exclusive_led(RGB_BLUE_GPIO);
                } else {
                    ESP_LOGW(TAG, "Unrecognized remote command.");
                }
            }
        }
    }
}