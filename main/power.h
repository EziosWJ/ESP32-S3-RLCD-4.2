#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Power management: DFS frequency window, tickless automatic light sleep
 * (gated on USB Serial/JTAG attachment), Wi-Fi modem power save and the
 * transmit power cap.
 *
 * Call once, after every subsystem (Wi-Fi, sensors, display, battery) has
 * been initialized and before entering the main loop. Failures are logged
 * and do not abort boot; the returned status is the first PM error seen.
 */
esp_err_t power_management_init(void);

#ifdef __cplusplus
}
#endif
