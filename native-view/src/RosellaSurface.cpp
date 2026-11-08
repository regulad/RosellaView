/*
 * RosellaSurface: a native UI component that shows the mirrored video. It
 * takes the sender's H.264 straight from the receiver (rosella_video.h),
 * decodes it with the OS's hardware decoder (Decoder.h), and renders it with
 * its own GLES shader into a surface inside the app's window. Unlike Vega's
 * MSE player, nothing on this path clips full-range video (DECISIONS.md D9,
 * D11, D12).
 *
 * Flow: makeView → IViewModule::makeMediaSurfaceView (a hole-punched surface
 * inside the app's window); onSurfaceCreated (UI thread) →
 * IKeplerMediaSurfaceProvider::getMediaSurface(). Then, on our own thread:
 * IGraphicsModule → display controller → IGpuDevice (OpenGL ES) → graphics
 * buffers (ARGB8888, scan-out + GPU write) → createOffscreenSurface → IContext
 * makeCurrent; attach to the receiver; then per frame: decode → de-tile →
 * upload → draw → IMediaSurface::presentBuffer.
 *
 * Why not makeGLSurfaceView + createScreenSurface(window handle)? An app's
 * native component can only reach createDisplayControllerForSystemClients
 * (ForApp needs the IComponentInstance, which the RN runtime doesn't hand out),
 * and a GPU device from that controller can't make the UI toolkit's window
 * current: "Make Current Surface Wrong!" for every config (tried on device).
 * Offscreen buffers don't depend on the display connection.
 *
 * Timing: each frame is shown `latency` ms after the sender captured it (its
 * capture time on our clock, from libairplay's NTP sync). JS sets the latency
 * (Receiver.setVideoLatency) so the picture matches the audio player.
 *
 * Status lines go to JS as "native_status" receiver events, through the
 * bridge. (onStatus view events emitted from here never reached JS.) JS
 * falls back to MSE if the surface doesn't attach to the receiver, which it
 * does only once the decoder works, or detaches during a session.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <apmf/apmf_base.h>
#include <apmf/array.h>
#include <apmf/component.h>
#include <apmf/data_object.h>
#include <apmf/native_pointer.h>
#include <apmf/process.h>
#include <apmf/ptr.h>
#include <apmf/string.h>
#include <apmf/string_view.h>

#include <apmf/iface/com/amazon/apmf/IStandard.h>
#include <apmf/iface/com/amazon/apmf/ISubscription.h>
#include <apmf/iface/com/amazon/apmf/InvalidArgumentError.h>
#include <apmf/iface/com/amazon/kepler/buffer/IBufferGroup.h>
#include <apmf/iface/com/amazon/kepler/graphics/BufferMime.h>
#include <apmf/iface/com/amazon/kepler/graphics/BufferPixelFormat.h>
#include <apmf/iface/com/amazon/kepler/graphics/BufferUsage.h>
#include <apmf/iface/com/amazon/kepler/graphics/CompressionPolicy.h>
#include <apmf/iface/com/amazon/kepler/graphics/ContextConfigAttributes.h>
#include <apmf/iface/com/amazon/kepler/graphics/IContext.h>
#include <apmf/iface/com/amazon/kepler/graphics/IContextConfig.h>
#include <apmf/iface/com/amazon/kepler/graphics/IDisplayController.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGlGpuDevice.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGlImage.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGpuDevice.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGraphicsModule.h>
#include <apmf/iface/com/amazon/kepler/graphics/IBufferReleaseListener.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGraphicsSurface.h>
#include <apmf/iface/com/amazon/kepler/graphics/IMediaSurface.h>
#include <apmf/iface/com/amazon/kepler/graphics/PresentationMode.h>
#include <apmf/iface/com/amazon/kepler/graphics/PresentResult.h>
#include <apmf/iface/com/amazon/kepler/graphics/RenderingCapabilities.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IContext.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IContextDelegate.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IFrame.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/ILayout.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/ILayoutDelegate.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IViewManager.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IViewManagerFactory.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/IKeplerMediaSurfaceProvider.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/ISurfaceView.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/ISurfaceViewListener.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/IView.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/IViewModule.h>

#include "AudioOut.h"
#include "Decoder.h"
#include "gles2.h"
#include "rosella_video.h"

namespace rosella {

using apmf::ApmfBase;
using apmf::Ptr;
using apmf::StringView;
using apmf::View;
namespace kb = apmf::iface::com::amazon::kepler::buffer;
namespace kg = apmf::iface::com::amazon::kepler::graphics;
namespace rx = apmf::iface::com::amazon::kepler::uitoolkit::react;
namespace uv = apmf::iface::com::amazon::kepler::uitoolkit::view;

// The render target: the display's size; the surface scales it to the view.
constexpr int kTargetWidth = 1920;
constexpr int kTargetHeight = 1080;
constexpr int kTargets = 4;
// Feed the decoder this long before a frame is due (it takes ~4 ms; the
// first picture after a new decoder, up to ~75 ms, is shown late).
constexpr uint64_t kFeedAheadNs = 30'000'000;
// Decoded pictures waiting to be shown hold the decoder's output buffers.
constexpr size_t kMaxReady = 3;
// More waiting access units than this (seconds of video) means we're far
// behind: start over at the next keyframe.
constexpr size_t kMaxWaiting = 300;
// A picture shown this much after it was due counts as late.
constexpr uint64_t kLateNs = 50'000'000;
// No picture for this long while being fed at least kStallFrames: the decoder
// is stuck. (It holds back its newest picture until the next access unit, so
// a sender that has gone quiet leaves one waiting; that isn't a stall.)
constexpr uint64_t kStallNs = 2'000'000'000;
constexpr int kStallFrames = 10;
// The latency follows JS's target at this rate, so a change speeds the
// picture up or slows it down by at most 10% instead of freezing or skipping.
// (NATIVE_SLEW_MS_PER_S in app/src/NativeVideo.ts.)
constexpr double kSlewPerSecond = 0.1;
constexpr uint64_t kKeyframeEveryNs = 1'000'000'000;
constexpr uint64_t kReportEveryNs = 250'000'000;

static uint64_t realtimeNs() {
  timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (uint64_t)t.tv_sec * 1'000'000'000ull + (uint64_t)t.tv_nsec;
}

// ---------------------------------------------------------------------------
// The receiver's side of the bridge, looked up in libRosellaView.so.

struct Bridge {
  rosella_video_attach_fn attach = nullptr;
  rosella_video_request_keyframe_fn requestKeyframe = nullptr;
  rosella_video_latency_ms_fn latencyMs = nullptr;
  rosella_video_report_fn report = nullptr;
  rosella_video_status_fn status = nullptr;
  rosella_audio_attach_fn audioAttach = nullptr;  // optional (older receivers lack it)

  bool load() {
    void* lib = dlopen(ROSELLA_VIDEO_LIBRARY, RTLD_NOW | RTLD_NOLOAD);
    auto sym = [lib](char const* name) {
      void* p = lib ? dlsym(lib, name) : nullptr;
      return p ? p : dlsym(RTLD_DEFAULT, name);
    };
    attach = reinterpret_cast<rosella_video_attach_fn>(sym("rosella_video_attach"));
    requestKeyframe = reinterpret_cast<rosella_video_request_keyframe_fn>(sym("rosella_video_request_keyframe"));
    latencyMs = reinterpret_cast<rosella_video_latency_ms_fn>(sym("rosella_video_latency_ms"));
    report = reinterpret_cast<rosella_video_report_fn>(sym("rosella_video_report"));
    status = reinterpret_cast<rosella_video_status_fn>(sym("rosella_video_status"));
    audioAttach = reinterpret_cast<rosella_audio_attach_fn>(sym("rosella_audio_attach"));
    return attach && requestKeyframe && latencyMs && report && status;
  }
};

// ---------------------------------------------------------------------------
// Shared state between the UI thread (view callbacks), libairplay's mirroring
// thread (access units in) and the render thread.

struct AccessUnit {
  std::vector<uint8_t> data;
  uint64_t senderNs = 0, localNs = 0;
  bool idr = false;
  unsigned width = 0, height = 0;  // from its SPS, if it has one
  bool fullRange = false;
  unsigned matrix = 2;
};

struct SurfaceState {
  std::mutex mutex;
  std::condition_variable wake;
  std::thread thread;
  std::atomic<rosella_video_status_fn> statusSink{nullptr};  // set once the bridge is found
  bool stop = false;
  // From the mirroring thread.
  std::deque<AccessUnit> units;
  bool reset = false;

  void status(std::string const& message) {
    if (auto sink = statusSink.load()) sink(("RosellaSurface: " + message).c_str());
  }
};

static void onFrame(void* ctx, rosella_video_frame const* f) {
  auto* state = static_cast<SurfaceState*>(ctx);
  AccessUnit au;
  au.data.assign(f->data, f->data + f->len);
  au.senderNs = f->sender_ns;
  // The capture time on our clock, unless the NTP sync isn't there yet.
  uint64_t const now = realtimeNs();
  uint64_t const off = f->local_ns > now ? f->local_ns - now : now - f->local_ns;
  au.localNs = f->local_ns && off < 5'000'000'000ull ? f->local_ns : now;
  au.idr = f->idr != 0;
  au.width = f->width;
  au.height = f->height;
  au.fullRange = f->full_range != 0;
  au.matrix = f->matrix;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->units.size() >= kMaxWaiting) {
      state->units.clear();
      state->reset = true;
    }
    state->units.push_back(std::move(au));
  }
  state->wake.notify_all();
}

static void onReset(void* ctx) {
  auto* state = static_cast<SurfaceState*>(ctx);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->units.clear();
    state->reset = true;
  }
  state->wake.notify_all();
}

// SPS (7) and PPS (8) NAL units of an access unit, with their start codes:
// replayed ahead of a keyframe that comes without them, for a new decoder.
static std::vector<uint8_t> parameterSets(std::vector<uint8_t> const& au) {
  std::vector<uint8_t> out;
  size_t const n = au.size();
  size_t i = 0;
  while (i + 3 < n) {
    if (!(au[i] == 0 && au[i + 1] == 0 && au[i + 2] == 1)) {
      i++;
      continue;
    }
    size_t const start = i + 3;
    size_t end = start;
    while (end + 3 <= n && !(au[end] == 0 && au[end + 1] == 0 && (au[end + 2] == 1 || au[end + 2] == 0))) end++;
    if (end + 3 > n) end = n;
    int const type = au[start] & 0x1f;
    if (type == 7 || type == 8) {
      static uint8_t const code[4] = {0, 0, 0, 1};
      out.insert(out.end(), code, code + 4);
      out.insert(out.end(), au.begin() + start, au.begin() + end);
    }
    i = end;
  }
  return out;
}

// ---------------------------------------------------------------------------
// GL.

#define X(ret, name, params)      \
  using name##_t = ret(*) params; \
  static name##_t name;
ROSELLA_GL_FUNCTIONS(X)
#undef X

static bool loadGl(View<kg::IGpuDevice> gpu, SurfaceState& state) {
  void* lib = nullptr;
  auto lookup = [&](char const* name) -> void* {
    void* p = nullptr;
    try {
      p = apmf::FromNativePointer<void*>(gpu->getExtensionAPI(name));
    } catch (std::exception const&) {
    }
    if (p) return p;
    if (!lib) lib = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    return lib ? dlsym(lib, name) : nullptr;
  };
  bool ok = true;
#define X(ret, name, params)                                    \
  name = reinterpret_cast<name##_t>(lookup(#name));             \
  if (!name) {                                                  \
    state.status(std::string("GL function missing: ") + #name); \
    ok = false;                                                 \
  }
  ROSELLA_GL_FUNCTIONS(X)
#undef X
  return ok;
}

static char const* const kVertexShader = R"(
attribute vec2 aPos;
varying vec2 vTex;
void main() {
  // Texture row 0 is the top of the picture, and so is row 0 (GL y = 0) of
  // the scan-out buffer we render into: no flip.
  vTex = vec2(aPos.x * 0.5 + 0.5, aPos.y * 0.5 + 0.5);
  gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// NV12 to RGB with the stream's own range and matrix (uniforms set by
// setColour). Centre chroma on 128/255, not 0.5, or neutral chroma tints.
static char const* const kFragmentShader = R"(
precision highp float;
varying vec2 vTex;
uniform sampler2D uY;
uniform sampler2D uUV;
uniform vec2 uScale;
uniform vec2 uBias;
uniform vec2 uCMax;
uniform float uYOffset;
uniform float uYGain;
uniform float uCGain;
uniform vec4 uM;
void main() {
  // Texel centres of the visible picture only: the decoder pads it to whole
  // blocks with zeros (green), which bilinear filtering would blend in.
  vec2 t = uBias + vTex * uScale;
  float y = (texture2D(uY, t).r - uYOffset) * uYGain;
  vec2 c = (texture2D(uUV, min(t, uCMax)).ra - vec2(128.0 / 255.0)) * uCGain;
  float r = y + uM.x * c.y;
  float g = y - uM.y * c.x - uM.z * c.y;
  float b = y + uM.w * c.x;
  gl_FragColor = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
}
)";

static GLuint compile(GLenum type, char const* source, SurfaceState& state) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512] = {0};
    glGetShaderInfoLog(shader, sizeof log - 1, nullptr, log);
    state.status(std::string("shader compile failed: ") + log);
    return 0;
  }
  return shader;
}

static Ptr<kg::IContextConfig> makeConfig(View<kg::IGpuDevice> gpu) {
  return gpu->createContextConfig(apmf::MakeBuilderFor<kg::ContextConfigAttributes>()
                                      ->numRedBits(8)
                                      ->numGreenBits(8)
                                      ->numBlueBits(8)
                                      ->numAlphaBits(8)
                                      ->numDepthBits(-1)
                                      ->numStencilBits(-1)
                                      ->numSamplesPerPixel(-1)
                                      ->needPackedDepthStencilBuffer(false)
                                      ->presentationMode(kg::PresentationMode::SYSTEM_DEFAULT)
                                      ->compressionPolicy(kg::CompressionPolicy::SYSTEM_DEFAULT)
                                      ->isLossyCompressionOk(false)
                                      ->nativeConfigAttributes({})
                                      ->build()
                                      .QueryInterface<kg::ContextConfigAttributes>());
}

// The display hands each presented buffer back when it's done scanning it
// out; until then we don't draw into it.
struct ReleaseTracker : ApmfBase<ReleaseTracker, kg::IBufferReleaseListener> {
  void onBufferRelease(View<kb::IBufferGroup> group) {
    uint64_t const id = group->getUniqueId();
    for (int i = 0; i < kTargets; i++) {
      if (ids[i] == id) busy[i] = false;
    }
  }
  uint64_t ids[kTargets] = {0};
  std::atomic<bool> busy[kTargets] = {};
};

// The GL objects the video is drawn with.
struct Renderer {
  Ptr<kb::IBufferGroup> buffers[kTargets];
  Ptr<kg::IGlImage> images[kTargets];
  GLuint fbos[kTargets] = {0};
  GLuint program = 0;
  GLuint tex[2] = {0, 0};
  int texStride = 0, texRows = 0;
  GLint uScale = -1, uBias = -1, uCMax = -1, uYOffset = -1, uYGain = -1, uCGain = -1, uM = -1;
  std::vector<uint8_t> y, uv;  // de-tiled planes

  // BT.709 unless the stream says BT.601 (5, 6) or BT.2020 (9, 10); full or
  // limited (16-235 luma, 16-240 chroma) range.
  void setColour(bool fullRange, unsigned matrix, int height) {
    float kr = 0.2126f, kb = 0.0722f;
    if (matrix == 5 || matrix == 6 || (matrix == 2 && height > 0 && height < 720)) {
      kr = 0.299f;
      kb = 0.114f;
    } else if (matrix == 9 || matrix == 10) {
      kr = 0.2627f;
      kb = 0.0593f;
    }
    float const kg = 1.0f - kr - kb;
    glUniform4f(uM, 2.0f * (1.0f - kr), 2.0f * kb * (1.0f - kb) / kg, 2.0f * kr * (1.0f - kr) / kg, 2.0f * (1.0f - kb));
    glUniform1f(uYOffset, fullRange ? 0.0f : 16.0f / 255.0f);
    glUniform1f(uYGain, fullRange ? 1.0f : 255.0f / 219.0f);
    glUniform1f(uCGain, fullRange ? 1.0f : 255.0f / 224.0f);
  }
};

static bool setUpGl(SurfaceState& state, Ptr<kg::IGraphicsModule> const& graphics, Ptr<kg::IGpuDevice> const& gpu,
                    Ptr<kg::IContext> const& context, Ptr<kg::IContextConfig> const& config,
                    Ptr<kg::IGraphicsSurface>& surface, Renderer& r) {
  auto glGpu = gpu.TryQueryInterface<kg::IGlGpuDevice>();
  if (!glGpu) {
    state.status("GPU device has no IGlGpuDevice");
    return false;
  }
  auto usage = apmf::MakeBuilderFor<kg::BufferUsage>()
                   ->scanOut(true)
                   ->gpuWrite(true)
                   ->gpuRead(true)
                   ->cameraWrite(false)
                   ->cameraRead(false)
                   ->secured(false)
                   ->softwareReadOften(false)
                   ->softwareReadRarely(false)
                   ->softwareWriteOften(false)
                   ->softwareWriteRarely(false)
                   ->hardwareVideoDecoder(false)
                   ->hardwareVideoEncoder(false)
                   ->decrypt(false)
                   ->build()
                   .QueryInterface<kg::BufferUsage>();
  // Each buffer backs a texture (IGlImage) that is the colour attachment of
  // our own FBO. (An offscreen IGraphicsSurface over the buffer gets no FBO
  // here: getFrameBufferObject() is 0, status GL_FRAMEBUFFER_UNDEFINED; it
  // only serves to make the context current.)
  for (int i = 0; i < kTargets; i++) {
    r.buffers[i] = graphics->createGraphicsBuffer(kTargetWidth, kTargetHeight, kg::BufferPixelFormat::ARGB8888,
                                                  kg::BufferMime::NONE, usage);
  }
  surface = gpu->createOffscreenSurface(r.buffers[0], config);
  context->makeCurrent(surface);
  if (!loadGl(gpu, state)) return false;
  GLuint targets[kTargets];
  glGenTextures(kTargets, targets);
  glGenFramebuffers(kTargets, r.fbos);
  for (int i = 0; i < kTargets; i++) {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, targets[i]);
    r.images[i] = glGpu->createGlImage();
    r.images[i]->attachToCurrentContext(targets[i], GL_TEXTURE_2D, GL_TEXTURE2);
    r.images[i]->bindBuffer(r.buffers[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, r.fbos[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[i], 0);
    GLenum const status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      state.status("render target incomplete: FBO status " + std::to_string(status));
      return false;
    }
  }
  GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader, state);
  GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader, state);
  if (!vs || !fs) return false;
  r.program = glCreateProgram();
  glAttachShader(r.program, vs);
  glAttachShader(r.program, fs);
  glBindAttribLocation(r.program, 0, "aPos");
  glLinkProgram(r.program);
  GLint linked = 0;
  glGetProgramiv(r.program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[512] = {0};
    glGetProgramInfoLog(r.program, sizeof log - 1, nullptr, log);
    state.status(std::string("program link failed: ") + log);
    return false;
  }
  glUseProgram(r.program);
  glUniform1i(glGetUniformLocation(r.program, "uY"), 0);
  glUniform1i(glGetUniformLocation(r.program, "uUV"), 1);
  r.uScale = glGetUniformLocation(r.program, "uScale");
  r.uBias = glGetUniformLocation(r.program, "uBias");
  r.uCMax = glGetUniformLocation(r.program, "uCMax");
  r.uYOffset = glGetUniformLocation(r.program, "uYOffset");
  r.uYGain = glGetUniformLocation(r.program, "uYGain");
  r.uCGain = glGetUniformLocation(r.program, "uCGain");
  r.uM = glGetUniformLocation(r.program, "uM");
  r.setColour(false, 2, 1080);
  glGenTextures(2, r.tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (int i = 0; i < 2; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, r.tex[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  state.status(std::string("GL: ") + reinterpret_cast<char const*>(glGetString(GL_RENDERER)) + " / " +
               reinterpret_cast<char const*>(glGetString(GL_VERSION)));
  return true;
}

// Uploads the de-tiled picture and draws it letterboxed into target `index`.
static void draw(Renderer& r, int index, int width, int height, int stride) {
  int const rows = height, uvRows = (height + 1) / 2;
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, r.tex[0]);
  bool const resize = stride != r.texStride || rows != r.texRows;
  if (resize) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, stride, rows, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, r.y.data());
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, stride, rows, GL_LUMINANCE, GL_UNSIGNED_BYTE, r.y.data());
  }
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, r.tex[1]);
  if (resize) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, stride / 2, uvRows, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                 r.uv.data());
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, stride / 2, uvRows, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, r.uv.data());
  }
  r.texStride = stride;
  r.texRows = rows;
  float const sw = (float)stride, sh = (float)rows;
  glUniform2f(r.uScale, (width - 1) / sw, (rows - 1) / sh);
  glUniform2f(r.uBias, 0.5f / sw, 0.5f / sh);
  // The last chroma texel centre: chroma covers (width + 1) / 2 of stride / 2.
  glUniform2f(r.uCMax, (((width + 1) / 2) - 0.5f) / (sw / 2), (uvRows - 0.5f) / uvRows);

  // The largest box of the picture's shape that fits, centred.
  float const scale = std::min((float)kTargetWidth / width, (float)kTargetHeight / height);
  int const w = (int)(width * scale + 0.5f), h = (int)(height * scale + 0.5f);
  glBindFramebuffer(GL_FRAMEBUFFER, r.fbos[index]);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, kTargetWidth, kTargetHeight);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport((kTargetWidth - w) / 2, (kTargetHeight - h) / 2, w, h);
  static GLfloat const quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
  glEnableVertexAttribArray(0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glFinish();  // the buffer must be complete before the display reads it
}

// ---------------------------------------------------------------------------
// The render thread: receive, decode, schedule, draw.

struct Timing {
  uint64_t senderNs = 0, localNs = 0;
};

struct Ready {
  Picture picture;
  Timing timing;
};

static void renderThread(std::shared_ptr<SurfaceState> state, Ptr<kg::IMediaSurface> media) {
  Bridge bridge;
  if (!bridge.load()) return;  // nothing to report to; JS falls back to MSE
  state->statusSink = bridge.status;
  Renderer r;
  try {
    auto graphics =
        apmf::GetProcessObject()->getComponent("/com.amazon.kepler.graphics").TryQueryInterface<kg::IGraphicsModule>();
    if (!graphics) {
      state->status("no /com.amazon.kepler.graphics IGraphicsModule");
      return;
    }
    auto display = graphics->createDisplayControllerForSystemClients(nullptr);
    auto gpu = graphics->createGpuDeviceUsingOpenGlesApi(display);
    auto config = makeConfig(gpu);
    auto context = gpu->createContext(nullptr, config);
    Ptr<kg::IGraphicsSurface> surface;
    if (!setUpGl(*state, graphics, gpu, context, config, surface, r)) {
      state->status("GL setup failed");
      return;
    }

    Decoder decoder([state](std::string const& m) { state->status(m); });
    // A first decoder at the display's size proves the platform still offers
    // it; it's reopened at the stream's own size when its SPS arrives.
    if (!decoder.open(kTargetWidth, kTargetHeight)) {
      state->status("hardware decoder unavailable");
      return;
    }

    // The media surface doesn't keep its listener alive: we must, for as long
    // as frames can be released (a temporary crashed in libgwsi-okidl).
    auto releases = Ptr<ReleaseTracker>::Make();
    for (int i = 0; i < kTargets; i++) releases->ids[i] = r.buffers[i]->getUniqueId();
    auto releaseSubscription = media->addBufferReleaseListener(releases);

    rosella_video_consumer consumer{state.get(), onFrame, onReset};
    bridge.attach(&consumer);

    // Spike (branch spike/native-audio): audio to the OS's PCM output too.
    AudioOut audio([state](std::string const& m) { state->status(m); });
    rosella_audio_consumer audioConsumer{
        &audio, [](void* ctx, int16_t const* pcm, size_t frames, unsigned, unsigned, uint64_t senderNs) {
          static_cast<AudioOut*>(ctx)->push(pcm, frames, senderNs);
        }};
    if (bridge.audioAttach) {
      audio.start();
      bridge.audioAttach(&audioConsumer);
    }
    state->status("native video ready");

    std::deque<AccessUnit> feeding;       // due, waiting for a decoder input buffer
    std::map<uint64_t, Timing> inFlight;  // by pts
    std::deque<Ready> ready;
    std::vector<uint8_t> paramSets;  // the stream's latest SPS + PPS
    unsigned streamW = 0, streamH = 0;
    bool fullRange = false;
    unsigned matrix = 2;
    bool needIdr = true, freshDecoder = true, wantKeyframe = false;
    uint64_t lastPts = 0, lastKeyframeNs = 0, lastReportNs = 0, lastShownNs = 0;
    // When the oldest access unit still without a picture went in (0: none).
    // (Not the time since the last picture: senders stop sending while the
    // screen is still, and the first frame after that isn't a stall.)
    uint64_t waitingSinceNs = 0;
    int fedSinceOutput = 0;
    // The latency in effect, easing towards JS's (-1: not started).
    double latencyNsEased = -1;
    uint64_t lastLoopNs = realtimeNs();
    int targetIndex = 0;
    double uploadMs = 0;
    rosella_video_stats stats{};
    stats.starts = 1;

    auto dropReady = [&](size_t keep) {
      while (ready.size() > keep) {
        decoder.giveBack(ready.front().picture);
        ready.pop_front();
        stats.skipped++;
      }
    };
    auto restart = [&](char const* why) {
      state->status(std::string("decoder restart: ") + why);
      dropReady(0);
      inFlight.clear();
      decoder.close();
      needIdr = true;
      wantKeyframe = true;
      waitingSinceNs = 0;
      fedSinceOutput = 0;
    };

    for (;;) {
      uint64_t now = realtimeNs();
      double const targetNs = (double)std::max(0, bridge.latencyMs()) * 1e6;
      if (latencyNsEased < 0 || stats.shown == 0) {
        latencyNsEased = targetNs;  // nothing on screen yet to keep smooth
      } else {
        double const step = kSlewPerSecond * (double)(now - lastLoopNs);
        latencyNsEased += std::max(-step, std::min(step, targetNs - latencyNsEased));
      }
      lastLoopNs = now;
      uint64_t const latencyNs = (uint64_t)latencyNsEased;
      bool reset = false;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->stop) break;
        reset = state->reset;
        state->reset = false;
        while (!state->units.empty() && state->units.front().localNs + latencyNs <= now + kFeedAheadNs) {
          feeding.push_back(std::move(state->units.front()));
          state->units.pop_front();
        }
        stats.queued = (int)(state->units.size() + feeding.size());
      }
      if (reset) {
        feeding.clear();
        restart("the sender reset its stream");
      }

      // Feed the decoder whatever is due, from a keyframe on.
      while (!feeding.empty()) {
        AccessUnit& au = feeding.front();
        if (au.width) {
          paramSets = parameterSets(au.data);
          streamW = au.width;
          streamH = au.height;
        }
        if (streamW && (!decoder.isOpen() || decoder.width() != (int)streamW || decoder.height() != (int)streamH)) {
          if (decoder.isOpen()) restart("new picture size");
          if (!decoder.open((int)streamW, (int)streamH)) {
            stats.errors++;
            feeding.pop_front();
            wantKeyframe = true;
            continue;
          }
          stats.starts++;
          freshDecoder = true;
        } else if (!decoder.isOpen()) {
          feeding.pop_front();  // no SPS seen yet: nothing to open a decoder with
          stats.skipped++;
          wantKeyframe = true;
          continue;
        }
        if (needIdr && !au.idr) {
          feeding.pop_front();
          stats.skipped++;
          wantKeyframe = true;
          continue;
        }
        std::vector<uint8_t> withConfig;
        uint8_t const* data = au.data.data();
        size_t len = au.data.size();
        if (freshDecoder && !au.width && !paramSets.empty()) {
          withConfig = paramSets;
          withConfig.insert(withConfig.end(), au.data.begin(), au.data.end());
          data = withConfig.data();
          len = withConfig.size();
        }
        uint64_t const pts = std::max(lastPts + 1, au.senderNs / 1000);
        auto const result = decoder.feed(data, len, pts, au.idr, au.width != 0 || !withConfig.empty());
        if (result == Decoder::Feed::Busy) break;
        if (result == Decoder::Feed::Queued) {
          lastPts = pts;
          inFlight[pts] = Timing{au.senderNs, au.localNs};
          needIdr = false;
          freshDecoder = false;
          if (!waitingSinceNs) waitingSinceNs = realtimeNs();
          fedSinceOutput++;
          if (au.width) {
            fullRange = au.fullRange;
            matrix = au.matrix;
          }
        } else if (result == Decoder::Feed::TooBig) {
          state->status("access unit too big for the decoder: " + std::to_string(au.data.size()) + " bytes");
          needIdr = true;
          wantKeyframe = true;
          stats.errors++;
        } else {
          stats.errors++;
          restart("feeding failed");
        }
        feeding.pop_front();
      }

      // Collect decoded pictures.
      for (;;) {
        Picture picture;
        std::string error;
        auto const taken = decoder.take(picture, error);
        if (taken == Decoder::Take::None) break;
        if (taken == Decoder::Take::Error) {
          stats.errors++;
          state->status("decode error: " + error);
          restart("decode error");
          break;
        }
        stats.decoded++;
        uint64_t const outNs = realtimeNs();
        Timing timing{0, outNs};
        auto it = inFlight.find(picture.pts);
        if (it != inFlight.end()) timing = it->second;
        inFlight.erase(inFlight.begin(), inFlight.upper_bound(picture.pts));
        // Still waiting on later input: count from now.
        waitingSinceNs = inFlight.empty() ? 0 : outNs;
        fedSinceOutput = 0;
        ready.push_back(Ready{picture, timing});
        dropReady(kMaxReady);
      }

      // Show the newest picture that's due; skip older ones.
      now = realtimeNs();
      int due = -1;
      for (size_t i = 0; i < ready.size(); i++) {
        if (ready[i].timing.localNs + latencyNs <= now) due = (int)i;
      }
      if (due >= 0) {
        int target = -1;
        for (int k = 0; k < kTargets; k++) {
          int const i = (targetIndex + k) % kTargets;
          if (!releases->busy[i]) {
            target = i;
            break;
          }
        }
        // Never released for this long: the listener has gone quiet; reuse.
        if (target < 0 && now - lastShownNs > 100'000'000) target = targetIndex;
        if (target >= 0) {
          dropReady(ready.size() - (size_t)due);
          Ready shown = ready.front();
          ready.pop_front();
          Picture& p = shown.picture;
          // The decoder reports its padded size (816x1088 for 810x1080): show
          // the SPS's.
          if (decoder.width() > 0 && decoder.height() > 0) {
            p.width = std::min(p.width, decoder.width());
            p.height = std::min(p.height, decoder.height());
          }
          auto const t0 = std::chrono::steady_clock::now();
          size_t const ySize = (size_t)p.stride * p.height, uvSize = (size_t)p.stride * ((p.height + 1) / 2);
          if (r.y.size() != ySize) r.y.assign(ySize, 0);
          if (r.uv.size() != uvSize) r.uv.assign(uvSize, 128);
          bool const ok = p.width > 0 && p.height > 0 && decoder.detile(p, r.y.data(), r.uv.data());
          decoder.giveBack(p);
          if (ok) {
            r.setColour(fullRange, matrix, p.height);
            draw(r, target, p.width, p.height, p.stride);
            double const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            uploadMs = uploadMs ? uploadMs * 0.95 + ms * 0.05 : ms;
            releases->busy[target] = true;
            auto const result = media->presentBuffer(r.buffers[target], nullptr, nullptr, 0, 0);
            if (result.value != kg::PresentResult::SUBMITTED.value && result.value != kg::PresentResult::QUEUED.value) {
              releases->busy[target] = false;
              stats.errors++;
            }
            targetIndex = (target + 1) % kTargets;
            lastShownNs = realtimeNs();
            if (lastShownNs > shown.timing.localNs + latencyNs + kLateNs) stats.late++;
            stats.shown++;
            stats.shown_sender_ns = shown.timing.senderNs;
            stats.shown_local_ns = lastShownNs;
            stats.width = (unsigned)p.width;
            stats.height = (unsigned)p.height;
            if (stats.shown == 1) {
              state->status("first picture shown: " + std::to_string(p.width) + "x" + std::to_string(p.height) +
                            (fullRange ? " full" : " limited") + " range, matrix " + std::to_string(matrix));
            }
          }
        }
      }

      now = realtimeNs();
      if (waitingSinceNs && now - waitingSinceNs > kStallNs && fedSinceOutput >= kStallFrames) {
        restart("no pictures for 2 s");
      }
      if (wantKeyframe && now - lastKeyframeNs > kKeyframeEveryNs) {
        bridge.requestKeyframe();
        lastKeyframeNs = now;
        wantKeyframe = false;
      }
      if (now - lastReportNs > kReportEveryNs) {
        stats.upload_ms = uploadMs;
        bridge.report(&stats);
        lastReportNs = now;
      }

      // Sleep until something's due: the decoder has no output callback, so
      // poll it every 2 ms while pictures are in flight.
      uint64_t wakeNs = now + 20'000'000;
      if (!inFlight.empty() || !feeding.empty()) wakeNs = now + 2'000'000;
      if (!ready.empty()) wakeNs = std::min(wakeNs, ready.front().timing.localNs + latencyNs);
      {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->units.empty()) {
          uint64_t const dueNs = state->units.front().localNs + latencyNs;
          wakeNs = std::min(wakeNs, dueNs > kFeedAheadNs ? dueNs - kFeedAheadNs : 0);
        }
        if (wakeNs > now) {
          state->wake.wait_for(lock, std::chrono::nanoseconds(wakeNs - now),
                               [&] { return state->stop || state->reset; });
        }
      }
    }

    if (bridge.audioAttach) bridge.audioAttach(nullptr);  // waits out a call in progress
    audio.stop();
    bridge.attach(nullptr);  // waits out a frame call in progress
    dropReady(0);
    decoder.close();
    try {
      releaseSubscription->unsubscribe();
    } catch (std::exception const&) {
    }
    context->resetCurrent();
    state->status("render thread stopped after " + std::to_string(stats.shown) + " pictures");
  } catch (std::exception const& e) {
    bridge.attach(nullptr);
    state->status(std::string("render thread error: ") + e.what());
  }
}

// ---------------------------------------------------------------------------
// UI thread: surface lifecycle and status events.

struct SurfaceListener : ApmfBase<SurfaceListener, uv::ISurfaceViewListener> {
  explicit SurfaceListener(std::shared_ptr<SurfaceState> state) : state_(std::move(state)) {}

  void onSurfaceCreated(View<uv::ISurfaceView> surfaceView) {
    try {
      auto provider = Ptr<uv::ISurfaceView>(surfaceView).TryQueryInterface<uv::IKeplerMediaSurfaceProvider>();
      auto media = provider ? provider->getMediaSurface() : Ptr<kg::IMediaSurface>();
      if (!media) {
        state_->status("surface view has no media surface");
        return;
      }
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (!state_->thread.joinable()) {
        state_->stop = false;
        state_->thread = std::thread(renderThread, state_, media);
      }
    } catch (std::exception const& e) {
      state_->status(std::string("onSurfaceCreated error: ") + e.what());
    }
  }

  void onSurfaceUpdated(View<uv::ISurfaceView>) {}

  void onSurfaceDestroyed(View<uv::ISurfaceView>) {
    std::thread thread;
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->stop = true;
      thread = std::move(state_->thread);
    }
    state_->wake.notify_all();
    if (thread.joinable()) thread.join();
  }

  std::shared_ptr<SurfaceState> state_;
};

struct SurfaceViewManager : ApmfBase<SurfaceViewManager, rx::IViewManager, rx::IContextDelegate, rx::ILayoutDelegate> {
  explicit SurfaceViewManager(Ptr<uv::IViewModule> viewModule) : viewModule_(std::move(viewModule)) {}

  Ptr<uv::IView> makeView() {
    auto state = std::make_shared<SurfaceState>();
    auto listener = Ptr<SurfaceListener>::Make(state);
    return viewModule_->makeMediaSurfaceView(listener);
  }

  void updateContext(View<rx::IContext>) {}

  void updateLayout(View<uv::IView> view, View<rx::ILayout> layout) {
    view->setSize(layout->getContentFrame()->getSize());
  }

  Ptr<uv::IViewModule> viewModule_;
};

struct ViewManagerFactory : ApmfBase<ViewManagerFactory, rx::IViewManagerFactory> {
  Ptr<rx::IViewManager> makeViewManager(StringView name) {
    if (name == "RosellaSurface") {
      auto viewModule = apmf::GetProcessObject()
                            ->getComponent("/com.amazon.kepler.uitoolkit.view")
                            .TryQueryInterface<uv::IViewModule>();
      return Ptr<SurfaceViewManager>::Make(viewModule);
    }
    throw apmf::InvalidArgumentError("RosellaSurface: unknown view manager");
  }
};

}  // namespace rosella

APMF_COMPONENT("/xyz.regulad.rosellaview.surface", rosella::ViewManagerFactory);
