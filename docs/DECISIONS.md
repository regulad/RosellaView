# Decisions

Phase 0 outcomes and other choices that later work builds on. The raw probe logs are in `~/vega-probe/results/` (private notes, not part of this repo).

## D1. Video output: W3C Media Source Extensions from JS (2026-11-06)

**Decision:** Mirrored video goes through the OS-provided `@amazon-devices/react-native-w3cmedia` JS player (`VideoPlayer`, `KeplerVideoSurfaceView`, `MediaSource`, `SourceBuffer`), fed with fragmented MP4 that native code builds from UxPlay's H.264 access units. We accept roughly **0.45 s** of player latency.

**Why:**
- Probe results (`~/vega-probe/mse-probe`, 1080p60 H.264 High, no B-frames, one `moof`+`mdat` per frame):
  - per-frame appends kept up at 60 fps with **0 dropped frames**
  - the overlay (an 80%-opacity `Animated` view above the surface) renders correctly
  - the player holds a fixed buffer of about 415–450 ms ahead of `currentTime`
- Nothing on the JS side reduces that buffer:
  - starting `play()` on the first append: no effect
  - `playbackRate` 1.25: still ~300–370 ms
  - seeking to the live edge: stalls the player
  - `remove()` plus a `timestampOffset` reset at keyframes: the player rebuilds the same buffer, and you see visible skips
- Native MSSE (`IMediaSampleSource`) can't attach to the React Native video surface. It needs a native `IMediaSurface`, which comes from `createMediaSurface(displayController, windowHandle)`. A Turbo Module has no documented way to obtain those for the RN window, and the JS player has no MSSE mode.
- 0.45 s is fine for the intended use. Someone typing or gaming watches their phone; the TV is for everyone else.

**Implications:**
- Keep video output behind one interface (`video/` → "fMP4 segmenter + JS appender"), so a lower-latency backend can replace it later if Amazon documents one.
- Use `sequence` append mode and start `play()` on the first appended fragment.
- The app manifest needs the media services. Without them the player silently never plays:

```toml
[[wants.service]]
id = "com.amazon.mediametrics.service"
[[wants.service]]
id = "com.amazon.mediabuffer.service"
[[wants.service]]
id = "com.amazon.mediatransform.service"
[[wants.service]]
id = "com.amazon.audio.stream"
[[wants.service]]
id = "com.amazon.audio.control"
[[wants.service]]
id = "com.amazon.media.server"
[[wants.service]]
id = "com.amazon.media.playersession.service"
[[wants.privilege]]
id = "com.amazon.devconf.privilege.accessibility"
```

  (Source: developer.amazon.com/docs/vega/2.0/media-player-setup.html. The build adds `needs.module /com.amazon.kepler.w3cmedia_2@IW3cmedia_3` itself.)
- **To revisit:** check whether the buffer shrinks at lower frame rates or resolutions. Ask Amazon about a low-latency MSE/MSSE mode; this matters more for RosellaRemote.

## D2. Test network: 802.11n on the lab AP (2026-11-06)

The laptop's `create_ap` AP now runs with `IEEE80211N=1`, started via `create_ap.service` (not enabled at boot) instead of the Linux WiFi Hotspot GUI, which rewrote the config back to 802.11g. Throughput to the stick went from about 10 to 38 Mbit/s.

## D3. Audio: decode, then FLAC verbatim frames in MSE (2026-11-07)

**What iOS sends** (iPhone18,1, AirPlay/980.77.1), logged from `audio_get_format`:
- Music only: ALAC 44.1 kHz/16-bit/stereo (`ct=2`, `audioFormat=0x40000`, 352 samples per packet).
- Screen mirroring: AAC-ELD 44.1 kHz stereo (`ct=8`, `audioFormat=0x1000000`, 480 samples per packet).
- Advertising only AAC-LC (`audioInputFormats`/`audioOutputFormats = 0x00C00000` in `/info`, via a local UxPlay patch) changed nothing; iOS chose the same codecs. The patch was reverted and not forked.

**What Vega's MSE plays** (`isTypeSupported` plus real fMP4 fragments fed at real time, `~/vega-probe/mse-probe`):

| Format | Result |
|---|---|
| AAC-ELD `mp4a.40.39` | not supported |
| ALAC | not supported |
| PCM `ipcm` | claimed supported, but fails with `MEDIA_ERR_DECODE` |
| FLAC `fLaC` | plays (heard) |
| AAC-LC `mp4a.40.2` | plays (heard) |

**Decision:**
- Decode ALAC and AAC-ELD with FFmpeg's `alac`/`aac` decoders (LGPL build, decoders only).
- Wrap the PCM in FLAC frames with VERBATIM subframes: no prediction or entropy coding, just headers and CRCs, so it costs about as much as a copy.
- Mux as a `fLaC` track in fragmented MP4, appended to a second `SourceBuffer` on the same `MediaSource` as the video so the player keeps A/V sync.
- Pass AAC-LC (`ct=4`) through as `mp4a.40.2` if a sender ever uses it.
- Fill silence while audio is idle, so the audio buffer never starves video playback.

## D4. Video frame timing: real durations under a 10 fps SPS declaration (2026-11-07)

**Decision:** Each video frame's `tfhd` default duration is the sender-timestamp interval before it, clamped to 1–190 ms. The SPS is rewritten to declare 10 fps.

**Why:**
- Vega's MSE player declares a discontinuity at any frame lasting more than twice the frame duration in the SPS VUI timing. It then drops everything until the next keyframe, and senders send keyframes rarely; iOS may send one per session. With no VUI timing at all, it assumes 1 ms per frame.
- Earlier builds therefore declared 60 fps and gave every frame 1/60 s. That works for a 60 fps iPhone, but a Mac playing a film sends ~24 fps, so the player went through each second of video in 0.4 s and then waited (visible stutter).
- Probe (`~/vega-probe/mse-probe`, streams made by `~/vega-probe/vt/tool/vtprobe` with this muxer, fed at their real rate; rate = media time ÷ wall time):
  - 24 fps feed, declared 60 fps, 1/60 s each: rate **0.40**, freezes up to 980 ms
  - 24 fps feed, declared 60 fps, 1/24 s each: never plays (discontinuity)
  - 24 fps feed, declared 60 fps, alternating 2/60 and 3/60 s: never plays
  - 24 fps feed, declared 24 or 10 fps, 1/24 s each: rate **1.00**, no drops
  - 60 fps feed, declared 24 or 10 fps, 1/60 s each: rate **1.00**, no drops
  - 60 fps feed, declared 10 fps, jittered 1/90–1/45 s (mean 1/60): rate **1.00**, no drops
- So container durations are honoured; the declaration only sets the ceiling (2 × 1/10 s).

**Implications:**
- Longer pauses (a still screen sends no frames) are cut to 190 ms in the timeline. The player then waits for the next frame, as before.

## D5. Telemetry: Sentry, with a native bridge (2026-11-07)

**Decision:** Crash, error, log and metric reporting goes to Sentry (org `regulad`, project `rosellaview`, US region). It replaces the hand-rolled `/scratch` journal and crash breadcrumbs. Sender details (device ID, model, name, AirPlay version) are attached deliberately, to tell iPhone, iPad and Mac problems apart.

**Why:**
- No crash-reporting vendor supports Vega. The pieces that work there:
  - `@sentry/core` with our own client, fetch transport and Hermes stack parser
  - sentry-native (MIT, so fine under the AGPL) for native crashes
- **Breakpad doesn't work:** its minidumps arrived as "Invalid Minidump ... system information stream was not found". Its dump writer ptraces the crashed process from a child, which the app sandbox evidently refuses.
- **Crashpad doesn't work:** it needs a separate handler process.
- **inproc works, with a fix:** it sends a stack trace that Sentry symbolicates against our uploaded debug files, but its armv7 unwinder returned a single bogus frame. The `on_crash` hook replaces that with pc, lr and stack-scanned return addresses into our own library.
- Native envelopes go out through JS, so the module needs no HTTPS client of its own.

**Implications:**
- Every build that should be readable in Sentry goes through `scripts/build.sh`.
- Sender details are personal data, so the Appstore privacy disclosure must cover crash and diagnostic data sent to a third party (Sentry).
- The sender's OS name, version and build, its AirPlay version and its MAC address come from the SETUP request. libairplay doesn't pass them on, so our fork does: `regulad/UxPlay`, branch `rosella`, adds a `report_client_details` callback.

## D6. Video sync samples and trimming (2026-11-07, revised by D7)

**What we tried, and what held:**
- iOS and macOS send one IDR per mirroring session. MSE keeps played compressed video for seeking, and Vega's player:
  - never evicts it by itself
  - can cut it only at real IDR frames, judged from the H.264 data
- The `SourceBuffer` filled at the OS library's 100 MB threshold, after about 2 minutes of a Mac's 8 Mbit/s and about 6 of an iPhone's, then froze with `QuotaExceededError`.
- Flagging sync samples once a second didn't help, in `trun` or in the `tfhd` defaults: the player ignores the container's flags for this.
- `remove()` behind the playhead ran on to the next IDR. With none ahead, it wiped the buffer, the frames about to play included.
- The decoder itself never needs old compressed frames; P-frames refer to decoded pictures in its reference buffer.

**What stays:**
- `MirrorPlayer` removes played video more than 10 s behind the playhead every 5 s (hls.js's `backBufferLength`).
- On `QuotaExceededError` it trims and retries the segment.
- `fmp4.c` flags real IDRs as sync samples, in `tfhd`. The once-a-second flags were removed, since the player ignores them.
- The GOP cache and replay for rebuilding a stalled player (`fmp4_resync`) were removed too. A stalled player is now closed, and the next requested IDR starts a new one (`Receiver.restartVideo`).
- The trim only works because D7 makes the sender send real IDRs.

**A/B result (2026-11-07, Mac, Big Buck Bunny):**
- **Trim off (B):** the player evicts by itself at its threshold. Played video went to about 96 s, then dropped to about 4 s; no freeze, no errors. So with D7's keyframes the trim isn't needed for correctness.
- **The buffer is allocated in our process as data arrives,** not reserved up front. During B's fill, RSS went 124 → 265 MB, then grew about 3–4 MB/min after eviction.
- **Trim on (A):** RSS still climbed steadily, 122 → 231 MB over 5 minutes, about 20 MB/min, with only about 10 s buffered. That points to a leak or allocator churn outside the `SourceBuffer` (segment ArrayBuffers? freed memory not returned?). An overnight run on A tells us which.
- **B over a full loop (about 9 minutes):**
  - played video sawtoothed from about 4 s up to 134 s
  - RSS climbed to 310 MB without levelling off, with swap at 393 MB and 26–35 MB of RAM free
  - our CPU share crept from 11% to 15%
  - the owner saw regular micro-stutter. The sender's frame rate held at 24 fps, so the stutter was on the playback side: likely memory pressure, plus the player's big burst evictions.
- **Decision: keep the trim, unconditionally** (the A/B switch is gone). It matches Amazon's advice (VEGA.md). Investigate A's memory growth over an overnight run.

## D7. Ask the sender for keyframes: forceKeyFrame on the event channel (2026-11-07)

**Decision:**
- Our UxPlay fork (`regulad/UxPlay`, branch `rosella`) returns a real `eventPort` in the first SETUP response and accepts the sender's connection to it.
- `raop_request_keyframe()` sends `POST /command` with the plist `{type: "forceKeyFrame"}`.
- The app asks every 10 s while video plays (`KEYFRAME_EVERY_TICKS` in `App.tsx`).

**Why:**
- Without real IDRs, D6's trim can't work. Apple's CarPlay receivers send this request (`_AirPlayReceiverSessionForceKeyFrame`); UxPlay advertised `eventPort = 0`, so it had never been tried on ordinary mirroring.
- Tested on 2026-11-07, both sending one IDR per request and both answering in plain text (`RTSP/1.0 200 OK`) with no event-channel encryption:
  - macOS 11.7.11 (AirPlay/550.10)
  - iOS 27.0.1 (AirPlay/980.77.1)
- With it, played video in the buffer held at about 9.5 s, video buffered ahead at about 0.5–0.65 s, and there were no stalls. No visible hitch at each IDR.

**Implications:**
- Each forced IDR comes with a codec (SPS/PPS) packet that libairplay reports as `video_pause`/`video_resume`. That's noise.
- If a sender refuses the request (CarPlay senders are known to), video freezes when the buffer fills again. The app reports "keyframe request not sent", and a stall warning when that happens.
- The 10 s interval must stay below `MirrorPlayer`'s 10 s back buffer plus its 5 s trim period.

## D8. Lip sync: keep audio on the picture by sender time; viewer-set delay (2026-11-07)

**Decision:**
- Every segment carries the sender's clock time of its first sample (`ntp_time_remote`, which audio and video share). Each `MirrorPlayer` keeps a map from its playhead to sender time.
- `AvSync` compares the two once a second. When audio is more than 80 ms off its target for 2 s, the native audio pipeline drops upcoming samples (audio late) or inserts silence (audio early).
- The target is the video's sender time minus a viewer-set audio delay. Left/right on the remote while mirroring set it, in 20 ms steps from −500 to +500 ms, and it's saved.

**Why:**
- **Video and audio play in separate MSE players with nothing tying them together.**
  - Overnight, one hiccup left audio about 2.6 s behind for 7 hours.
  - In a later session, a step left it 0.6 s behind: visible, and under the earlier absolute 1.2 s cap.
- **Seeking the live audio player (`currentTime`) doesn't move it on Vega.** 147 "corrections" in a few minutes changed nothing, and probably caused the audible dips.
- **Corrections need guard rails.** The first version dropped 1.2 s at once, starved the audio player, then kept dropping as the stalled player's offset grew, until the audio was gone. Now:
  - at most 300 ms per step
  - at least 300 ms of audio left buffered
  - only while both playheads move
  - a 5 s warm-up after a player starts
  - offsets over 3 s ignored
  - 4 s to settle after each correction
- **A constant offset remains that the timestamps don't capture, and it depends on the sender.** On the same TV, an iPhone (iOS 27) matched at +300 ms and a Mac (macOS 11) at 0. So iOS evidently stamps its audio about 300 ms later than its video, and macOS doesn't.
  - Hence the viewer delay is **kept per sender device** (by AirPlay device ID).
  - A new device starts from a default for its kind: iPhone +300 ms, Mac 0. iPads are assumed like iPhones until tested.
  - Any TV-specific output latency is folded into the same number.

**Implications:**
- In audio-only mode (no video player) the latency cap in `MirrorPlayer` keeps audio live instead, by dropping samples down to about 0.5 s.
- AAC-LC passthrough (ct 4) can't be shifted, since it isn't decoded. No sender has used it so far.
- Metrics: `av.offset` (distance from target; negative means audio late), `av.delay` and `av.corrections`.

## D9. Known limitation: full-range video is clipped by the MSE path (2026-11-07; resolved by D12)

**Finding:**
- iOS mirrors **full-range** video: BT.709 primaries and matrix, sRGB transfer (13), and `video_full_range_flag` 1, in the SPS. macOS sends limited range.
- Vega's W3C MSE video path decodes full range as if it were limited, so detail at the ends is lost. On a 0–255 grey step chart, 0–16 merged into one black and 235–255 into one white. In practice the near-white greys of iPadOS/iOS UI merge into white backgrounds; seen on the Files screen.
- It doesn't depend on the TV's HDMI format: Settings offers RGB (default) or YCbCr, with no range setting, and both looked the same.

**Tried, all clipped** (the chart through RosellaView's muxer, played on the TV):
- **A:** full range in the SPS only (as iOS sends it, and as RosellaView passes it through)
- **B:** A plus an MP4 `colr` nclx box declaring full range
- **C:** the full-range flag with no colour description
- **D:** full range, all-BT.709 description, plus `colr`
- **E:** as D, without `colr`

**Decision:**
- Ship with the limitation and document it (COMPATIBILITY.md).
- The muxer still writes the `colr` box from the sender's SPS: it's correct per ISO BMFF, and a future Vega update may honour it.
- `video_init` reports the sender's range and colour codes, and the `video.range` tag marks affected sessions.

**Update:** the native pipeline (D12) shows full range correctly and is now the default; this applies only to the MSE fallback.

**Ways out, none cheap:**
- **Remapping the pixels** (full → limited) needs a decode and re-encode of every frame. That's beyond this CPU at 1080p.
- **iOS** has no AirPlay setting to send limited range.
- **A native video surface** with our own rendering, or a player that honours the flag, could fix it. That's next to investigate.
- **Ask Amazon** whether there's a supported way to signal full range to the MSE player.

## D10. APMF interface glue is a System Library; build native components with the SDK (2026-11-07)

**Decision:** Native UI components (for a native video surface) are built the normal way: the SDK's IDL import libraries and generated glue, calling the OS's `libapmf.so`. No clean-room re-implementation, and no linking exception from the GPL authors.

**Why:**
- GPLv3 §1 excludes from Corresponding Source any System Library: one that is "included in the normal form of packaging a Major Component" (Vega OS) and "serves only to enable use of the work with that Major Component".
- The generated glue is thin interface code (vtable thunks, interface hashes, call shims) that only calls the OS. It's the same role as OS SDK headers and import stubs, which GPL programs routinely compile in: Windows SDK, macOS SDK.
- Bundled vendor *libraries* with their own functionality are different, and stay excluded: Vega's static `libturbomoduleAPI.a`, Google Play Services client libraries (research `06-gpl-linking-shims.md`).
- **Owner's call:** a vanishing risk, fully defensible, and it greatly simplifies the work.

**Implications:**
- The glue source isn't distributed (System Libraries are excluded from Corresponding Source), which also fits the SDK's confidentiality terms.
- A one-line note for the lawyer review, next to playfair.
- The existing Node-API module stays as it is.

## D11. Native video surface: a media-surface component with our own GLES (2026-11-07; proof of concept)

**Result:**
- A native UI component (`native-view/`, package `rosellaview-surface`, `/xyz.regulad.rosellaview.surface`) renders inside RosellaView's window with our own OpenGL ES (Mesa / Mali-G310, ES 3.1), using public APIs only.
- A full-range NV12 grey step chart, converted by our own full-range BT.709 shader, showed **all 8 bars at both ends on the TV** (0–32 and 223–255). Pixel readback was exact.
- So full range survives the display path. The clipping (D9) is in the MSE/hardware-decoder path.

**How:**
1. `IViewModule::makeMediaSurfaceView` gives a hole-punched surface in the window; `IKeplerMediaSurfaceProvider::getMediaSurface()` gives its `IMediaSurface`.
2. On our own thread: system-client display controller → `createGpuDeviceUsingOpenGlesApi`, then three 1920×1080 ARGB8888 scan-out graphics buffers.
3. Each buffer is a GL texture (`IGlGpuDevice::createGlImage`/`bindBuffer`) attached to our own framebuffer. An offscreen surface exists only to make the context current.
4. Draw, `glFinish`, then `IMediaSurface::presentBuffer`.

**What didn't work:** `makeGLSurfaceView` + `createScreenSurface(window handle)`. An app component only reaches `createDisplayControllerForSystemClients`, and a GPU device from it can't make the UI toolkit's window current ("Make Current Surface Wrong!"). expo-gl uses private factories for this; we don't.

**Gotchas:**
- `addBufferReleaseListener` doesn't keep its listener alive; hold it, or it segfaults.
- The SDK ships no GL headers, so `native-view/src/gles2.h` declares what we use. Entry points come from `IGpuDevice::getExtensionAPI`.
- JS `onStatus` events emitted from the frame callback never arrive (unexplained), so status goes to `/scratch/rosella-surface.log`.
- The library must be named `<uiComponentName>.so`.
- It needs its own package: one autolink entry can't have both `libraryName` and `uiComponentName`.

**Next:** done in D12, which feeds the mirrored video through it.

## D12. Native video: hardware decode through media_transform, rendered on our own surface (2026-11-07)

**Decision:** Mirrored video no longer goes through MSE. The receiver hands each H.264 access unit to the native surface component (D11), which decodes it with the OS's hardware decoder and renders it with our own shader. MSE stays as a manual Compatibility Mode.

**Why:**
- **Full range** is shown correctly (D9). The decoder passes samples through untouched: a full-range chart decoded bit-exactly, matching FFmpeg.
- **Cheap:** about 4 ms per frame in the decoder. 1080p60 keeps up, at about 370 fps unpaced. The CPU de-tile costs about 22% of one core at 1080p60; FFmpeg software decode would need about 1.2 cores.
- **No MSE workarounds:** no fMP4, back-buffer trim (D6) or periodic keyframes (D7). Lip sync moves the video instead of cutting audio.

**How:**
- **Bridge** (`native/include/rosella_video.h`): `libRosellaView.so` exports `rosella_video_attach`/`_report`/`_latency_ms`/`_request_keyframe`; the component finds them with `dlopen(RTLD_NOLOAD)` + `dlsym`. While a consumer is attached, `cb_video_process` sends access units to it instead of the muxer.
- **Decoder** (`native-view/src/Decoder.cpp`): `IMediaFactory::createTransform` with mime `video/h264` (`video/avc` fails to configure) and the SPS's resolution, reopened when the size changes. Input: ARGB8888 graphics buffers with `BufferMime::H264`. Output: MediaTek MM21 tiles, mapped and de-tiled on the CPU, then uploaded as NV12 textures.
- **Rendering:** the shader uses the stream's own range and matrix from the SPS (iOS: full range; macOS: limited) and letterboxes into 1920×1080 scan-out buffers. A buffer is reused only once the display has released it.
- **Timing:** a frame is shown `latency` ms after its capture time, which libairplay's NTP sync maps onto our clock.
  - JS sets the latency (`Receiver.setVideoLatency`); the component eases towards it at 100 ms/s (at most a 10% speed change), so corrections never freeze or skip.
  - AvSync takes the median offset of 5 readings, moves the latency within 80–1500 ms, and shifts audio only beyond those bounds. The audio player keeps its own latency cap.
- **Recovery:** the component starts again at the next IDR, replaying the cached SPS/PPS to a new decoder, and asks for keyframes at most once a second. It restarts the decoder on a decode error, on a size change, or after 10+ frames in with nothing out for 2 s.
- **MSE stays, as Compatibility Mode only:** a checkbox on the About screen, persisted, switched on by the viewer. There's no automatic fallback (owner's call): the native path is the product. The component's status lines reach JS through the bridge as `native_status` receiver events (view events from it never arrive, D11).

**Gotchas found on the way:**
- The decoder reports its **padded** size: 816×1088 for an 810×1080 SPS. The padding is zeros, which shows as a green edge. Draw the SPS's size, sampling only the centres of visible texels.
- The decoder holds its newest picture until the next access unit arrives, so a sender gone quiet leaves one in flight. A stall check that counts time alone restarts it on every pause.
- **A keyframe request before the stream's first frame ends macOS mirroring:** the Mac answers with an empty codec packet, which libairplay reads as non-H.264. `receiver_request_keyframe` refuses until the first frame.
- With native video, the iPhone's lip sync is right at 0 ms delay. It needed +300 ms on MSE.

**Risk:** `media_transform` is outside the OS compatibility contract. An OS update could break it; viewers can then turn on Compatibility Mode, and we'd ship a fix (plan B: FFmpeg software decode, which keeps full range).