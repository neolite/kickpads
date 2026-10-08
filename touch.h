// Trackpad core: raw per-finger data from the private MultitouchSupport framework.
#pragma once
#include <stdbool.h>

typedef struct {
  int id;          // stable while the finger stays down
  float x, y;      // 0..1, origin bottom-left
  float pressure;  // raw force value (device units, ~0..several hundred)
  float size;      // contact area / capacitance
} Finger;

// Called on the multitouch thread for every frame (~100+ Hz), including empty frames on release.
typedef void (*FrameHandler)(const Finger *fingers, int count, void *ctx);

bool touchStart(FrameHandler handler, void *ctx);  // false if framework/devices unavailable
void touchStop(void);
