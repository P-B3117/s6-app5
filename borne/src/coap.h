#pragma once

typedef void (*coap_server_hit_cb_t)(const char *resource, const char *method, const char *query);

void coap_server_start(coap_server_hit_cb_t on_hit);
