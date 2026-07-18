#ifndef AURORA_AURORA_H
#define AURORA_AURORA_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"
#endif

typedef enum {
  SAMPLER_BILINEAR,
  SAMPLER_AREA,
} AuroraSampler;

typedef enum {
  BACKEND_AUTO,
  BACKEND_D3D11,
  BACKEND_D3D12,
  BACKEND_METAL,
  BACKEND_VULKAN,
  BACKEND_OPENGL,
  BACKEND_OPENGLES,
  BACKEND_WEBGPU,
  BACKEND_NULL,
} AuroraBackend;

typedef enum {
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR,
  LOG_FATAL,
} AuroraLogLevel;

typedef enum {
  AURORA_FRAME_CAPTURE_IDLE,
  AURORA_FRAME_CAPTURE_PENDING,
  AURORA_FRAME_CAPTURE_SUCCEEDED,
  AURORA_FRAME_CAPTURE_INVALID_ARGUMENT,
  AURORA_FRAME_CAPTURE_UNSUPPORTED,
  AURORA_FRAME_CAPTURE_GPU_ERROR,
  AURORA_FRAME_CAPTURE_PNG_ERROR,
  AURORA_FRAME_CAPTURE_IO_ERROR,
} AuroraFrameCaptureStatus;

typedef struct {
  int32_t x;
  int32_t y;
} AuroraWindowPos;

typedef struct {
  uint32_t width;
  uint32_t height;

  /**
   * Width of the main GX framebuffer.
   */
  uint32_t fb_width;

  /**
   * Height of the main GX framebuffer.
   */
  uint32_t fb_height;

  /**
   * The size of the framebuffer used to present to the operating system.
   * May differ from fb_width if Aurora is instructed to force an aspect ratio or resolution configuration.
   */
  uint32_t native_fb_width;

  /**
   * The size of the framebuffer used to present to the operating system.
   * May differ from fb_height if Aurora is instructed to force an aspect ratio or resolution configuration.
   */
  uint32_t native_fb_height;
  float scale;
} AuroraWindowSize;

typedef struct SDL_Window SDL_Window;
typedef struct AuroraEvent AuroraEvent;

typedef void (*AuroraLogCallback)(AuroraLogLevel level, const char* module, const char* message, unsigned int len);
typedef void (*AuroraImGuiInitCallback)(const AuroraWindowSize* size);

#define MEM1_DEFAULT_SIZE (24 * 1024 * 1024)
#define ARAM_DEFAULT_SIZE (16 * 1024 * 1024)

typedef struct {
  const char* appName;
  const char* userPath;
  const char* cachePath;
  /* Renderer pipeline manifests and backend-compiled shader objects only. */
  const char* rendererCachePath;
  const char* resourcesPath;
  AuroraBackend desiredBackend;
  uint32_t msaa;
  uint16_t maxTextureAnisotropy;
  bool vsync;
  bool startFullscreen;
  /*
   * Create a presentation-capable window without showing it. Frames continue
   * to execute while initially hidden until aurora_show_window() succeeds.
   */
  bool startHidden;
  bool allowJoystickBackgroundEvents;
  bool pauseOnFocusLost;
  bool allowTextureDumps;
  bool allowCpuAdapter;
  /*
   * Execute and submit emulated GPU work without acquiring or presenting a host
   * swapchain image. Intended for callers running simulation-only workloads.
   */
  bool disablePresentation;
  /*
   * Compile every pipeline before accepting the draw that first references it.
   * This may increase wall-clock time, but prevents frames with omitted draws.
   */
  bool blockOnPipelineCompilation;
  int32_t windowPosX;
  int32_t windowPosY;
  uint32_t windowWidth;
  uint32_t windowHeight;
  void* iconRGBA8;
  uint32_t iconWidth;
  uint32_t iconHeight;
  AuroraLogCallback logCallback;
  AuroraLogLevel logLevel;
  AuroraImGuiInitCallback imGuiInitCallback;

  /*
   * The size of the GameCube's main memory, or MEM1 on the Wii.
   * Note that it will not be allocated at the exact 0x80000000 address, as that cannot be guaranteed.
   * This can be set to 0 to disable allocating this region.
   */
  uint32_t mem1Size;

  /*
   * The size of the GameCube's ARAM, or MEM2 on the Wii.
   * This can be set to 0 to disable allocating this region.
   */
  uint32_t mem2Size;
} AuroraConfig;

typedef struct {
  AuroraBackend backend;
  const char* userPath;
  const char* cachePath;
  SDL_Window* window;
  AuroraWindowSize windowSize;
} AuroraInfo;

AuroraInfo aurora_initialize(int argc, char* argv[], const AuroraConfig* config);
void aurora_shutdown();
const AuroraEvent* aurora_update();
bool aurora_begin_frame();
/*
 * Begins a frame while preserving the existing EFB color and depth contents.
 * This permits UI-only updates over the last rendered game frame.
 */
bool aurora_begin_retained_frame();
void aurora_end_frame();
/*
 * Captures the resolved EFB produced by the next aurora_end_frame call. The
 * requested output size is generated on the CPU after readback. A successful
 * request makes aurora_end_frame wait until the PNG has been atomically
 * written, so the status is terminal when aurora_end_frame returns.
 *
 * Capture is unavailable on the null backend. The UTF-8 output path is copied
 * by this call and its parent directory must already exist.
 */
bool aurora_capture_next_frame_png(const char* path, uint32_t width, uint32_t height);
AuroraFrameCaptureStatus aurora_get_frame_capture_status();
/* Returns the full error length, excluding the terminating null byte. */
size_t aurora_copy_frame_capture_error(char* destination, size_t capacity);
/* Must be called from Aurora's main SDL/window thread. */
bool aurora_show_window();
/*
 * Enables or suppresses host swapchain presentation without changing the
 * active graphics backend or discarding emulated GPU work. Must be called on
 * Aurora's main thread. A caller enabling presentation remains responsible
 * for showing the window.
 */
void aurora_set_presentation_enabled(bool enabled);

void aurora_set_log_level(AuroraLogLevel level);
void aurora_set_pause_on_focus_lost(bool value);
void aurora_set_background_input(bool value);
void aurora_set_resampler(AuroraSampler sampler);
void aurora_set_automation_input_quarantine(bool value);
bool aurora_get_automation_input_quarantine();

AuroraBackend aurora_get_backend();
const AuroraBackend* aurora_get_available_backends(size_t* count);

#ifdef __cplusplus
}
#endif

#endif
