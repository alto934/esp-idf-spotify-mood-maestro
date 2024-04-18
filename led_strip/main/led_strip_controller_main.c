#include <string.h>
#include <math.h>
#include <ctype.h>  // Added for isxdigit() function
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM      0

#define EXAMPLE_LED_NUMBERS         60
#define FADE_IN_DURATION_MS        2000 // 1 second for fade in
#define FADE_OUT_DURATION_MS       2000 // 1 second for fade out
#define MOOD_COLOR_CHANGE_MS       500 // 1 second between mood color changes
#define MIN_BRIGHTNESS_PERCENT     20   // Minimum brightness percentage during fade-out

#define MAX_ESPNOW_MSG_SIZE 250

static const char *TAG = "example";
static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];

// Mood color definitions
typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} mood_color_t;

static const mood_color_t mood_colors[] = {
    {148, 0, 211},     // Graduation Purple
    {224, 255, 255},       // light cyan
    {0, 0, 255},       // dark blue
    {255, 69, 0},       // dark orange
    {255, 0, 0},      // red
    {0, 146, 0},     // Shrek Green
    {252, 3, 148}       // Pink
};

static int current_mood_color_index = 0;
static int8_t fade_direction = 1; // 1 for fade in, -1 for fade out
static float fade_value = 0.0f;
static uint8_t received_uid[4] = {0}; // Initialize with zeros
static bool new_uid_received = false;

static float linear_fade(float x) {
    return x;
}



static void led_strip_fade_task(void *arg)
{
    rmt_channel_handle_t led_chan = (rmt_channel_handle_t)arg;
    rmt_encoder_handle_t led_encoder = NULL;
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    // Initialize the LED strip with all pixels off
    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
    ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));

    while (1) {
        // Wait for a new UID to be received
        while (!new_uid_received) {
            vTaskDelay(1); // Delay for 1ms
        }

        new_uid_received = false;

        // Print the first byte of the received UID
        ESP_LOGI(TAG, "First byte of received UID: 0x%02X", received_uid[0]);

        // Map the received UID to a mood color index based on the first byte
        switch (received_uid[0]) {
            case 0x33:
                current_mood_color_index = 0; // graduation purple
                break;
            case 0x93:
                current_mood_color_index = 1; // Utopia white
                break;
            case 0x8B:
                current_mood_color_index = 2; // sad light blue
                break;
            case 0x76:
                current_mood_color_index = 3; // Clown blue
                break;
            case 0xC4:
                current_mood_color_index = 4; // Spicy red
                break;
            case 0xB6:
                current_mood_color_index = 5; // Shrek Green
                break;
            case 0x39:
                current_mood_color_index = 6; // Oakar Pink
                break;
            default:
                ESP_LOGI(TAG, "Unknown UID, using default color");
                current_mood_color_index = 0; // Default to dark gray
                break;
        }

        // Initialize the fade phase variables
        int64_t fade_start_time = esp_timer_get_time();
        int64_t fade_duration = (FADE_IN_DURATION_MS + FADE_OUT_DURATION_MS) * 1000;
        float fade_period = (2.0f * M_PI) / fade_duration;

        while (1) {
            // Calculate the elapsed time and the fade value using a sine wave
            int64_t elapsed_time = esp_timer_get_time() - fade_start_time;
            float fade_value = 0.5f * (1.0f + sinf(elapsed_time * fade_period));

            // Update the LED strip pixels with the current mood color and fade value
            const mood_color_t *current_mood_color = &mood_colors[current_mood_color_index];
            for (int i = 0; i < EXAMPLE_LED_NUMBERS; i++) {
                led_strip_pixels[i * 3 + 0] = current_mood_color->green * fade_value;
                led_strip_pixels[i * 3 + 1] = current_mood_color->red * fade_value;
                led_strip_pixels[i * 3 + 2] = current_mood_color->blue * fade_value;
            }

            // Transmit the updated pixels
            ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));

            vTaskDelay(1); // Delay for 1ms to achieve ~1000fps

            // Check if a new UID has been received
            if (new_uid_received) {
                break; // Exit the continuous fade loop and handle the new UID
            }

            // Check if the fade cycle is complete
            if (elapsed_time >= fade_duration) {
                fade_start_time = esp_timer_get_time(); // Reset the fade start time
            }
        }
    }
}

typedef struct struct_message {
    uint8_t uid[4]; // RFID UID
} struct_message;

// Function to print the received UID
void print_uid(const uint8_t *uid) {
    ESP_LOGI(TAG, "UID: %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
}

void espnow_receive_cb(const uint8_t *mac_addr, const uint8_t *data, int len) {
    if (mac_addr == NULL || data == NULL || len < sizeof(struct_message)) {
        ESP_LOGE(TAG, "Receive callback received invalid arguments");
        return;
    }

    ESP_LOGI(TAG, "Received ESP-NOW message from: " MACSTR, MAC2STR(mac_addr));
    struct_message msg;
    memcpy(&msg, data, sizeof(msg));
    print_uid(msg.uid);

    // Process the received data to extract the UID
    uint8_t uid_index = 0;
    const char *ptr = (char *)data;
    while (*ptr) {
        if (isspace((unsigned char)*ptr)) {
            ptr++; // Skip whitespace characters
        } else if (isxdigit((unsigned char)*ptr)) {
            char hex_byte[3] = {ptr[0], ptr[1], '\0'};
            received_uid[uid_index++] = (uint8_t)strtoul(hex_byte, NULL, 16);
            ptr += 3; // Skip past the hex byte and the space after it
        } else {
            break; // Stop processing if an unexpected character is encountered
        }
        if (uid_index >= sizeof(received_uid)) {
            ESP_LOGW(TAG, "UID buffer overflow");
            break;
        }
    }
    received_uid[uid_index] = 0; // Null-terminate the UID
    print_uid(received_uid); // Print the UID for debugging
    new_uid_received = true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    // Print the MAC address of the device
    uint8_t mac_addr[6];
    esp_read_mac(mac_addr, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Device MAC address: %02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Wi-Fi in Station mode
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_receive_cb));

    // Print the receiver's MAC address
    uint8_t receiver_mac_addr[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(receiver_mac_addr, ESP_MAC_WIFI_STA));
    ESP_LOGI(TAG, "Receiver MAC Address: " MACSTR, MAC2STR(receiver_mac_addr));

    xTaskCreate(led_strip_fade_task, "led_strip_fade", 4096, led_chan, 5, NULL);
}