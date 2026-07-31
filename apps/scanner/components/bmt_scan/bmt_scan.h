#pragma once

#include "esp_err.h"

/* Register the GAP callback, configure scan params, start the
 * radio_manager_task (time-division between GAP scan and mesh publish)
 * and the timeout_check_task. */
esp_err_t bmt_scan_start(void);
