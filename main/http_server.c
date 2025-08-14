#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_server.h"
#include "esp_wifi.h"

#include "../libraries/cJson.h"
#include "main_common.h"

#define ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define ESP_WIFI_CHANNEL CONFIG_ESP_WIFI_CHANNEL
#define ESP_MAX_STA_CONN CONFIG_ESP_MAX_STA_CONN

#define OTA_DATA_BUFFER_SIZE 1024

static const char *TAG = "AP";

static char ota_write_data[OTA_DATA_BUFFER_SIZE + 1] = {0};

/* Our URI handler function to be called during root GET request */
esp_err_t get_handler(httpd_req_t *req)
{
  /* Send a simple response */
  const char resp[] =
      "<!DOCTYPE html>\n"
      "<html lang=\"en\">\n"
      "<head>\n"
      "  <meta charset=\"UTF-8\">\n"
      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
      "  <title>Tesla Ambient Lighting</title>\n"
      "</head>\n"
      "<body>\n"
      "  <h1>Tesla Ambient Lighting Control</h1>\n"
      "  <p><b>Note:</b> Any changes to color made will appear on the next light refresh</p>\n"
      "  <label for=\"red\">Red:</label>\n"
      "  <input type=\"number\" id=\"red\" name=\"red\" min=\"0\" max=\"255\" value=\"0\">\n"
      "  <br>\n"
      "  <label for=\"green\">Green:</label>\n"
      "  <input type=\"number\" id=\"green\" name=\"green\" min=\"0\" max=\"255\" value=\"0\">\n"
      "  <br>\n"
      "  <label for=\"blue\">Blue:</label>\n"
      "  <input type=\"number\" id=\"blue\" name=\"blue\" min=\"0\" max=\"255\" value=\"0\">\n"
      "  <br>\n"
      "  <button id=\"submitBtn\">Submit</button>\n"
      "  <script>\n"
      "    document.getElementById('submitBtn').addEventListener('click', function() {\n"
      "      const red = document.getElementById('red').value;\n"
      "      const green = document.getElementById('green').value;\n"
      "      const blue = document.getElementById('blue').value;\n"
      "      fetch('/api', {\n"
      "        method: 'POST',\n"
      "        headers: {\n"
      "          'Content-Type': 'application/json'\n"
      "        },\n"
      "        body: JSON.stringify({ red: Number(red), green: Number(green), blue: Number(blue) })\n"
      "      });\n"
      "    });\n"
      "  </script>\n"
      "</body>\n"
      "</html>";
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

/* Our URI handler function to be called during POST /api request */
esp_err_t api_handler(httpd_req_t *req)
{
  /* Destination buffer for content of HTTP POST request.
   * httpd_req_recv() accepts char* only, but content could
   * as well be any binary data (needs type casting).
   * In case of string data, null termination will be absent, and
   * content length would give length of string */
  char content[128];

  /* Truncate if content length larger than the buffer */
  size_t recv_size = fmin(req->content_len, sizeof(content));

  int ret = httpd_req_recv(req, content, recv_size);
  if (ret <= 0)
  { /* 0 return value indicates connection closed */
    /* Check if timeout occurred */
    if (ret == HTTPD_SOCK_ERR_TIMEOUT)
    {
      /* In case of timeout one can choose to retry calling
       * httpd_req_recv(), but to keep it simple, here we
       * respond with an HTTP 408 (Request Timeout) error */
      httpd_resp_send_408(req);
    }
    /* In case of error, returning ESP_FAIL will
     * ensure that the underlying socket is closed */
    return ESP_FAIL;
  }

  /* Send a simple response */
  const char resp[] = "API request received";
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

  /* Parse JSON data */
  ESP_LOGI(TAG, "Received data: %.*s", recv_size, content);
  cJSON *json = cJSON_ParseWithLength(content, recv_size);

  uint8_t red = cJSON_GetNumberValue(cJSON_GetObjectItem(json, "red"));
  uint8_t green = cJSON_GetNumberValue(cJSON_GetObjectItem(json, "green"));
  uint8_t blue = cJSON_GetNumberValue(cJSON_GetObjectItem(json, "blue"));
  rgb_t color = {red, green, blue};

  /* Deallocate JSON data */
  cJSON_Delete(json);

  /* Update current color of ambient lighting */
  xSemaphoreTake(current_color_lock, portMAX_DELAY);
  current_color = color;
  xSemaphoreGive(current_color_lock);

  return ESP_OK;
}

/* Our URI handler function to be called during POST /uri request */
esp_err_t ota_handler(httpd_req_t *req)
{
  esp_err_t err;
  /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
  esp_ota_handle_t update_handle = 0;
  const esp_partition_t *update_partition = NULL;

  ESP_LOGI(TAG, "Starting OTA update...");

  const esp_partition_t *configured = esp_ota_get_boot_partition();
  const esp_partition_t *running = esp_ota_get_running_partition();

  if (configured != running)
  {
    ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08" PRIx32 ", but running from offset 0x%08" PRIx32,
             configured->address, running->address);
    ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
  }
  ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08" PRIx32 ")",
           running->type, running->subtype, running->address);

  update_partition = esp_ota_get_next_update_partition(NULL);
  ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%" PRIx32,
           update_partition->subtype, update_partition->address);

  int binary_file_length = 0;
  /*deal with all receive packet*/
  bool image_header_was_checked = false;

  while (1)
  {
    int data_read = httpd_req_recv(req, ota_write_data, OTA_DATA_BUFFER_SIZE);
    if (data_read > 0)
    {
      if (image_header_was_checked == false)
      {
        esp_app_desc_t new_app_info;
        if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))
        {
          // check current version with downloading
          memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
          ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

          esp_app_desc_t running_app_info;
          if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK)
          {
            ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
          }

          image_header_was_checked = true;

          err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
          if (err != ESP_OK)
          {
            ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            return ESP_FAIL;
          }
          ESP_LOGI(TAG, "esp_ota_begin succeeded");
        }
        else
        {
          ESP_LOGE(TAG, "received package is not fit len");
          esp_ota_abort(update_handle);
          return ESP_FAIL;
        }
      }
      err = esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
      if (err != ESP_OK)
      {
        esp_ota_abort(update_handle);
        return ESP_FAIL;
      }
      binary_file_length += data_read;
      ESP_LOGD(TAG, "Written image length %d", binary_file_length);
    }
    else if (data_read == 0)
    {
      ESP_LOGI(TAG, "Connection closed, all data received");
      break; // End of file
    }
  }
  ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

  err = esp_ota_end(update_handle);
  if (err != ESP_OK)
  {
    if (err == ESP_ERR_OTA_VALIDATE_FAILED)
    {
      ESP_LOGE(TAG, "Image validation failed, image is corrupted");
    }
    else
    {
      ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
    }
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Restarting system!");
  esp_restart();
  return ESP_OK;
}

/* URI handler structure for root GET */
httpd_uri_t uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = get_handler,
    .user_ctx = NULL};

/* URI handler structure for POST /api */
httpd_uri_t uri_post = {
    .uri = "/api",
    .method = HTTP_POST,
    .handler = api_handler,
    .user_ctx = NULL};

/* URI handler structure for POST /ota */
httpd_uri_t ota_post = {
    .uri = "/ota",
    .method = HTTP_POST,
    .handler = ota_handler,
    .user_ctx = NULL};

/* Function for starting the webserver */
httpd_handle_t start_webserver(void)
{
  /* Generate default configuration */
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = CONFIG_HTTP_SERVER_TASK_PRIORITY;
  config.core_id = CONFIG_HTTP_SERVER_TASK_CORE;
  config.stack_size = 4096;

  /* Empty handle to esp_http_server */
  httpd_handle_t server = NULL;

  /* Start the httpd server */
  if (httpd_start(&server, &config) == ESP_OK)
  {
    /* Register URI handlers */
    httpd_register_uri_handler(server, &uri_get);
    httpd_register_uri_handler(server, &uri_post);
    httpd_register_uri_handler(server, &ota_post);
  }
  /* If server failed to start, handle will be NULL */
  return server;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server)
{
  if (server)
  {
    /* Stop the httpd server */
    httpd_stop(server);
  }
}

void wifi_init_softap(void)
{
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_config = {
      .ap = {
          .ssid = ESP_WIFI_SSID,
          .ssid_len = strlen(ESP_WIFI_SSID),
          .channel = ESP_WIFI_CHANNEL,
          .password = ESP_WIFI_PASS,
          .max_connection = ESP_MAX_STA_CONN,
          .authmode = WIFI_AUTH_WPA3_PSK,
          .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
          .pmf_cfg = {
              .required = true,
          },
      },
  };
  if (strlen(ESP_WIFI_PASS) == 0)
  {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "SoftAP init finished. SSID:%s password:%s channel:%d",
           ESP_WIFI_SSID, ESP_WIFI_PASS, ESP_WIFI_CHANNEL);
}

esp_err_t start_http_server_task()
{
  /* Initialize NVS */
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to initialize NVS");
    return ret;
  }

  /* Initialize AP */
  ESP_LOGI(TAG, "Initializing AP mode...");
  wifi_init_softap();

  /* Start HTTP server */
  if (!start_webserver())
  {
    ESP_LOGE(TAG, "Failed to start web server");
    return ESP_FAIL;
  }

  return ESP_OK;
}
