#pragma once

/* Initialise the UART driver and the command-handling task
 * (commands: 1/2/3/4, s/p/a/m, u/g, 0/9). */
void bmt_uart_start(void);
