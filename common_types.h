#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Holophonor loop states */
enum holo_state
{
  HOLO_STATE_IDLE,
  HOLO_STATE_RECORDING,
  HOLO_STATE_PLAYING,
  HOLO_STATE_STOPPED
};

#endif /* COMMON_TYPES_H */
