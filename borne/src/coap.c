/*
 * coap_server.c
 *
 * Minimal CoAP server task for ESP32 (ESP-IDF), no DTLS / no auth.
 * Runs alongside existing WiFi + BLE. Exposes a single resource
 * "ack" that triggers a user callback whenever it's requested.
 *
 * Requires ESP-IDF's built-in coap component (protocols/libcoap wrapper).
 * See sdkconfig notes at the bottom of this file.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "coap3/coap.h"

#include "coap.h"

static const char *TAG = "COAP";

#define COAP_SERVER_PORT   "5683"   /* default CoAP port, plain UDP */
#define COAP_TASK_STACK    (8 * 1024)
#define COAP_TASK_PRIO     5

/* ------------------------------------------------------------------ */
/* User-facing callback: called every time the "ack" resource is hit */
/* ------------------------------------------------------------------ */
static coap_server_hit_cb_t s_user_cb = NULL;

void coap_server_set_callback(coap_server_hit_cb_t cb)
{
    s_user_cb = cb;
}

/* ------------------------------------------------------------------ */
/* Resource handler                                                    */
/* ------------------------------------------------------------------ */
static void handle_resource(const char *resource_name,
                            coap_resource_t *resource,
                            coap_session_t *session,
                            const coap_pdu_t *request,
                            const coap_string_t *query,
                            coap_pdu_t *response)
{
    ESP_LOGE(TAG, ">>> handle_resource called for '%s'", resource_name);
    coap_pdu_code_t method = coap_pdu_get_code(request);
    const char *method_str = "UNKNOWN";

    switch (method) {
        case COAP_REQUEST_GET:    method_str = "GET";    break;
        case COAP_REQUEST_POST:   method_str = "POST";   break;
        case COAP_REQUEST_PUT:    method_str = "PUT";    break;
        case COAP_REQUEST_DELETE: method_str = "DELETE"; break;
        default: break;
    }

    const char *query_str = (query && query->s) ? (const char *)query->s : "";

    ESP_LOGI(TAG, "Resource '%s' hit: method=%s query=%s", resource_name, method_str, query_str);

    /* Fire the user callback */
    if (s_user_cb) {
        s_user_cb(resource_name, method_str, query_str);
    } else {
        ESP_LOGE(TAG, "No user callback registered for resource '%s'", resource_name);
    }

    /* Reply with a simple text payload */
    static const char msg[] = "hello from esp32";
    coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
    coap_add_data(response, strlen(msg), (const uint8_t *)msg);
}

static void ack_handler(coap_resource_t *resource,
                         coap_session_t *session,
                         const coap_pdu_t *request,
                         const coap_string_t *query,
                         coap_pdu_t *response)
{
    handle_resource("ack", resource, session, request, query, response);
}

/* ------------------------------------------------------------------ */
/* CoAP server task                                                     */
/* ------------------------------------------------------------------ */
static void coap_server_task(void *pvParameters)
{
    coap_context_t *ctx = NULL;
    coap_address_t serv_addr;
    coap_resource_t *resource = NULL;

    coap_startup();

    /* Bind to any address, IPv4, plain UDP, default CoAP port */
    coap_address_init(&serv_addr);
    serv_addr.addr.sin.sin_family = AF_INET;
    serv_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;
    serv_addr.addr.sin.sin_port = htons(atoi(COAP_SERVER_PORT));

    while (1) {
        ctx = coap_new_context(NULL);
        if (!ctx) {
            ESP_LOGE(TAG, "coap_new_context() failed, retrying in 2s");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* IO/blocking mode: let coap manage its own sockets */
        coap_context_set_block_mode(ctx, COAP_BLOCK_USE_LIBCOAP);

        coap_endpoint_t *ep = coap_new_endpoint(ctx, &serv_addr, COAP_PROTO_UDP);
        if (!ep) {
            ESP_LOGE(TAG, "coap_new_endpoint() failed, retrying in 2s");
            coap_free_context(ctx);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* Register resource "ack" -> reachable at coap://<ip>/ack */
        coap_resource_t *ack_resource = coap_resource_init(coap_make_str_const("ack"), 0);
        coap_register_handler(ack_resource, COAP_REQUEST_POST,   ack_handler);
        coap_add_resource(ctx, ack_resource);

        ESP_LOGI(TAG, "CoAP server listening on UDP port %s, resources: /ack", COAP_SERVER_PORT);

        /* Main loop: process incoming requests */
        while (1) {
            int result = coap_io_process(ctx, 1000 /* ms timeout */);
            if (result < 0) {
                ESP_LOGE(TAG, "coap_io_process() error, restarting context");
                break;
            }
            /* yield to other tasks (WiFi/BLE) */
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        coap_free_context(ctx);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* not reached */
    coap_cleanup();
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* Public start function                                               */
/* ------------------------------------------------------------------ */
void coap_server_start(coap_server_hit_cb_t on_hit)
{
    coap_server_set_callback(on_hit);
    xTaskCreate(coap_server_task, "coap_server", COAP_TASK_STACK, NULL, COAP_TASK_PRIO, NULL);
}

/*
 * Example usage from app_main() (after WiFi is connected):
 *
 *   static void my_coap_cb(const char *method, const char *query) {
 *       printf("CoAP hit! method=%s query=%s\n", method, query);
 *   }
 *
 *   coap_server_start(my_coap_cb);
 *
 * Then test from a PC on the same network with libcoap's coap-client:
 *   coap-client -m get coap://<esp32-ip>/hello
 */
