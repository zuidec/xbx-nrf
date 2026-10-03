/*
 * Controller pairing mode (docs/protocol.md, "Pairing mode"): entered by
 * holding Pair (pair-gpios) for PAIR_HOLD_MS, powering on with it held, or
 * booting unpaired; ends after PAIR_TIMEOUT_MS. The pairing indicator (the
 * "heavy" LED, on-board on the Pro Micro) blinks fast meanwhile.
 *
 * Sub-step 1: mode and indicator only; the radio exchange comes next.
 */

#ifndef XBX_PAIR_H_
#define XBX_PAIR_H_

#include <stdbool.h>
#include <stdint.h>

/* Set up the Pair input and indicator; enters pairing if !paired or Pair is held. */
int pair_init(bool paired);

/* Call every report tick (TX thread) with the ms elapsed since the last call. */
void pair_tick(uint32_t elapsed_ms);

/* Pairing mode active: the indicator LED belongs to pairing meanwhile. */
bool pair_active(void);

#endif /* XBX_PAIR_H_ */
