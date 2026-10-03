#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

// --- Configuration ---
#define WIFI_SSID     "<WIFI_SSID>>"
#define WIFI_PASS     "<WIFI_PASSWORD>"

#define GREEN_GPIO    GPIO_NUM_25
#define RED_GPIO      GPIO_NUM_27
#define BLUE_GPIO     GPIO_NUM_32

#define COMMON_ANODE  false // Set 'true' for Common Anode (+), 'false' for Common Cathode (-)

static const char *TAG = "ESP32_GPIO_RGB";

// Function to set digital output states (HIGH or LOW)
void set_rgb_digital(bool red_on, bool green_on, bool blue_on) {
    bool red_level   = red_on;
    bool green_level = green_on;
    bool blue_level  = blue_on;

    if (COMMON_ANODE) {
        red_level   = !red_level;
        green_level = !green_level;
        blue_level  = !blue_level;
    }

    ESP_LOGI(TAG, "set_rgb_digital: state(R=%d, G=%d, B=%d) [COMMON_ANODE=%d] -> Output pins: RED(GPIO%d)=%d, GREEN(GPIO%d)=%d, BLUE(GPIO%d)=%d",
             red_on, green_on, blue_on, COMMON_ANODE,
             RED_GPIO, red_level, GREEN_GPIO, green_level, BLUE_GPIO, blue_level);

    gpio_set_level(RED_GPIO,   red_level   ? 1 : 0);
    gpio_set_level(GREEN_GPIO, green_level ? 1 : 0);
    gpio_set_level(BLUE_GPIO,  blue_level  ? 1 : 0);
}

// Embedded web assets (linked via EMBED_TXTFILES)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");

extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

// HTTP GET handler for root path "/"
static esp_err_t root_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "==> HTTP GET %s [Client IP/Socket Request]", req->uri);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    // Use HTTPD_RESP_USE_STRLEN so the null terminator added by EMBED_TXTFILES is excluded
    httpd_resp_send(req, (const char *)index_html_start, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "<== Served index.html successfully");
    return ESP_OK;
}

// HTTP GET handler for "/style.css"
static esp_err_t style_css_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "==> HTTP GET /style.css");
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_send(req, (const char *)style_css_start, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "<== Served style.css successfully");
    return ESP_OK;
}

// HTTP GET handler for "/app.js"
static esp_err_t app_js_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "==> HTTP GET /app.js");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_send(req, (const char *)app_js_start, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "<== Served app.js successfully");
    return ESP_OK;
}

// HTTP GET handler for "/setgpio?r=1&g=0&b=0"
static esp_err_t setgpio_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "==> HTTP GET %s", req->uri);

    char buf[128] = {0};
    esp_err_t err = httpd_req_get_url_query_str(req, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get URL query string: %s", esp_err_to_name(err));
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Query string: '%s'", buf);

    char r_val[8] = {0}, g_val[8] = {0}, b_val[8] = {0};
    esp_err_t err_r = httpd_query_key_value(buf, "r", r_val, sizeof(r_val));
    esp_err_t err_g = httpd_query_key_value(buf, "g", g_val, sizeof(g_val));
    esp_err_t err_b = httpd_query_key_value(buf, "b", b_val, sizeof(b_val));

    if (err_r == ESP_OK && err_g == ESP_OK && err_b == ESP_OK) {
        bool r = (atoi(r_val) > 0);
        bool g = (atoi(g_val) > 0);
        bool b = (atoi(b_val) > 0);

        ESP_LOGI(TAG, "Parsed values -> R: %d ('%s'), G: %d ('%s'), B: %d ('%s')",
                 r, r_val, g, g_val, b, b_val);

        set_rgb_digital(r, g, b);

        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_sendstr(req, "OK");
        ESP_LOGI(TAG, "<== /setgpio executed and responded 200 OK");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Parameter parsing failed: r=%s (%s), g=%s (%s), b=%s (%s)",
                 r_val, esp_err_to_name(err_r),
                 g_val, esp_err_to_name(err_g),
                 b_val, esp_err_to_name(err_b));
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
}

// Web server setup
static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true; // Automatically close old idle sockets on new connections

    ESP_LOGI(TAG, "Starting HTTP server on port %d...", config.server_port);
    esp_err_t ret = httpd_start(&server, &config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Server started. Registering URI handlers...");

        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t index_uri = { .uri = "/index.html", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t style_uri = { .uri = "/style.css", .method = HTTP_GET, .handler = style_css_get_handler };
        httpd_register_uri_handler(server, &style_uri);

        httpd_uri_t app_js_uri = { .uri = "/app.js", .method = HTTP_GET, .handler = app_js_get_handler };
        httpd_register_uri_handler(server, &app_js_uri);

        httpd_uri_t setgpio_uri = { .uri = "/setgpio", .method = HTTP_GET, .handler = setgpio_get_handler };
        httpd_register_uri_handler(server, &setgpio_uri);

        ESP_LOGI(TAG, "All URI handlers registered successfully (/ , /index.html, /style.css, /app.js, /setgpio)");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server! Error: %s", esp_err_to_name(ret));
    }
    return server;
}

// Initialize GPIOs
static void init_gpios(void) {
    ESP_LOGI(TAG, "Configuring GPIO outputs: Red=%d, Green=%d, Blue=%d", RED_GPIO, GREEN_GPIO, BLUE_GPIO);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RED_GPIO) | (1ULL << GREEN_GPIO) | (1ULL << BLUE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&io_conf);
    ESP_LOGI(TAG, "gpio_config result: %s", esp_err_to_name(err));
}

// Wi-Fi Event Handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi station started. Connecting to SSID: '%s'...", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi Connected! IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver();
    }
}

// Wi-Fi Initialization
static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void) {
    ESP_LOGI(TAG, "=============================================");
    ESP_LOGI(TAG, "       ESP32 RGB LED Controller Started      ");
    ESP_LOGI(TAG, "=============================================");
    ESP_ERROR_CHECK(nvs_flash_init());
    init_gpios();
    set_rgb_digital(false, false, false); // Start OFF
    wifi_init_sta();
}