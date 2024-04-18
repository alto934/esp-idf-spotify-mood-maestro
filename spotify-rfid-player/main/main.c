#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include <inttypes.h> // Include this header for PRId64
#include <cJSON.h>
#include "driver/uart.h"

#define TAG "SPOTIFY_API"
#define TAG2 "espserial_receiver"

#define UART_NUM UART_NUM_1 // Replace with the appropriate UART number
#define BUF_SIZE (3072)
static QueueHandle_t uart_queue;



// Placeholder for client ID, client secret, access token, and refresh token
char client_id[] = "INSERT_CLIENT_ID"; //can be stored securely in NVS_FLASH for persistance across reboots
char client_secret[] = "INSERT_CLIENT_SECRET"; //can be stored securely in NVS_FLASH for persistance across reboots 
char access_token[252];
char refresh_token[252];

const char *ssid = "INSERT_WIFI_SSID"; //can be stored securely in NVS_FLASH for persistance across reboots
const char *pass = "INSERT_WIFI_PASS"; //can be stored securely in NVS_FLASH for persistance across reboots
int8_t retry_num = 0;

#define AUTH_TASK_STACK_SIZE 4096 // Adjust the size as needed


// Global buffer and its current size
static char *response_buffer = NULL;
static int response_buffer_len = 0;

// UID Message
typedef struct struct_message {
    uint8_t uid[4]; // Changed struct to only include RFID UID
} struct_message;

char saved_device_id[100]; // Assuming the device ID will not exceed this length

esp_err_t handle_http_response(esp_http_client_event_t *evt)
{

    switch (evt->event_id)
    {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR: %s", esp_err_to_name(evt->data));
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // Reallocate the buffer to hold the received data and ensure there's an extra byte for null termination
            char *new_buffer = realloc(response_buffer, response_buffer_len + evt->data_len + 1);
            if (new_buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for response");
                free(response_buffer);
                response_buffer = NULL;
                response_buffer_len = 0;
                return ESP_FAIL;
            }
            response_buffer = new_buffer;

            // Append the new data to the buffer
            memcpy(response_buffer + response_buffer_len, evt->data, evt->data_len);
            response_buffer_len += evt->data_len;
            response_buffer[response_buffer_len] = '\0'; // Null-terminate the buffer
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            int status_code = esp_http_client_get_status_code(evt->client);
            ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);

            if (status_code >= 200 && status_code < 300) {
                // Successful response
                if (esp_http_client_get_content_length(evt->client) != response_buffer_len) {
                    ESP_LOGW(TAG, "Read less data than expected");
                }
                // Handle the response data
                ESP_LOGI(TAG, "Response: %s", response_buffer);
            } else {
                // Error response
                ESP_LOGE(TAG, "HTTP request failed with status code: %d", status_code);
                // Handle the error response
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            status_code = esp_http_client_get_status_code(evt->client);
            ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);
            if (response_buffer != NULL) {
                free(response_buffer);
                response_buffer = NULL;
                response_buffer_len = 0;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Function to get Spotify device ID of a specific device by name
esp_err_t get_spotify_device_id(const char *access_token, const char* target_device_name) {
    ESP_LOGI(TAG, "Getting list of Spotify devices for device name: %s", target_device_name);

    const char *devices_url = "https://api.spotify.com/v1/me/player/devices";
    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);

    esp_http_client_config_t config = {
        .url = devices_url,
        .event_handler = handle_http_response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set headers
    esp_http_client_set_header(client, "Authorization", auth_header);

    // Perform the HTTP GET request
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
        
        // Parse the response to find the device ID
        cJSON *root = cJSON_Parse(response_buffer);
        cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
        cJSON *device;
        bool device_found = false;
        cJSON_ArrayForEach(device, devices) {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(device, "name");
            if (strcmp(name->valuestring, target_device_name) == 0) {
                // Found the target device
                cJSON *id = cJSON_GetObjectItemCaseSensitive(device, "id");
                if (id != NULL) {
                    // Save the device ID for later use
                    strncpy(saved_device_id, id->valuestring, sizeof(saved_device_id) - 1);
                    saved_device_id[sizeof(saved_device_id) - 1] = '\0'; // Ensure null-termination
                    device_found = true;
                    ESP_LOGI(TAG, "Device ID for '%s' saved: %s", name->valuestring, saved_device_id);
                    break; // Stop searching as we've found our device
                }
            }
        }
        cJSON_Delete(root);

        if (!device_found) {
            ESP_LOGW(TAG, "Target device '%s' not found.", target_device_name);
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return ESP_OK;
}

// PUT request
esp_err_t http_put_request(const char *url, const char *data, const char *auth_header) {
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .event_handler = handle_http_response,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set the authorization header
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // Open the connection
    esp_err_t err = esp_http_client_open(client, strlen(data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    // Write the data for the PUT request
    int wlen = esp_http_client_write(client, data, strlen(data));
    if (wlen < 0) {
        ESP_LOGE(TAG, "Write failed");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // Perform the HTTP PUT request
    esp_err_t perform_err = esp_http_client_perform(client);
    if (perform_err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP PUT Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP PUT request failed: %s", esp_err_to_name(perform_err));
    }

    // Clean up the client
    esp_http_client_cleanup(client);

    return perform_err;
}


// Function to perform HTTP GET request
esp_err_t http_get_request(const char *url)
{
  esp_http_client_config_t config = {
      .url = url,
      .event_handler = handle_http_response,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
  return err;
}

// Function to request authorization
void request_authorization()
{
  char url[415];
  sprintf(url, "https://accounts.spotify.com/authorize?client_id=%s&response_type=code&redirect_uri=http://192.168.149.88/&show_dialog=true&scope=user-read-private%%20user-read-email%%20user-modify-playback-state%%20user-read-playback-position%%20user-library-read%%20streaming%%20user-read-playback-state%%20user-read-recently-played%%20playlist-read-private", client_id);
  ESP_LOGI(TAG, "Authorization URL: %s", url);
  // Perform HTTP GET request to authorization URL
  esp_err_t err = http_get_request(url);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to request authorization");
  }
}

static void request_authorization_task(void *pvParameters)
{
  UBaseType_t uxHighWaterMark;
  uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
  printf("Stack high water mark for AuthTask is %u\n", uxHighWaterMark);
  request_authorization();
  // After the task has done some work, check the remaining stack space.
  uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
  printf("Stack high water mark for AuthTask is %u\n", uxHighWaterMark);
  vTaskDelete(NULL);
  
}

// Function to perform HTTP POST request for token exchange
esp_err_t http_post_request(const char *url, const char *post_data, const char *headers)
{
  esp_http_client_config_t config = {
      .url = url,
      .event_handler = handle_http_response,
      .method = HTTP_METHOD_POST,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);

  // Set headers
  if (headers != NULL) {
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_header(client, "Authorization", headers);
    // esp_http_client_set_header(client, "Accept-Encoding", "identity");
  }

  // Set POST data
  if (post_data != NULL) {
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
  }

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
         esp_http_client_get_status_code(client),
         esp_http_client_get_content_length(client));
  } else {
    ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
  }
  
  esp_http_client_cleanup(client);
  return err;
}

/**
 * @brief Get the currently authenticated user's profile information
 *
 * @param access_token The access token for authentication
 * @return esp_err_t ESP_OK if the request was successful, or an error code otherwise
 */
esp_err_t get_user_profile(const char *access_token)
{
    const char *base_url = "https://api.spotify.com/v1/me";

    char request_url[300];
    snprintf(request_url, sizeof(request_url), "%s?access_token=%s", base_url, access_token);

    ESP_LOGI(TAG, "Request URL: %s", request_url); // Print the complete request URL

    esp_http_client_config_t config = {
        .url = base_url,
        .method = HTTP_METHOD_GET,
        .event_handler = handle_http_response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        while (response_buffer_len == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        cJSON *root = cJSON_ParseWithLength(response_buffer, response_buffer_len);
        if (root == NULL) {
            ESP_LOGE(TAG, "Failed to parse JSON response");
            free(response_buffer);
            response_buffer = NULL;
            response_buffer_len = 0;
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        cJSON *display_name = cJSON_GetObjectItemCaseSensitive(root, "display_name");
        if (cJSON_IsString(display_name)) {
            ESP_LOGI(TAG, "User display name: %s", display_name->valuestring);
        }

        cJSON_Delete(root);
        free(response_buffer);
        response_buffer = NULL;
        response_buffer_len = 0;
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

static const char *TAG3 = "SPOTIFY_PLAY";

#define SPOTIFY_API_BASE_URL "https://api.spotify.com/v1/me/player/play"

esp_err_t play_spotify_album(const char *access_token, const char *device_id, const char *album_uri) {
    esp_http_client_config_t config = {
        .url = SPOTIFY_API_BASE_URL,
        .method = HTTP_METHOD_PUT,
        .event_handler = NULL,
        .user_data = NULL,
        .timeout_ms = 10000,  // Adjust the timeout value as needed
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG3, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }
    char auth_header[512];
    // Set the request headers
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", access_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // Prepare the request body
    char request_body[256];
    snprintf(request_body, sizeof(request_body), "{\"context_uri\":\"%s\"}", album_uri);

    // Set the request body
    esp_http_client_set_post_field(client, request_body, strlen(request_body));

    // Construct the complete URL with the query parameter
    char url[128];
    snprintf(url, sizeof(url), "%s?device_id=%s", SPOTIFY_API_BASE_URL, device_id);

    // Set the URL
    esp_http_client_set_url(client, url);
    // Print the complete PUT request for debugging
    ESP_LOGI(TAG3, "PUT Request: %s %s", url, request_body);

    // Send the PUT request
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 204) {
            ESP_LOGI(TAG3, "Successfully played Spotify album: %s", album_uri);
        } else {
            ESP_LOGE(TAG3, "Failed to play Spotify album, status code: %d", status_code);
            // Print the HTTP response
            char response_buffer[2048];
            int response_len = esp_http_client_read_response(client, response_buffer, sizeof(response_buffer));
            if (response_len > 0) {
                response_buffer[response_len] = '\0';  // Null-terminate the buffer
                ESP_LOGE(TAG3, "HTTP Response: %s", response_buffer);
} else {
    ESP_LOGE(TAG3, "Failed to read HTTP response");
}

        
        }
    } else {
        ESP_LOGE(TAG3, "Failed to perform HTTP request: %s", esp_err_to_name(err));
    }

    // Clean up
    esp_http_client_cleanup(client);

    return err;
}


// ESP-serial functions
void play_spotify_content_by_uid(const uint8_t *uid) {
    ESP_LOGI(TAG2, "First byte of UID: 0x%02X", uid[0]);
    // Assuming each UID corresponds to a unique Spotify URI 
    // simple switch case based on the first byte of the UID
    switch(uid[0]) {
        case 0x33:
            play_spotify_album(access_token, saved_device_id, "spotify:album:4SZko61aMnmgvNhfhgTuD3");//graduation
            ESP_LOGI(TAG2, "Free heap size: %lu bytes", esp_get_free_heap_size());
            // get_user_profile(access_token);
            break;
        case 0x93:
            play_spotify_album(access_token, saved_device_id, "spotify:album:18NOKLkZETa4sWwLMIm0UZ"); //Utopia
            break;
        case  0x8B:
            play_spotify_album(access_token, saved_device_id, "spotify:playlist:5W7LO7gT68cTmUefJkrmI2"); //Sad Mix
            break;
        case  0x76:
            play_spotify_album(access_token, saved_device_id, "spotify:playlist:3ULJmafcgqIt9dDngDlufQ"); //Clown Mix
            break;
        case  0xC4:
            play_spotify_album(access_token, saved_device_id, "spotify:playlist:4VEYXB0BHVcRn1xvQh0asU"); //Spicy mix
            break;
        case  0xB6:
            play_spotify_album(access_token, saved_device_id, "spotify:playlist:2j24pbwBa42NSiAz6PrZ0G"); //Shrek
            break;
        case  0x39:
            play_spotify_album(access_token, saved_device_id, "spotify:playlist:67AIpw122AZCIfHW5R1Lt3"); //Oakar's Playlist
            break;
        // Add more cases for different UIDs
        default:
            ESP_LOGI(TAG2, "Unknown UID, cannot play Spotify content");
            break;
    }
}



void print_uid(const uint8_t *uid) {
    ESP_LOGI(TAG2, "Received UID: %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
}

static void rx_task(void *arg) {
    uint8_t data[BUF_SIZE];
    int length = 0;
    uart_event_t event;
    static uint8_t uid[5] = {0}; // Buffer to store the received UID
    static uint8_t uid_index = 0;

    for (;;) {
        if (xQueueReceive(uart_queue, (void *)&event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    length = uart_read_bytes(UART_NUM, data, event.size, portMAX_DELAY);
                    data[length] = 0; // Null-terminate the received data

                    // Process the received data to extract the UID
                    uint8_t *ptr = (uint8_t *)strstr((char *)data, "UID:");
                    if (ptr != NULL) {
                        ptr += 4; // Skip past "UID:"
                        uid_index = 0;
                        while (*ptr) {
                            if (isspace(*ptr)) {
                                ptr++; // Skip whitespace characters
                            } else if (isxdigit(*ptr)) {
                                char hex_byte[3] = {ptr[0], ptr[1], '\0'};
                                uid[uid_index++] = (uint8_t)strtoul(hex_byte, NULL, 16);
                                ptr += 3; // Skip past the hex byte and the space
                            } else {
                                break; // Stop processing if an unexpected character is encountered
                            }
                            if (uid_index >= sizeof(uid)) {
                                ESP_LOGW(TAG2, "UID buffer overflow");
                                break;
                            }
                        }
                        uid[uid_index] = 0; // Null-terminate the UID
                        print_uid(uid); // Print the UID for debugging
                        play_spotify_content_by_uid(uid); // Play Spotify content based on UID
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

// Function to extract tokens from JSON response
static esp_err_t extract_tokens(const char *json_response, char *access_token, size_t access_token_size, char *refresh_token, size_t refresh_token_size) {
    ESP_LOGI(TAG, "JSON Response: %s", json_response);
    cJSON *json = cJSON_Parse(json_response);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error: %s", error_ptr);
        } else {
            ESP_LOGE(TAG, "JSON parse error but error pointer is null");
        }
        return ESP_FAIL;
    }

    const cJSON *access_token_json = cJSON_GetObjectItemCaseSensitive(json, "access_token");
    const cJSON *refresh_token_json = cJSON_GetObjectItemCaseSensitive(json, "refresh_token");

    if (access_token_json && cJSON_IsString(access_token_json) && (access_token_json->valuestring != NULL)) {
        strncpy(access_token, access_token_json->valuestring, access_token_size - 1);
        access_token[access_token_size - 1] = '\0'; // Ensure null-termination
    } else {
        ESP_LOGE(TAG, "Access token not found or is not a string in JSON response");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Check if refresh_token is required by the caller (refresh_token_size > 0)
    if (refresh_token_size > 0) {
        if (refresh_token_json && cJSON_IsString(refresh_token_json) && (refresh_token_json->valuestring != NULL)) {
            strncpy(refresh_token, refresh_token_json->valuestring, refresh_token_size - 1);
            refresh_token[refresh_token_size - 1] = '\0'; // Ensure null-termination
        } else {
            ESP_LOGW(TAG, "Refresh token not found or is not a string in JSON response");
            // Not failing the function here as refresh token might be optional
        }
    }

    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t exchange_auth_code_for_tokens(const char *auth_code)
{
    esp_http_client_config_t config = {
        .url = "https://accounts.spotify.com/api/token",
        .method = HTTP_METHOD_POST,
        .event_handler = handle_http_response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Prepare the POST data
    char post_data[670];
    int post_data_len = snprintf(post_data, sizeof(post_data),
                                 "grant_type=authorization_code&code=%s&redirect_uri=%s&client_id=%s&client_secret=%s",
                                 auth_code, "http://192.168.149.88/", client_id, client_secret);

    if (post_data_len >= sizeof(post_data) - 1) {
        ESP_LOGE(TAG, "Post data was truncated");
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_post_field(client, post_data, post_data_len);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

    // Perform the HTTP POST request
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        // Wait for the complete response to be received
        while (response_buffer_len == 0) {
            vTaskDelay(pdMS_TO_TICKS(10)); // Delay for 10 milliseconds
        }

        // Extract tokens from the response
        err = extract_tokens(response_buffer, access_token, sizeof(access_token), refresh_token, sizeof(refresh_token));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to extract tokens from the response");
        } else {
            ESP_LOGI(TAG, "Access token: %s", access_token);
            ESP_LOGI(TAG, "Refresh token: %s", refresh_token);
        }

        // Free the response buffer
        free(response_buffer);
        response_buffer = NULL;
        response_buffer_len = 0;
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// WiFi event handler
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_id == WIFI_EVENT_STA_START)
  {
    printf("WIFI CONNECTING....\n");
  }
  else if (event_id == WIFI_EVENT_STA_CONNECTED)
  {
    printf("WiFi CONNECTED\n");
  }
  else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    printf("WiFi lost connection\n");
    if (retry_num < 5)
    {
      esp_wifi_connect();
      retry_num++;
      printf("Retrying to Connect...\n");
    }
  }
  else if (event_id == IP_EVENT_STA_GOT_IP)
  {
    printf("Wifi got IP...\n\n");
    // Create a task for requesting authorization to avoid stack overflow
    xTaskCreate(request_authorization_task, "auth_task", AUTH_TASK_STACK_SIZE, NULL, 5, NULL);
    
  }
}

// Function to initialize WiFi connection
void wifi_connection()
{
  // Initialize Wi-Fi
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  // Initialize Wi-Fi configuration
  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

  // Register Wi-Fi event handler
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  // Set Wi-Fi configuration
  wifi_config_t wifi_config = {0}; // Zero-initialize the config struct
  strcpy((char *)wifi_config.sta.ssid, ssid);
  strcpy((char *)wifi_config.sta.password, pass);

  // Start Wi-Fi connection
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_connect());
  printf("Wi-Fi connection initiated\n");
}

// Function to handle the HTTP GET request on the redirect URI
esp_err_t redirect_handler(httpd_req_t *req)
{
  ESP_LOGI("redirect_handler", "Request Method: %d", req->method);
  char *buf;
  size_t buf_len;

  // Get the length of the query string of the request URI
  buf_len = httpd_req_get_url_query_len(req) + 1; // +1 for null-terminating character

  if (buf_len > 1)
  {
    // Allocate memory for the buffer to store the query string
    buf = malloc(buf_len);
    if (!buf)
    {
      ESP_LOGE("redirect_handler", "Failed to allocate memory for buf.");
      return ESP_FAIL;
    }

    // Get the query string
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
    {
      ESP_LOGI("redirect_handler", "Query string: %s", buf);
      // Now parse the URI to extract the "code" query parameter
      char param[512]; // Buffer to store the value of the "code" parameter
      if (httpd_query_key_value(buf, "code", param, sizeof(param)) == ESP_OK)
      {
        ESP_LOGI("redirect_handler", "Received code: %s", param);
        // Code to handle the received 'code' parameter
        // exchange_auth_code_for_tokens(param);
        esp_err_t err = exchange_auth_code_for_tokens(param);
        if (err == ESP_OK) {
           
            // Access token successfully obtained
            err = get_user_profile(access_token);
  
            err = get_spotify_device_id(access_token, "Akhil’s Laptop");
            // err = play_spotify_album(access_token, "26ddd1d634a07ce6e730676e4bbfe122f489b1e0", "spotify:album:06mXfvDsRZNfnsGZvX2zpb");
            // err = play_spotify_track(access_token,"26ddd1d634a07ce6e730676e4bbfe122f489b1e0","spotify:track:58xpZwxUpgrnJMTEmvkZMP");
            // err = get_currently_playing(access_token);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get user profile");
                ESP_LOGE(TAG, "Failed to get user devices");
                ESP_LOGE(TAG3, "Failed to play Spotify album");
            }
        }
        
      }
      else
      {
        ESP_LOGW("redirect_handler", "Code parameter not found in the request.");
        free(buf);
        return ESP_FAIL;
      }
    }
    else
    {
      ESP_LOGW("redirect_handler", "Failed to get query string.");
      free(buf);
      return ESP_FAIL;
    }

    free(buf);
  }
  else
  {
    ESP_LOGW("redirect_handler", "Query string length is zero.");
    return ESP_FAIL;
  }

  // Send response to the client
  const char *resp_str = "Authorization received";
  httpd_resp_send(req, resp_str, strlen(resp_str));

  return ESP_OK;
}


// Initialize the HTTP server
httpd_handle_t start_webserver(void)
{
  
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192; // Set the stack size of the server task

  // Start the httpd server
  if (httpd_start(&server, &config) == ESP_OK)
  {
    // Set URI handler for the redirect URI
    httpd_uri_t redirect_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = redirect_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &redirect_uri);
  }

  return server;
}

void stop_webserver(httpd_handle_t server)
{
  // Stop the httpd server
  httpd_stop(server);
}

void app_main(void)
{
  // Initialize NVS
  // Set log level for all components to verbose
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  esp_log_level_set("wifi", ESP_LOG_WARN);
  ESP_ERROR_CHECK(nvs_flash_init());

  // Start WiFi connection
  wifi_connection();

  uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE); // Replace with the appropriate RX and TX pins

    uart_driver_install(UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart_queue, 0);

    xTaskCreate(rx_task, "uart_rx_task", 16384, NULL, configMAX_PRIORITIES - 1, NULL);

  // Start the HTTP server
  httpd_handle_t server = start_webserver();
  if (server == NULL)
  {
    printf("Failed to start the web server\n");
    return;
  }

  printf("Web server started\n");

  
} 
