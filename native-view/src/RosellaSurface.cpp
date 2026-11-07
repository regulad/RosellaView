/*
 * RosellaSurface: a native UI component that owns a GL surface inside the
 * app's window and renders into it on its own thread.
 *
 * Proof of concept: it uploads a full-range NV12 frame (1280x720 Y, 640x360
 * interleaved UV) holding a grey step chart and converts it to RGB in our own
 * fragment shader, with the full-range BT.709 matrix. Top half: Y = 0, 4, 8,
 * 12, 16, 20, 24, 32; bottom half: Y = 223, 227, 231, 235, 239, 243, 247, 255;
 * UV = 128 throughout, so R = G = B = Y. If every bar is distinct on the TV,
 * nothing between this shader and the panel clips to video range.
 *
 * Flow: makeView → IViewModule::makeMediaSurfaceView (a hole-punched surface
 * inside the app's window); onSurfaceCreated (UI thread) →
 * IKeplerMediaSurfaceProvider::getMediaSurface(). Then, on our own thread:
 * IGraphicsModule → display controller → IGpuDevice (OpenGL ES) → graphics
 * buffers (ARGB8888, scan-out + GPU write) → createOffscreenSurface → IContext
 * makeCurrent → draw → IMediaSurface::presentBuffer.
 *
 * Why not makeGLSurfaceView + createScreenSurface(window handle)? An app's
 * native component can only reach createDisplayControllerForSystemClients
 * (ForApp needs the IComponentInstance, which the RN runtime doesn't hand out),
 * and a GPU device from that controller can't make the UI toolkit's window
 * current: "Make Current Surface Wrong!" for every config (tried on device).
 * Offscreen buffers don't depend on the display connection.
 *
 * Status goes to /scratch/rosella-surface.log (read it from the component
 * shell) and to JS as onStatus events.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <exception>
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
#include <apmf/iface/com/amazon/kepler/graphics/IFrameHint.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGlGpuDevice.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGlGraphicsSurface.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGlImage.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGpuDevice.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGraphicsModule.h>
#include <apmf/iface/com/amazon/kepler/graphics/IBufferReleaseListener.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGraphicsSurface.h>
#include <apmf/iface/com/amazon/kepler/graphics/IMediaSurface.h>
#include <apmf/iface/com/amazon/kepler/graphics/PresentationMode.h>
#include <apmf/iface/com/amazon/kepler/graphics/PresentResult.h>
#include <apmf/iface/com/amazon/kepler/graphics/RectI.h>
#include <apmf/iface/com/amazon/kepler/graphics/RenderingCapabilities.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IContext.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IContextDelegate.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IEventEmitter.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IFrame.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IJsObject.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IJsObjectBuilder.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/ILayout.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/ILayoutDelegate.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IPayloadDelegate.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IViewManager.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/react/IViewManagerFactory.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/IFrameEvent.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/IFrameEventListener.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/IKeplerMediaSurfaceProvider.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/ISurfaceView.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/ISurfaceViewListener.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/IView.h>
#include <apmf/iface/com/amazon/kepler/uitoolkit/view/IViewModule.h>

#include "gles2.h"

namespace rosella {

using apmf::ApmfBase;
using apmf::Ptr;
using apmf::String;
using apmf::StringView;
using apmf::View;
namespace kb = apmf::iface::com::amazon::kepler::buffer;
namespace kg = apmf::iface::com::amazon::kepler::graphics;
namespace rx = apmf::iface::com::amazon::kepler::uitoolkit::react;
namespace uv = apmf::iface::com::amazon::kepler::uitoolkit::view;

// ---------------------------------------------------------------------------
// The test frame: full-range NV12, neutral chroma.

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kBars = 8;
constexpr uint8_t kTopY[kBars] = {0, 4, 8, 12, 16, 20, 24, 32};
constexpr uint8_t kBottomY[kBars] = {223, 227, 231, 235, 239, 243, 247, 255};

static void makeFrame(std::vector<uint8_t>& y, std::vector<uint8_t>& uv) {
  y.resize(kWidth * kHeight);
  for (int row = 0; row < kHeight; row++) {
    uint8_t const* levels = row < kHeight / 2 ? kTopY : kBottomY;
    for (int col = 0; col < kWidth; col++) {
      y[row * kWidth + col] = levels[col * kBars / kWidth];
    }
  }
  uv.assign((kWidth / 2) * (kHeight / 2) * 2, 128);
}

// ---------------------------------------------------------------------------
// Shared state between the UI thread (view callbacks) and the GL thread.

// Development log, readable from the component shell (an app's syslog and
// stderr don't reach loggingctl).
static void fileLog(std::string const& message) {
  if (FILE* f = std::fopen("/scratch/rosella-surface.log", "a")) {
    std::fprintf(f, "%s\n", message.c_str());
    std::fclose(f);
  }
}

struct SurfaceState {
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::string> messages;  // to JS, drained on the UI thread
  std::thread thread;
  bool stop = false;

  void status(std::string message) {
    fileLog(message);
    std::lock_guard<std::mutex> lock(mutex);
    messages.push_back(std::move(message));
  }
};

// ---------------------------------------------------------------------------
// GL thread.

#define X(ret, name, params) using name##_t = ret(*) params; static name##_t name;
ROSELLA_GL_FUNCTIONS(X)
#undef X

static bool loadGl(View<kg::IGpuDevice> gpu, SurfaceState& state) {
  void* lib = nullptr;
  int fromDevice = 0, fromLib = 0;
  auto lookup = [&](char const* name) -> void* {
    void* p = nullptr;
    try {
      p = apmf::FromNativePointer<void*>(gpu->getExtensionAPI(name));
    } catch (std::exception const&) {
    }
    if (p) {
      fromDevice++;
      return p;
    }
    if (!lib) {
      lib = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    }
    p = lib ? dlsym(lib, name) : nullptr;
    if (p) {
      fromLib++;
    }
    return p;
  };
  bool ok = true;
#define X(ret, name, params)                                  \
  name = reinterpret_cast<name##_t>(lookup(#name));           \
  if (!name) {                                                \
    state.status(std::string("GL function missing: ") + #name); \
    ok = false;                                               \
  }
  ROSELLA_GL_FUNCTIONS(X)
#undef X
  state.status("GL functions: " + std::to_string(fromDevice) + " via getExtensionAPI, " +
               std::to_string(fromLib) + " via libGLESv2");
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

// Full-range BT.709: Y = Y'/255, Cb/Cr = (U/V - 128)/255; no 16..235 scaling.
// (Centre on 128/255, not 0.5, or neutral chroma tints R and B by +1.)
static char const* const kFragmentShader = R"(
precision highp float;
varying vec2 vTex;
uniform sampler2D uY;
uniform sampler2D uUV;
void main() {
  float y = texture2D(uY, vTex).r;
  vec2 c = texture2D(uUV, vTex).ra - vec2(128.0 / 255.0);
  float r = y + 1.5748 * c.y;
  float g = y - 0.1873 * c.x - 0.4681 * c.y;
  float b = y + 1.8556 * c.x;
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

static std::string hex(unsigned v) {
  char b[16];
  std::snprintf(b, sizeof b, "%x", v);
  return b;
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

constexpr int kBuffers = 3;

// The display hands each presented buffer back when it's done scanning it out:
// evidence the frames reached the screen.
struct ReleaseCounter : ApmfBase<ReleaseCounter, kg::IBufferReleaseListener> {
  explicit ReleaseCounter(std::shared_ptr<SurfaceState> state) : state_(std::move(state)) {}
  void onBufferRelease(View<kb::IBufferGroup>) {
    int const n = ++released_;
    if (n == 1 || n == 10 || n == 100) {
      state_->status("display released " + std::to_string(n) + " buffer(s)");
    }
  }
  std::shared_ptr<SurfaceState> state_;
  std::atomic<int> released_{0};
};

static void renderThread(std::shared_ptr<SurfaceState> state, Ptr<kg::IMediaSurface> media, int width,
                         int height) {
  try {
    auto graphics = apmf::GetProcessObject()
                        ->getComponent("/com.amazon.kepler.graphics")
                        .TryQueryInterface<kg::IGraphicsModule>();
    if (!graphics) {
      state->status("no /com.amazon.kepler.graphics IGraphicsModule");
      return;
    }
    auto display = graphics->createDisplayControllerForSystemClients(nullptr);
    auto gpu = graphics->createGpuDeviceUsingOpenGlesApi(display);
    state->status("GPU device created (rendering capabilities " +
                  std::to_string(gpu->getRenderingCapabilities().value) + ")");
    auto config = makeConfig(gpu);

    // Render targets: scan-out buffers the GPU writes, presented on the view's
    // media surface. A few, so one can be on screen while we draw another.
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
    Ptr<kb::IBufferGroup> buffers[kBuffers];
    Ptr<kg::IGlImage> images[kBuffers];
    GLuint targets[kBuffers] = {0};
    GLuint fbos[kBuffers] = {0};
    auto context = gpu->createContext(nullptr, config);
    auto glGpu = gpu.TryQueryInterface<kg::IGlGpuDevice>();
    if (!glGpu) {
      state->status("GPU device has no IGlGpuDevice");
      return;
    }
    bool complete = false;
    Ptr<kg::IGraphicsSurface> surface;
    for (auto const& format : {kg::BufferPixelFormat::ARGB8888, kg::BufferPixelFormat::XRGB8888}) {
      std::string const name(format.value.data(), format.value.size());
      try {
        for (int i = 0; i < kBuffers; i++) {
          buffers[i] = graphics->createGraphicsBuffer(width, height, format, kg::BufferMime::NONE, usage);
        }
        surface = gpu->createOffscreenSurface(buffers[0], config);
        context->makeCurrent(surface);
        if (!fbos[0]) {
          if (!loadGl(gpu, *state)) {
            return;
          }
          glGenTextures(kBuffers, targets);
          glGenFramebuffers(kBuffers, fbos);
        }
        GLenum status = 0;
        for (int i = 0; i < kBuffers; i++) {
          glActiveTexture(GL_TEXTURE2);
          glBindTexture(GL_TEXTURE_2D, targets[i]);
          if (images[i]) {
            images[i]->unbindCurrentBuffer();
          } else {
            images[i] = glGpu->createGlImage();
            images[i]->attachToCurrentContext(targets[i], GL_TEXTURE_2D, GL_TEXTURE2);
          }
          images[i]->bindBuffer(buffers[i]);
          glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
          glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[i], 0);
          status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
          if (status != GL_FRAMEBUFFER_COMPLETE) {
            break;
          }
        }
        state->status(name + ": " + std::to_string(kBuffers) + " scan-out buffers " + std::to_string(width) + "x" +
                      std::to_string(height) + " as GL images, FBO status 0x" + hex(status));
        if (status == GL_FRAMEBUFFER_COMPLETE) {
          complete = true;
          break;
        }
      } catch (std::exception const& e) {
        state->status(name + ": failed: " + e.what());
      }
    }
    if (!complete) {
      state->status("no renderable buffer format");
      return;
    }

    state->status(std::string("GL: ") + reinterpret_cast<char const*>(glGetString(GL_VENDOR)) + " / " +
                  reinterpret_cast<char const*>(glGetString(GL_RENDERER)) + " / " +
                  reinterpret_cast<char const*>(glGetString(GL_VERSION)));

    GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader, *state);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader, *state);
    if (!vs || !fs) {
      return;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPos");
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
      char log[512] = {0};
      glGetProgramInfoLog(program, sizeof log - 1, nullptr, log);
      state->status(std::string("program link failed: ") + log);
      return;
    }
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uY"), 0);
    glUniform1i(glGetUniformLocation(program, "uUV"), 1);

    std::vector<uint8_t> yPlane, uvPlane;
    makeFrame(yPlane, uvPlane);
    GLuint tex[2];
    glGenTextures(2, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < 2; i++) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, tex[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, kWidth, kHeight, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                 yPlane.data());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, kWidth / 2, kHeight / 2, 0, GL_LUMINANCE_ALPHA,
                 GL_UNSIGNED_BYTE, uvPlane.data());
    GLenum err = glGetError();
    state->status("textures uploaded (glGetError=" + std::to_string(err) + ")");

    // The media surface doesn't keep its listener alive: we must, for as long
    // as frames can be released (a temporary here crashed in libgwsi-okidl).
    auto releaseCounter = Ptr<ReleaseCounter>::Make(state);
    auto releaseSubscription = media->addBufferReleaseListener(releaseCounter);
    static GLfloat const quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    int frames = 0;
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (state->stop) {
          break;
        }
      }
      int const index = frames % kBuffers;
      glBindFramebuffer(GL_FRAMEBUFFER, fbos[index]);
      glViewport(0, 0, width, height);
      glDisable(GL_BLEND);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_SCISSOR_TEST);
      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glUseProgram(program);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
      glEnableVertexAttribArray(0);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

      if (frames == 0) {
        // Read back the centre of every bar: our shader's output, before the
        // compositor and display pipeline touch it.
        int const w = width, h = height;
        std::string top = "readback top (R/G/B):", bottom = "readback bottom (R/G/B):";
        for (int i = 0; i < kBars; i++) {
          int const x = (2 * i + 1) * w / (2 * kBars);
          uint8_t px[4] = {0, 0, 0, 0};
          glReadPixels(x, h / 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);  // buffer row 0 is the top
          top += " " + std::to_string(px[0]) + "/" + std::to_string(px[1]) + "/" + std::to_string(px[2]);
          glReadPixels(x, h * 3 / 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
          bottom += " " + std::to_string(px[0]) + "/" + std::to_string(px[1]) + "/" + std::to_string(px[2]);
        }
        state->status(top);
        state->status(bottom);
      }

      GLenum const drawError = glGetError();
      glFinish();  // the buffer must be complete before the display reads it
      auto result = media->presentBuffer(buffers[index], nullptr, nullptr, 0, 0);
      if (frames < 3 || (result.value != kg::PresentResult::SUBMITTED.value &&
                         result.value != kg::PresentResult::QUEUED.value)) {
        state->status("frame " + std::to_string(frames) + " presented on the media surface (PresentResult=" +
                      std::to_string(static_cast<int>(result.value)) + ", glGetError=" + std::to_string(drawError) +
                      ")");
      }
      frames++;
      // A still image: redraw twice a second.
      std::unique_lock<std::mutex> lock(state->mutex);
      state->wake.wait_for(lock, std::chrono::milliseconds(500), [&] { return state->stop; });
    }
    try {
      releaseSubscription->unsubscribe();
    } catch (std::exception const&) {
    }
    context->resetCurrent();
    state->status("GL thread stopped after " + std::to_string(frames) + " frames");
  } catch (std::exception const& e) {
    state->status(std::string("GL thread error: ") + e.what());
  }
}

// ---------------------------------------------------------------------------
// UI thread: surface lifecycle and status events.

struct StatusPayload : ApmfBase<StatusPayload, rx::IPayloadDelegate> {
  explicit StatusPayload(std::string message) : message_(std::move(message)) {}
  Ptr<rx::IJsObject> getPayload(View<rx::IJsObjectBuilder> builder) {
    return builder->setString("message", message_)->build();
  }
  std::string message_;
};

struct SurfaceListener : ApmfBase<SurfaceListener, uv::ISurfaceViewListener, uv::IFrameEventListener> {
  explicit SurfaceListener(std::shared_ptr<SurfaceState> state) : state_(std::move(state)) {}

  void onSurfaceCreated(View<uv::ISurfaceView> surfaceView) {
    try {
      auto provider = Ptr<uv::ISurfaceView>(surfaceView).TryQueryInterface<uv::IKeplerMediaSurfaceProvider>();
      if (!provider) {
        state_->status("surface view has no IKeplerMediaSurfaceProvider");
        return;
      }
      auto media = provider->getMediaSurface();
      if (!media) {
        state_->status("surface view has no media surface");
        return;
      }
      state_->status("surface created: media surface " + std::to_string(media->getId()));
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (!state_->thread.joinable()) {
        state_->stop = false;
        // The test frame is drawn at the display's 1920x1080; the surface
        // scales it to the view.
        state_->thread = std::thread(renderThread, state_, media, 1920, 1080);
      }
    } catch (std::exception const& e) {
      state_->status(std::string("onSurfaceCreated error: ") + e.what());
    }
  }

  void onSurfaceUpdated(View<uv::ISurfaceView>) { state_->status("surface updated"); }

  void onSurfaceDestroyed(View<uv::ISurfaceView>) {
    state_->status("surface destroyed");
    std::thread thread;
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->stop = true;
      thread = std::move(state_->thread);
    }
    state_->wake.notify_all();
    if (thread.joinable()) {
      thread.join();
    }
  }

  // UI thread, once per display frame: forward queued status to JS.
  bool onFrameEvent(View<uv::IFrameEvent>) {
    if (!sawFrame_) {
      sawFrame_ = true;
      fileLog("first frame event (UI thread)");
    }
    std::deque<std::string> pending;
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      pending.swap(state_->messages);
    }
    Ptr<rx::IContext> context = context_ ? *context_ : Ptr<rx::IContext>();
    if (context && view_) {
      for (auto& m : pending) {
        try {
          context->getEventEmitter()->emitWithPayload(view_, "onStatus", Ptr<StatusPayload>::Make(m));
        } catch (std::exception const& e) {
          fileLog(std::string("emit failed: ") + e.what());
        }
      }
    } else if (!pending.empty()) {
      fileLog("status not sent to JS (no React context yet)");
    }
    return true;
  }

  bool sawFrame_ = false;

  std::shared_ptr<SurfaceState> state_;
  // Set by updateContext, which may come after makeView.
  std::shared_ptr<Ptr<rx::IContext>> context_;
  Ptr<uv::IView> view_;
};

struct SurfaceViewManager
    : ApmfBase<SurfaceViewManager, rx::IViewManager, rx::IContextDelegate, rx::ILayoutDelegate> {
  explicit SurfaceViewManager(Ptr<uv::IViewModule> viewModule) : viewModule_(std::move(viewModule)) {}

  Ptr<uv::IView> makeView() {
    auto state = std::make_shared<SurfaceState>();
    auto listener = Ptr<SurfaceListener>::Make(state);
    auto view = viewModule_->makeMediaSurfaceView(listener);
    listener->context_ = context_;
    listener->view_ = view;
    frameSubscriptions_.push_back(view->addFrameEventListener(listener));
    state->status("view made");
    return view;
  }

  void updateContext(View<rx::IContext> context) {
    fileLog("updateContext");
    *context_ = context;
  }

  void updateLayout(View<uv::IView> view, View<rx::ILayout> layout) {
    auto const size = layout->getContentFrame()->getSize();
    fileLog("updateLayout " + std::to_string(size.width) + "x" + std::to_string(size.height));
    view->setSize(size);
  }

  Ptr<uv::IViewModule> viewModule_;
  std::shared_ptr<Ptr<rx::IContext>> context_ = std::make_shared<Ptr<rx::IContext>>();
  std::vector<Ptr<apmf::iface::com::amazon::apmf::ISubscription>> frameSubscriptions_;
};

struct ViewManagerFactory : ApmfBase<ViewManagerFactory, rx::IViewManagerFactory> {
  Ptr<rx::IViewManager> makeViewManager(StringView name) {
    fileLog(std::string("makeViewManager ") + std::string(name.data(), name.size()));
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
