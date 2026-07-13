#ifndef AURORA_GFX_H
#define AURORA_GFX_H

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include "stdint.h"
#endif

#if !defined(NDEBUG) && !defined(AURORA_GFX_DEBUG_GROUPS)
#define AURORA_GFX_DEBUG_GROUPS
#endif

void push_debug_group(const char* label);
void pop_debug_group();

typedef struct {
  uint32_t queuedPipelines;
  uint32_t createdPipelines;
  uint32_t drawCallCount;
  uint32_t mergedDrawCallCount;
  uint32_t lastVertSize;
  uint32_t lastUniformSize;
  uint32_t lastIndexSize;
  uint32_t lastStorageSize;
  uint32_t lastTextureUploadSize;
} AuroraStats;

const AuroraStats* aurora_get_stats();
float aurora_get_fps();

/**
 * Draw-boundary telemetry for the GX color-channel count registers.
 *
 * XF and BP have distinct registers. Their raw values remain separate so
 * invalid retail states such as Eye Shredder's XF=12 / BP=4 are observable
 * without invoking host undefined behavior.
 */
typedef struct {
  uint32_t xfNumChansRaw;
  uint32_t bpNumChansRaw;
  uint32_t lastDrawMismatched;
  uint32_t mismatchLatched;
  uint32_t eyeShredderMismatchLatched;
  uint32_t lastMismatchXfNumChansRaw;
  uint32_t lastMismatchBpNumChansRaw;
  uint32_t reserved;
  uint64_t totalDrawCount;
  uint64_t mismatchDrawCount;
  uint64_t firstMismatchDraw;
  uint64_t lastMismatchDraw;
  uint64_t revision;
} AuroraGXChannelCountTelemetry;

/** Copies the latest telemetry snapshot into outTelemetry. */
void aurora_get_gx_channel_count_telemetry(AuroraGXChannelCountTelemetry* outTelemetry);

/** Clears draw counters and mismatch latches without changing live GX state. */
void aurora_reset_gx_channel_count_telemetry();

void aurora_enable_vsync(bool enabled);

#ifdef __cplusplus
}
#endif

#endif
