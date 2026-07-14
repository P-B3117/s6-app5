#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"


static const char *TAG = "serial_read";

#define UART_PORT       UART_NUM_0     // UART0 = the USB serial connection to your PC
#define UART_BUF_SIZE   1024
#define RX_BUF_SIZE     256

void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // UART0 is usually already installed for logging, but this makes sure
    // the driver + RX buffer exist so we can read from it too.
    uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
}

// Blocks until a newline-terminated string is received (or buffer fills).
// Returns the number of bytes read (not including null terminator), or -1 on timeout/error.
int uart_read_line(char *out_buf, size_t out_buf_size, TickType_t timeout_ticks)
{
    size_t pos = 0;
    uint8_t byte;

    while (pos < out_buf_size - 1) {
        int len = uart_read_bytes(UART_PORT, &byte, 1, timeout_ticks);

        if (len <= 0) {
            // no data / timeout
            if (pos == 0) {
                return -1;  // nothing received at all
            }
            break;  // return what we have so far
        }

        if (byte == '\n' || byte == '\r') {
            if (pos == 0) {
                continue;  // ignore leading CR/LF
            }
            break;
        }

        out_buf[pos++] = (char)byte;
    }

    out_buf[pos] = '\0';
    return (int)pos;
}
