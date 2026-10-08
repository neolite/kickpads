#include "touch.h"

#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>

// Private ABI, layout as in OpenMultitouchSupport / TrackWeight.
typedef struct { float x, y; } MTPoint;
typedef struct { MTPoint pos, vel; } MTVector;
typedef struct {
  int frame;
  double timestamp;
  int identifier;
  int state;  // 1 starting, 2 hovering, 3 making, 4 touching, 5 breaking, 6 lingering, 7 leaving
  int fingerId;
  int handId;
  MTVector normalized;
  float total;
  float pressure;
  float angle;
  float majorAxis;
  float minorAxis;
  MTVector absolute;
  int field14, field15;
  float density;
} MTTouch;
_Static_assert(sizeof(MTTouch) == 96, "MTTouch layout mismatch");

typedef void *MTDeviceRef;
typedef int (*MTContactCallback)(MTDeviceRef, MTTouch *, int, double, int);

static CFArrayRef (*MTDeviceCreateList)(void);
static void (*MTRegisterContactFrameCallback)(MTDeviceRef, MTContactCallback);
static int (*MTDeviceStart)(MTDeviceRef, int);
static void (*MTDeviceStop)(MTDeviceRef);

static CFArrayRef devices;
static FrameHandler handler;
static void *handlerCtx;

enum { MAX_FINGERS = 16 };

static int onFrame(MTDeviceRef dev, MTTouch *touches, int n, double ts, int frame) {
  (void)dev, (void)ts, (void)frame;
  Finger out[MAX_FINGERS];
  int count = 0;
  for (int i = 0; i < n && count < MAX_FINGERS; i++) {
    const MTTouch *t = &touches[i];
    if (t->state != 3 && t->state != 4) continue;  // only fingers actually in contact
    out[count++] = (Finger){t->identifier, t->normalized.pos.x, t->normalized.pos.y, t->pressure, t->total};
  }
  handler(out, count, handlerCtx);
  return 0;
}

static bool loadSymbols(void) {
  void *h = dlopen("/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport", RTLD_NOW);
  if (!h) return fprintf(stderr, "dlopen: %s\n", dlerror()), false;
  MTDeviceCreateList = dlsym(h, "MTDeviceCreateList");
  MTRegisterContactFrameCallback = dlsym(h, "MTRegisterContactFrameCallback");
  MTDeviceStart = dlsym(h, "MTDeviceStart");
  MTDeviceStop = dlsym(h, "MTDeviceStop");
  return MTDeviceCreateList && MTRegisterContactFrameCallback && MTDeviceStart && MTDeviceStop;
}

bool touchStart(FrameHandler h, void *ctx) {
  if (!loadSymbols()) return false;
  handler = h;
  handlerCtx = ctx;
  devices = MTDeviceCreateList();
  CFIndex count = devices ? CFArrayGetCount(devices) : 0;
  for (CFIndex i = 0; i < count; i++) {
    MTDeviceRef d = (MTDeviceRef)CFArrayGetValueAtIndex(devices, i);
    MTRegisterContactFrameCallback(d, onFrame);
    MTDeviceStart(d, 0);
  }
  return count > 0;
}

void touchStop(void) {
  for (CFIndex i = 0; devices && i < CFArrayGetCount(devices); i++)
    MTDeviceStop((MTDeviceRef)CFArrayGetValueAtIndex(devices, i));
}
