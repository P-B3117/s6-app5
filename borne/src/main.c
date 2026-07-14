#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

#include "http.h"
#include "wifi.h"

static const char *TAG = "BLE_SCAN";
static const char *URL = "http://172.24.225.206:3001";

#define SCAN_DURATION_SEC 10

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type =
        BLE_SCAN_TYPE_ACTIVE, // active scan, matches setActiveScan(true)
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval =
        0x00A0,            // ~100 * 0.625ms = 62.5ms, matches setInterval(100)
    .scan_window = 0x0063, // ~99 * 0.625ms  = 61.875ms, matches setWindow(99)
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE};

static int devices_found = 0;

QueueHandle_t http_queue;

typedef struct {
  char url[256];
  char json_payload[256];
} http_request_t;

// Extract the device name (if any) from the advertisement/scan-response payload
static bool get_adv_name(uint8_t *adv_data, uint8_t adv_data_len,
                         char *name_out, size_t name_out_len) {
  uint8_t name_len = 0;
  uint8_t *name_field =
      esp_ble_resolve_adv_data(adv_data, ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);

  if (name_field == NULL || name_len == 0) {
    name_field = esp_ble_resolve_adv_data(adv_data, ESP_BLE_AD_TYPE_NAME_SHORT,
                                          &name_len);
  }

  if (name_field == NULL || name_len == 0) {
    return false;
  }

  if (name_len >= name_out_len) {
    name_len = name_out_len - 1;
  }

  memcpy(name_out, name_field, name_len);
  name_out[name_len] = '\0';
  return true;
}

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param) {
  switch (event) {

  case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
    // Scan params are set, now start scanning
    esp_ble_gap_start_scanning(SCAN_DURATION_SEC);
    break;

  case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
    if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
      ESP_LOGE(TAG, "Scan start failed");
    } else {
      ESP_LOGI(TAG, "Scan started!");
    }
    break;

  case ESP_GAP_BLE_SCAN_RESULT_EVT: {
    esp_ble_gap_cb_param_t *scan_result = param;

    if (scan_result->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {

      ESP_LOGI(TAG, "ESP_GAP_SEARCH_INQ_RES_EVT");
      http_request_t request;
      devices_found++;

      char name[64] = {0};
      char addr_str[18];
      bool has_name =
          get_adv_name(scan_result->scan_rst.ble_adv,
                       scan_result->scan_rst.adv_data_len, name, sizeof(name));

      if (has_name) {
        uint8_t *addr = scan_result->scan_rst.bda;
        snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

        ESP_LOGI(TAG, "Name: %s", name);
        ESP_LOGI(TAG, "Advertised Device: addr=%s, rssi=%d", addr_str,
                 scan_result->scan_rst.rssi);
      }
      snprintf(request.json_payload, sizeof(request.json_payload),
               "{\"topic\": \"/borne/spotted/%s\", \"message\": {\"name\": "
               "\"%s\", \"rssi\": %d}}",
               has_name ? addr_str : "unknown", has_name ? name : "",
               scan_result->scan_rst.rssi);
      xQueueSend(http_queue, &request, portMAX_DELAY);
    }
    break;
  }

  case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
    ESP_LOGI(TAG, "Devices found: %d", devices_found);
    ESP_LOGI(TAG, "Scan done!");
    devices_found = 0;

    // Wait 5 seconds (matches delay(5000)) then restart the scan
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_ble_gap_start_scanning(SCAN_DURATION_SEC);
    break;

  default:
    break;
  }
}

void http_task(void *pvParameters) {
  while (1) {
    http_request_t request;
    if (xQueueReceive(http_queue, &request, portMAX_DELAY) == pdTRUE) {
      snprintf(request.url, sizeof(request.url), "%s", URL);
      ESP_LOGI(TAG, "HTTP request: %s", request.url);
      http_post_json(request.url, request.json_payload);
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

void app_main(void) {
  esp_err_t ret;

  http_queue = xQueueCreate(10, sizeof(http_request_t));

  // NVS is required by the BT stack
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGD(TAG, "WIFI_TIME");

  wifi_start();

  // Release classic BT memory since we only use BLE
  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
  ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

  ESP_ERROR_CHECK(esp_bluedroid_init());
  ESP_ERROR_CHECK(esp_bluedroid_enable());

  ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
  ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&ble_scan_params));

  ESP_LOGI(TAG, "Starting task http...\n");
  xTaskCreatePinnedToCore(http_task, "http", 2048, NULL,
                          configMAX_PRIORITIES - 1, NULL, 1);

  // The rest happens in gap_event_handler:
  // SCAN_PARAM_SET_COMPLETE -> start scan -> SCAN_RESULT (per device) ->
  // SCAN_STOP -> delay -> repeat
}
