/*
 * Battery monitoring (see battery.h). VBAT is sampled every SAMPLE_MS and
 * low-pass filtered (rumble sags it); the level comes from a 2× NiMH table,
 * untuned. Below EMPTY_MV for LOW_MS: power off (not on USB).
 */

#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_power.h>

#include "battery.h"
#include "power.h"

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

#define SAMPLE_MS  100
#define FILTER_SHIFT 4     /* ~1.6 s */
#define EMPTY_MV   2000    /* 1.0 V per NiMH cell */
#define LOW_MS     10000   /* below EMPTY_MV this long: power off */
#define SENSE_MIN_MV 1500  /* lower can't be running AAs: sense fault, ignore */

#define ZEPHYR_USER DT_PATH(zephyr_user)
#define HAS_VBAT    DT_PROP_HAS_NAME(ZEPHYR_USER, io_channels, vbat)

#define UNKNOWN 0xFF

#if HAS_VBAT
static const struct adc_dt_spec vbat = ADC_DT_SPEC_GET_BY_NAME(ZEPHYR_USER, vbat);

/* mV → %, linear between points; 2× NiMH under light load */
static const struct {
	uint16_t mv;
	uint8_t pct;
} curve[] = {
	{EMPTY_MV, 0}, {2200, 10}, {2350, 40}, {2450, 70}, {2600, 90}, {2750, 100},
};

static struct adc_sequence seq;
static int16_t raw;
static uint32_t mv_q4; /* filtered, mV << FILTER_SHIFT; 0 = no reading yet */
static uint32_t since_ms;
static uint32_t low_ms;
static uint8_t level = UNKNOWN;
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

static int read_mv(int32_t *mv)
{
	int err = adc_read_dt(&vbat, &seq);

	if (err) {
		return err;
	}
	*mv = MAX(raw, 0);
	return adc_raw_to_millivolts_dt(&vbat, mv);
}
#endif

int battery_init(void)
{
#if HAS_VBAT
	int err;

	if (!adc_is_ready_dt(&vbat)) {
		return -ENODEV;
	}
	err = adc_channel_setup_dt(&vbat);
	if (err) {
		return err;
	}
	err = adc_sequence_init_dt(&vbat, &seq);
	seq.buffer = &raw;
	seq.buffer_size = sizeof(raw);
	return err;
#else
	return 0;
#endif
}

uint8_t battery_tick(uint32_t elapsed_ms)
{
#if HAS_VBAT
	int32_t mv;

	since_ms += elapsed_ms;
	if (since_ms < SAMPLE_MS) {
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

	if (nrf_power_usbregstatus_vbusdet_get(NRF_POWER)) {
		/* rail from USB, AAs disconnected: show full, measure for the log */
		low_ms = 0;
		level = 100;
		return level;
	}
	level = mv_to_pct(mv);

	if (mv < EMPTY_MV) {
		low_ms += SAMPLE_MS;
		if (low_ms >= LOW_MS) {
			LOG_WRN("battery empty: %d mV", mv);
			power_off();
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

uint16_t battery_mv(void)
{
#if HAS_VBAT
	return mv_q4 >> FILTER_SHIFT;
#else
	return 0;
#endif
}
