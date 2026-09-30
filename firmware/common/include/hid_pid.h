/*
 * HID PID force feedback, sine only (docs/protocol.md, "USB HID mode").
 * Linux's hid-pidff drives it; SDL and Steam rumble through a sine effect.
 *
 * HID_PID_DESC goes inside the gamepad's application collection. It sets
 * every global it relies on and leaves Unit and Unit Exponent at 0.
 */

#ifndef XBX_HID_PID_H_
#define XBX_HID_PID_H_

#include <stdint.h>

#include <zephyr/usb/class/hid.h>

#define HID_PID_MAX_EFFECTS 16

/* Output report IDs */
#define HID_PID_ID_SET_EFFECT     0x11
#define HID_PID_ID_SET_ENVELOPE   0x12
#define HID_PID_ID_SET_PERIODIC   0x13
#define HID_PID_ID_EFFECT_OP      0x14
#define HID_PID_ID_BLOCK_FREE     0x15
#define HID_PID_ID_DEVICE_CONTROL 0x16
#define HID_PID_ID_DEVICE_GAIN    0x17
/* Feature report IDs */
#define HID_PID_ID_CREATE_EFFECT  0x21
#define HID_PID_ID_BLOCK_LOAD     0x22
#define HID_PID_ID_POOL           0x23

/* PID page (0x0F) usages */
#define PID_SET_EFFECT_REPORT     0x21
#define PID_EFFECT_BLOCK_INDEX    0x22
#define PID_EFFECT_TYPE           0x25
#define PID_ET_SINE               0x31
#define PID_DURATION              0x50
#define PID_GAIN                  0x52
#define PID_TRIGGER_BUTTON        0x53
#define PID_TRIGGER_REPEAT_INT    0x54
#define PID_DIRECTION_ENABLE      0x56
#define PID_DIRECTION             0x57
#define PID_SET_ENVELOPE_REPORT   0x5A
#define PID_ATTACK_LEVEL          0x5B
#define PID_ATTACK_TIME           0x5C
#define PID_FADE_LEVEL            0x5D
#define PID_FADE_TIME             0x5E
#define PID_SET_PERIODIC_REPORT   0x6E
#define PID_OFFSET                0x6F
#define PID_MAGNITUDE             0x70
#define PID_PHASE                 0x71
#define PID_PERIOD                0x72
#define PID_EFFECT_OP_REPORT      0x77
#define PID_EFFECT_OPERATION      0x78
#define PID_OP_EFFECT_START       0x79
#define PID_OP_EFFECT_START_SOLO  0x7A
#define PID_OP_EFFECT_STOP        0x7B
#define PID_LOOP_COUNT            0x7C
#define PID_DEVICE_GAIN_REPORT    0x7D
#define PID_DEVICE_GAIN           0x7E
#define PID_POOL_REPORT           0x7F
#define PID_RAM_POOL_SIZE         0x80
#define PID_SIMULTANEOUS_MAX      0x83
#define PID_BLOCK_LOAD_REPORT     0x89
#define PID_BLOCK_LOAD_STATUS     0x8B
#define PID_BLOCK_LOAD_SUCCESS    0x8C
#define PID_BLOCK_LOAD_FULL       0x8D
#define PID_BLOCK_LOAD_ERROR      0x8E
#define PID_BLOCK_FREE_REPORT     0x90
#define PID_DEVICE_CONTROL        0x96
#define PID_DC_ENABLE_ACTUATORS   0x97
#define PID_DC_RESET              0x9A
#define PID_DC_CONTINUE           0x9C
#define PID_DEVICE_MANAGED_POOL   0xA9
#define PID_SHARED_PARAM_BLOCKS   0xAA
#define PID_START_DELAY           0xA7
#define PID_CREATE_EFFECT_REPORT  0xAB
#define PID_RAM_POOL_AVAILABLE    0xAC

#define HID_PAGE_PID     0x0F
#define HID_PAGE_ORDINAL 0x0A

/* Main item flags */
#define PID_DATA_VAR_ABS   0x02
#define PID_DATA_ARRAY_ABS 0x00
#define PID_CONST          0x01

#define PID_LOGICAL_0_255   HID_LOGICAL_MIN8(0), HID_LOGICAL_MAX16(0xFF, 0x00)
#define PID_LOGICAL_0_65535 HID_LOGICAL_MIN8(0), HID_LOGICAL_MAX32(0xFF, 0xFF, 0x00, 0x00)
#define PID_BLOCK_INDEX_ITEMS                                                                   \
	HID_USAGE(PID_EFFECT_BLOCK_INDEX), HID_LOGICAL_MIN8(1),                                 \
		HID_LOGICAL_MAX8(HID_PID_MAX_EFFECTS), HID_REPORT_SIZE(8), HID_REPORT_COUNT(1)
/* times in ms: unit SI linear seconds, exponent -3 (two's complement nibble) */
#define PID_UNIT_MS   HID_ITEM(HID_ITEM_TAG_UNIT, HID_ITEM_TYPE_GLOBAL, 2), 0x01, 0x10, \
		      HID_UNIT_EXPONENT(0x0D)
#define PID_UNIT_NONE HID_ITEM(HID_ITEM_TAG_UNIT, HID_ITEM_TYPE_GLOBAL, 1), 0x00, \
		      HID_UNIT_EXPONENT(0x00)
/* array field listing the one effect type: logical collection, as hid-pidff expects */
#define PID_EFFECT_TYPE_ITEMS(main)                                                             \
	HID_USAGE(PID_EFFECT_TYPE), HID_COLLECTION(HID_COLLECTION_LOGICAL),                     \
		HID_USAGE(PID_ET_SINE), HID_LOGICAL_MIN8(1), HID_LOGICAL_MAX8(1),               \
		HID_REPORT_SIZE(8), HID_REPORT_COUNT(1), main(PID_DATA_ARRAY_ABS),              \
		HID_END_COLLECTION

#define HID_PID_DESC                                                                            \
	HID_USAGE_PAGE(HID_PAGE_PID),                                                           \
												\
	/* Set Effect */                                                                        \
	HID_USAGE(PID_SET_EFFECT_REPORT), HID_COLLECTION(HID_COLLECTION_LOGICAL),               \
		HID_REPORT_ID(HID_PID_ID_SET_EFFECT),                                           \
		PID_BLOCK_INDEX_ITEMS, HID_OUTPUT(PID_DATA_VAR_ABS),                            \
		PID_EFFECT_TYPE_ITEMS(HID_OUTPUT),                                              \
		HID_USAGE(PID_DURATION), HID_USAGE(PID_TRIGGER_REPEAT_INT),                     \
		HID_USAGE(PID_START_DELAY), PID_LOGICAL_0_65535, PID_UNIT_MS,                   \
		HID_REPORT_SIZE(16), HID_REPORT_COUNT(3), HID_OUTPUT(PID_DATA_VAR_ABS),         \
		PID_UNIT_NONE,                                                                  \
		HID_USAGE(PID_GAIN), PID_LOGICAL_0_255, HID_REPORT_SIZE(8),                     \
		HID_REPORT_COUNT(1), HID_OUTPUT(PID_DATA_VAR_ABS),                              \
		HID_USAGE(PID_TRIGGER_BUTTON), HID_LOGICAL_MIN8(0), HID_LOGICAL_MAX8(8),        \
		HID_OUTPUT(PID_DATA_VAR_ABS),                                                   \
		HID_USAGE(PID_DIRECTION_ENABLE), HID_LOGICAL_MAX8(1), HID_REPORT_SIZE(1),       \
		HID_OUTPUT(PID_DATA_VAR_ABS),                                                   \
		HID_REPORT_SIZE(7), HID_OUTPUT(PID_CONST),                                      \
		HID_USAGE(PID_DIRECTION), HID_COLLECTION(HID_COLLECTION_LOGICAL),               \
			HID_USAGE_PAGE(HID_PAGE_ORDINAL), HID_USAGE(1),                         \
			PID_LOGICAL_0_255, HID_REPORT_SIZE(8), HID_OUTPUT(PID_DATA_VAR_ABS),    \
			HID_USAGE_PAGE(HID_PAGE_PID),                                           \
		HID_END_COLLECTION,                                                             \
	HID_END_COLLECTION,                                                                     \
												\
	/* Set Envelope */                                                                      \
	HID_USAGE(PID_SET_ENVELOPE_REPORT), HID_COLLECTION(HID_COLLECTION_LOGICAL),             \
		HID_REPORT_ID(HID_PID_ID_SET_ENVELOPE),                                         \
		PID_BLOCK_INDEX_ITEMS, HID_OUTPUT(PID_DATA_VAR_ABS),                            \
		HID_USAGE(PID_ATTACK_LEVEL), HID_USAGE(PID_FADE_LEVEL), PID_LOGICAL_0_255,      \
		HID_REPORT_COUNT(2), HID_OUTPUT(PID_DATA_VAR_ABS),                              \
		HID_USAGE(PID_ATTACK_TIME), HID_USAGE(PID_FADE_TIME), PID_LOGICAL_0_65535,      \
		PID_UNIT_MS, HID_REPORT_SIZE(16), HID_OUTPUT(PID_DATA_VAR_ABS), PID_UNIT_NONE,  \
	HID_END_COLLECTION,                                                                     \
												\
	/* Set Periodic */                                                                      \
	HID_USAGE(PID_SET_PERIODIC_REPORT), HID_COLLECTION(HID_COLLECTION_LOGICAL),             \
		HID_REPORT_ID(HID_PID_ID_SET_PERIODIC),                                         \
		PID_BLOCK_INDEX_ITEMS, HID_OUTPUT(PID_DATA_VAR_ABS),                            \
		HID_USAGE(PID_MAGNITUDE), PID_LOGICAL_0_255, HID_OUTPUT(PID_DATA_VAR_ABS),      \
		HID_USAGE(PID_OFFSET), HID_LOGICAL_MIN8(0x80), HID_LOGICAL_MAX8(0x7F),          \
		HID_OUTPUT(PID_DATA_VAR_ABS),                                                   \
		HID_USAGE(PID_PHASE), PID_LOGICAL_0_255, HID_OUTPUT(PID_DATA_VAR_ABS),          \
		HID_USAGE(PID_PERIOD), PID_LOGICAL_0_65535, PID_UNIT_MS, HID_REPORT_SIZE(16),   \
		HID_OUTPUT(PID_DATA_VAR_ABS), PID_UNIT_NONE,                                    \
	HID_END_COLLECTION,                                                                     \
												\
	/* Effect Operation */                                                                  \
	HID_USAGE(PID_EFFECT_OP_REPORT), HID_COLLECTION(HID_COLLECTION_LOGICAL),                \
		HID_REPORT_ID(HID_PID_ID_EFFECT_OP),                                            \
		PID_BLOCK_INDEX_ITEMS, HID_OUTPUT(PID_DATA_VAR_ABS),                            \
		HID_USAGE(PID_EFFECT_OPERATION), HID_COLLECTION(HID_COLLECTION_LOGICAL),        \
			HID_USAGE(PID_OP_EFFECT_START), HID_USAGE(PID_OP_EFFECT_START_SOLO),    \
			HID_USAGE(PID_OP_EFFECT_STOP), HID_LOGICAL_MIN8(1),                     \
			HID_LOGICAL_MAX8(3), HID_OUTPUT(PID_DATA_ARRAY_ABS),                    \
		HID_END_COLLECTION,                                                             \
		HID_USAGE(PID_LOOP_COUNT), PID_LOGICAL_0_255, HID_OUTPUT(PID_DATA_VAR_ABS),     \
	HID_END_COLLECTION,                                                                     \
												\
	/* Block Free */                                                                        \
	HID_USAGE(PID_BLOCK_FREE_REPORT), HID_COLLECTION(HID_COLLECTION_LOGICAL),               \
		HID_REPORT_ID(HID_PID_ID_BLOCK_FREE),                                           \
		PID_BLOCK_INDEX_ITEMS, HID_OUTPUT(PID_DATA_VAR_ABS),                            \
	HID_END_COLLECTION,                                                                     \
												\
	/* Device Control: enable/disable actuators, stop all, reset, pause, continue */        \
	HID_USAGE(PID_DEVICE_CONTROL), HID_COLLECTION(HID_COLLECTION_LOGICAL),                  \
		HID_REPORT_ID(HID_PID_ID_DEVICE_CONTROL),                                       \
		HID_USAGE_MIN8(PID_DC_ENABLE_ACTUATORS), HID_USAGE_MAX8(PID_DC_CONTINUE),       \
		HID_LOGICAL_MIN8(1), HID_LOGICAL_MAX8(6), HID_REPORT_SIZE(8),                   \
		HID_REPORT_COUNT(1), HID_OUTPUT(PID_DATA_ARRAY_ABS),                            \
	HID_END_COLLECTION,                                                                     \
												\
	/* Device Gain */                                                                       \
	HID_USAGE(PID_DEVICE_GAIN_REPORT), HID_COLLECTION(HID_COLLECTION_LOGICAL),              \
		HID_REPORT_ID(HID_PID_ID_DEVICE_GAIN),                                          \
		HID_USAGE(PID_DEVICE_GAIN), PID_LOGICAL_0_255, HID_REPORT_SIZE(8),              \
		HID_REPORT_COUNT(1), HID_OUTPUT(PID_DATA_VAR_ABS),                              \
	HID_END_COLLECTION,                                                                     \
												\
	/* Create New Effect (feature, set) */                                                  \
	HID_USAGE(PID_CREATE_EFFECT_REPORT), HID_COLLECTION(HID_COLLECTION_LOGICAL),            \
		HID_REPORT_ID(HID_PID_ID_CREATE_EFFECT),                                        \
		PID_EFFECT_TYPE_ITEMS(HID_FEATURE),                                             \
	HID_END_COLLECTION,                                                                     \
												\
	/* Block Load (feature, get): result of the last Create New Effect */                   \
	HID_USAGE(PID_BLOCK_LOAD_REPORT), HID_COLLECTION(HID_COLLECTION_LOGICAL),               \
		HID_REPORT_ID(HID_PID_ID_BLOCK_LOAD),                                           \
		PID_BLOCK_INDEX_ITEMS, HID_FEATURE(PID_DATA_VAR_ABS),                           \
		HID_USAGE(PID_BLOCK_LOAD_STATUS), HID_COLLECTION(HID_COLLECTION_LOGICAL),       \
			HID_USAGE(PID_BLOCK_LOAD_SUCCESS), HID_USAGE(PID_BLOCK_LOAD_FULL),      \
			HID_USAGE(PID_BLOCK_LOAD_ERROR), HID_LOGICAL_MIN8(1),                   \
			HID_LOGICAL_MAX8(3), HID_FEATURE(PID_DATA_ARRAY_ABS),                   \
		HID_END_COLLECTION,                                                             \
		HID_USAGE(PID_RAM_POOL_AVAILABLE), PID_LOGICAL_0_65535, HID_REPORT_SIZE(16),    \
		HID_FEATURE(PID_DATA_VAR_ABS),                                                  \
	HID_END_COLLECTION,                                                                     \
												\
	/* Pool (feature, get) */                                                               \
	HID_USAGE(PID_POOL_REPORT), HID_COLLECTION(HID_COLLECTION_LOGICAL),                     \
		HID_REPORT_ID(HID_PID_ID_POOL),                                                 \
		HID_USAGE(PID_RAM_POOL_SIZE), PID_LOGICAL_0_65535, HID_REPORT_SIZE(16),         \
		HID_REPORT_COUNT(1), HID_FEATURE(PID_DATA_VAR_ABS),                             \
		HID_USAGE(PID_SIMULTANEOUS_MAX), PID_LOGICAL_0_255, HID_REPORT_SIZE(8),         \
		HID_FEATURE(PID_DATA_VAR_ABS),                                                  \
		HID_USAGE(PID_DEVICE_MANAGED_POOL), HID_USAGE(PID_SHARED_PARAM_BLOCKS),         \
		HID_LOGICAL_MAX8(1), HID_REPORT_SIZE(1), HID_REPORT_COUNT(2),                   \
		HID_FEATURE(PID_DATA_VAR_ABS),                                                  \
		HID_REPORT_SIZE(6), HID_REPORT_COUNT(1), HID_FEATURE(PID_CONST),                \
	HID_END_COLLECTION

/* Largest PID output report, for the OUT endpoint size check */
#define HID_PID_MAX_OUTPUT_LEN 13

/* Free all effects, gain to maximum, actuators on (also on USB disconnect). */
void hid_pid_reset(void);

/* PID output report (ID first). Returns -ENOENT if the ID isn't a PID report. */
int hid_pid_output(const uint8_t *buf, uint16_t len);

/* Feature reports (buf starts with the ID). get returns the length written. */
int hid_pid_set_feature(uint8_t id, const uint8_t *buf, uint16_t len);
int hid_pid_get_feature(uint8_t id, uint8_t *buf, uint16_t len);

/* Stop all effects (link lost: don't resume stale rumble on reconnect). */
void hid_pid_stop_all(void);

/*
 * Engine: rumble strength 0..255 at uptime now_ms, the sum of all playing
 * effects. Call every 1 ms; ends effects whose duration and loops are done.
 */
uint8_t hid_pid_strength(uint32_t now_ms);

#endif /* XBX_HID_PID_H_ */
