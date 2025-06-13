#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_wifi.h"

#include "../libraries/cJson.h"
#include "main_common.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define ESP_MAX_STA_CONN   CONFIG_ESP_MAX_STA_CONN

static const char *TAG = "AP";

/* Our URI handler function to be called during GET /uri request */
esp_err_t get_handler(httpd_req_t *req) {
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

/* Our URI handler function to be called during POST /uri request */
esp_err_t post_handler(httpd_req_t *req) {
    /* Destination buffer for content of HTTP POST request.
     * httpd_req_recv() accepts char* only, but content could
     * as well be any binary data (needs type casting).
     * In case of string data, null termination will be absent, and
     * content length would give length of string */
    char content[128];

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {  /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
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

/* URI handler structure for GET /uri */
httpd_uri_t uri_get = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = get_handler,
    .user_ctx = NULL
};

/* URI handler structure for POST /uri */
httpd_uri_t uri_post = {
    .uri      = "/api",
    .method   = HTTP_POST,
    .handler  = post_handler,
    .user_ctx = NULL
};

/* Function for starting the webserver */
httpd_handle_t start_webserver(void) {
    /* Generate default configuration */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = CONFIG_HTTP_SERVER_TASK_PRIORITY;
    config.core_id = CONFIG_HTTP_SERVER_TASK_CORE;
    config.stack_size = 4096;

    /* Empty handle to esp_http_server */
    httpd_handle_t server = NULL;

    /* Start the httpd server */
    if (httpd_start(&server, &config) == ESP_OK) {
        /* Register URI handlers */
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);
    }
    /* If server failed to start, handle will be NULL */
    return server;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server) {
    if (server) {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}

void wifi_init_softap(void) {
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
    if (strlen(ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP init finished. SSID:%s password:%s channel:%d",
             ESP_WIFI_SSID, ESP_WIFI_PASS, ESP_WIFI_CHANNEL);
}

esp_err_t start_http_server_task() {
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize NVS");
      return ret;
    }

    /* Initialize AP */
    ESP_LOGI(TAG, "Initializing AP mode...");
    wifi_init_softap();

    /* Start HTTP server */
    if (!start_webserver()) {
      ESP_LOGE(TAG, "Failed to start web server");
      return ESP_FAIL;
    }

    return ESP_OK;
}
