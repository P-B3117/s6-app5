#pragma once
#include <stdint.h>
#include "portmacro.h"

void uart_init(void);
int uart_read_line(char *out_buf, size_t out_buf_size, TickType_t timeout_ticks);
