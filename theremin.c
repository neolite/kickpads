// Dumb theremin: every finger on the trackpad is its own sine oscillator.
// x -> pitch (3 octaves, exponential), force -> volume. Run: ./theremin [--debug]

#include <AudioToolbox/AudioToolbox.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "touch.h"

enum { SAMPLE_RATE = 48000, VOICES = 5 };
static const float LOW_HZ = 110.f, OCTAVES = 3.f;
static const float PRESSURE_FULL = 400.f;  // observed range ~0..1200, 400 = firm press

// Written by the touch thread, read by the audio thread.
typedef struct {
  _Atomic float hz, amp;
} VoiceTarget;
static VoiceTarget targets[VOICES];

static int slotFinger[VOICES] = {-1, -1, -1, -1, -1};  // touch thread only
static bool debug;

static int slotFor(int fingerId) {
  for (int k = 0; k < VOICES; k++)
    if (slotFinger[k] == fingerId) return k;
  for (int k = 0; k < VOICES; k++)
    if (slotFinger[k] < 0) return slotFinger[k] = fingerId, k;
  return -1;  // more fingers than voices
}

static void onFrame(const Finger *f, int count, void *ctx) {
  (void)ctx;
  bool seen[VOICES] = {0};
  for (int i = 0; i < count; i++) {
    int k = slotFor(f[i].id);
    if (k < 0) continue;
    seen[k] = true;
    float p = fminf(f[i].pressure / PRESSURE_FULL, 1.f);
    atomic_store(&targets[k].hz, LOW_HZ * exp2f(f[i].x * OCTAVES));
    atomic_store(&targets[k].amp, 0.05f + 0.25f * powf(p, 0.6f));  // light touch still audible
    if (debug) printf("v%d x%.3f y%.3f p%6.1f size%.2f  ", k, f[i].x, f[i].y, f[i].pressure, f[i].size);
  }
  for (int k = 0; k < VOICES; k++)
    if (!seen[k] && slotFinger[k] >= 0) slotFinger[k] = -1, atomic_store(&targets[k].amp, 0.f);
  if (debug && count) putchar('\n');
}

static OSStatus render(void *ctx, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                       UInt32 frames, AudioBufferList *io) {
  (void)ctx, (void)flags, (void)ts, (void)bus;
  static float phase[VOICES], hz[VOICES] = {220, 220, 220, 220, 220}, amp[VOICES];
  float tHz[VOICES], tAmp[VOICES];
  for (int k = 0; k < VOICES; k++) {
    tHz[k] = atomic_load(&targets[k].hz);
    tAmp[k] = atomic_load(&targets[k].amp);
    if (amp[k] < 1e-4f && tAmp[k] > 0) hz[k] = tHz[k];  // new note: jump, don't glide from the old pitch
  }
  float *out = io->mBuffers[0].mData;
  for (UInt32 i = 0; i < frames; i++) {
    float s = 0;
    for (int k = 0; k < VOICES; k++) {
      hz[k] += (tHz[k] - hz[k]) * 0.002f;     // ~10 ms glide
      amp[k] += (tAmp[k] - amp[k]) * 0.001f;  // ~20 ms, kills clicks
      phase[k] += hz[k] / SAMPLE_RATE;
      phase[k] -= floorf(phase[k]);
      s += amp[k] * sinf(2.f * (float)M_PI * phase[k]);
    }
    out[i] = tanhf(s);  // soft clip when many fingers are down
  }
  for (UInt32 b = 1; b < io->mNumberBuffers; b++) memcpy(io->mBuffers[b].mData, out, frames * sizeof(float));
  return noErr;
}

static AudioUnit startAudio(void) {
  AudioComponentDescription desc = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                    kAudioUnitManufacturer_Apple, 0, 0};
  AudioUnit unit;
  if (AudioComponentInstanceNew(AudioComponentFindNext(NULL, &desc), &unit)) return NULL;
  AudioStreamBasicDescription fmt = {
      .mSampleRate = SAMPLE_RATE, .mFormatID = kAudioFormatLinearPCM,
      .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved,
      .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4, .mChannelsPerFrame = 2,
      .mBitsPerChannel = 32};
  AURenderCallbackStruct cb = {render, NULL};
  if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt) ||
      AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) ||
      AudioUnitInitialize(unit) || AudioOutputUnitStart(unit))
    return NULL;
  return unit;
}

static void quit(int sig) {
  (void)sig;
  touchStop();
  exit(0);
}

int main(int argc, char **argv) {
  debug = argc > 1 && strcmp(argv[1], "--debug") == 0;
  setvbuf(stdout, NULL, _IOLBF, 0);
  if (!startAudio()) return fprintf(stderr, "audio output failed\n"), 1;
  if (!touchStart(onFrame, NULL)) return fprintf(stderr, "no multitouch device\n"), 1;
  signal(SIGINT, quit);
  signal(SIGTERM, quit);
  printf("theremin: x = pitch, press harder = louder, up to %d fingers. Ctrl+C to quit.\n", VOICES);
  CFRunLoopRun();
}
