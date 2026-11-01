/*
 * Battery monitoring (see battery.h). VBAT is sampled every SAMPLE_MS once the
 * motors have been off for SETTLE_MS (rumble sags it), and low-pass filtered.
 * The level comes from a typical 2× NiMH curve and only goes down while on
 * batteries (a battery change means a power cycle). Below EMPTY_MV for LOW_MS:
 * power off (not on USB).
 */

#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_power.h>

#include "battery.h"
#include "input.h"
#include "power.h"
#include "rumble.h"

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

#define SAMPLE_MS  100
#define SETTLE_MS  300     /* after the motors stop */
#define RUMBLE_MIN_PCT 25  /* below: no rumble */
#define FILTER_SHIFT 4     /* ~1.6 s */
#define EMPTY_MV   2000    /* 1.0 V per NiMH cell */
#define LOW_MS     10000   /* below EMPTY_MV this long: power off */
#define SENSE_MIN_MV 1500  /* lower can't be running AAs: sense fault, ignore */

#define ZEPHYR_USER DT_PATH(zephyr_user)
/* read in input.c's scan: none with the fake input */
#define HAS_VBAT    (DT_PROP_HAS_NAME(ZEPHYR_USER, io_channels, vbat) && \
		     !IS_ENABLED(CONFIG_XBX_FAKE_INPUT))

#define UNKNOWN 0xFF

#if HAS_VBAT
static const struct adc_dt_spec vbat = ADC_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, vbat);

/*
 * mV → %, linear between points: typical low self-discharge NiMH (Eneloop
 * class) at a light load, per cell ×2. Flat from 70 to 30 %: coarse there.
 */
static const struct {
	uint16_t mv;
	uint8_t pct;
} curve[] = {
	{EMPTY_MV, 0}, {2240, 5}, {2360, 15}, {2420, 30}, {2460, 50},
	{2500, 70}, {2540, 85}, {2600, 95}, {2720, 100},
};

static uint32_t mv_q4; /* filtered, mV << FILTER_SHIFT; 0 = no reading yet */
static uint32_t since_ms;
static uint32_t quiet_ms; /* motors off this long */
static uint32_t low_ms;
static uint8_t level = UNKNOWN;
static bool on_usb;
static bool fault_logged;

static uint8_t mv_to_pct(uint32_t mv)
{
	if (mv <= curve[0].mv) {
		return 0;
	}
	for (int i = 1; i < ARRAY_SIZE(curve); i++) {
		if (mv < curve[i].mv) {
			uint32_t span = curve[i].mv - curve[i - 1].mv;

			return curve[i - 1].pct +
			       (mv - curve[i - 1].mv) * (curve[i].pct - curve[i - 1].pct) / span;
		}
	}
	return 100;
}

/* the last scan's value (the channel's gain and reference: the overlay) */
static int read_mv(int32_t *mv)
{
	int16_t raw;

	if (!input_vbat_raw(&raw)) {
		return -ENODATA;
	}
	*mv = raw;
	return adc_raw_to_millivolts_dt(&vbat, mv);
}
#endif

int battery_init(void)
{
#if HAS_VBAT
	/* the channel is set up by input.c, which scans it */
	return adc_is_ready_dt(&vbat) ? 0 : -ENODEV;
#else
	return 0;
#endif
}

uint8_t battery_tick(uint32_t elapsed_ms)
{
#if HAS_VBAT
	int32_t mv;
	bool usb = nrf_power_usbregstatus_vbusdet_get(NRF_POWER);

	if (usb != on_usb) {
		/* the rail changed source: start over */
		on_usb = usb;
		mv_q4 = 0;
		low_ms = 0;
		level = UNKNOWN;
	}
	quiet_ms = rumble_active() ? 0 : MIN(quiet_ms + elapsed_ms, SETTLE_MS);
	since_ms += elapsed_ms;
	if (since_ms < SAMPLE_MS || quiet_ms < SETTLE_MS) {
		return level;
	}
	since_ms = 0;

	if (read_mv(&mv)) {
		return level;
	}
	if (mv < SENSE_MIN_MV) {
		if (!fault_logged) {
			LOG_WRN("VBAT reads %d mV: sense fault?", mv);
			fault_logged = true;
		}
		mv_q4 = 0;
		low_ms = 0;
		level = UNKNOWN;
		return level;
	}
	if (mv_q4 == 0) {
		mv_q4 = (uint32_t)mv << FILTER_SHIFT;
	} else {
		mv_q4 += mv - (int32_t)(mv_q4 >> FILTER_SHIFT);
	}
	mv = mv_q4 >> FILTER_SHIFT;

	if (usb) {
		/* rail from USB, AAs disconnected: show full, measure for the log */
		level = 100;
		return level;
	}
	uint8_t pct = mv_to_pct(mv);

	if (level != UNKNOWN && level >= RUMBLE_MIN_PCT && pct < RUMBLE_MIN_PCT) {
		LOG_INF("battery %u %%: rumble off", pct);
	}
	level = (level == UNKNOWN) ? pct : MIN(level, pct);

	if (mv < EMPTY_MV) {
		low_ms += SAMPLE_MS;
		if (low_ms >= LOW_MS) {
			LOG_WRN("battery empty: %d mV", mv);
			power_off("battery empty");
		}
	} else {
		low_ms = 0;
	}
	return level;
#else
	ARG_UNUSED(elapsed_ms);
	return UNKNOWN;
#endif
}

bool battery_rumble_ok(void)
{
#if HAS_VBAT
	return level == UNKNOWN || level >= RUMBLE_MIN_PCT;
#else
	return true;
#endif
}

uint16_t battery_mv(void)
{
#if HAS_VBAT
	return mv_q4 >> FILTER_SHIFT;
#else
	return 0;
#endif
}
