/*
 * Battery monitoring on boards with a VBAT input (zephyr,user io-channel
 * "vbat"; 3v/B+, the switched AA rail). Low battery powers off. On USB power
 * the rail comes from USB: full. Without the input: unknown.
 */

#ifndef XBX_CTRL_BATTERY_H_
#define XBX_CTRL_BATTERY_H_

#include <stdint.h>

int battery_init(void);

/* Call every report tick (TX thread). Returns 0..100 %, 0xFF = unknown. */
uint8_t battery_tick(uint32_t elapsed_ms);

/* Filtered rail voltage, 0 = unknown (for the log). */
uint16_t battery_mv(void);

#endif /* XBX_CTRL_BATTERY_H_ */
