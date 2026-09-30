/*
 * Stick and trigger calibration, stored in flash (Zephyr settings on NVS,
 * key "calib/data"). Values are raw ADC counts, so the data is independent of
 * the board's ADC setup; the layout is versioned.
 */

#ifndef XBX_CTRL_CALIB_H_
#define XBX_CTRL_CALIB_H_

#include <stdbool.h>
#include <stdint.h>

enum calib_axis_id {
	CALIB_LX,
	CALIB_LY,
	CALIB_RX,
	CALIB_RY,
	CALIB_LT, /* triggers: centre unused */
	CALIB_RT,
	CALIB_AXES,
};

#define CALIB_FLAG_INVERT 0x01

struct calib_axis {
	int16_t min;
	int16_t center;
	int16_t max;
	uint8_t flags; /* CALIB_FLAG_* */
	uint8_t reserved;
};

struct calib_data {
	uint8_t version;
	uint8_t valid; /* bit per enum calib_axis_id */
	uint8_t reserved[2];
	struct calib_axis axis[CALIB_AXES];
};

/* Start settings and load the stored calibration, if any. */
int calib_init(void);

/* Copy the stored calibration; false if there is none. */
bool calib_get(struct calib_data *out);

/* Store (or erase) the calibration. Runs on the system work queue; flash
 * erases stall the CPU for tens of ms, so never call from time-critical code
 * that must not wait (these only queue the work).
 */
void calib_save(const struct calib_data *data);
void calib_erase(void);

#endif /* XBX_CTRL_CALIB_H_ */
