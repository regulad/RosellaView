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

## D6. Video sync samples: every frame a second, flagged in tfhd (2026-11-07)

**Decision:** A video fragment is flagged as a sync sample if it's a real IDR frame, or if a second of media time has passed since the last flagged fragment. The flag goes in the `tfhd` default sample flags, not in `trun`.

**Why:**
- iOS and macOS send one keyframe per mirroring session.
- MSE keeps already-played compressed video for seeking, and discards it only in whole runs from one sync sample to the next. Vega's media library hard-codes a 100 MB eviction threshold (`DEFAULT_EVICTION_THRESHOLD`, OS-provided JS).
- With no evictable runs, the `SourceBuffer` filled after about 2 minutes of a Mac's 8 Mbit/s: `QuotaExceededError`, then a frozen picture. The iPhone stall at about 6 minutes was the same thing.
- **The flag was in the wrong box.** Vega's player ignores `trun`'s per-sample fields, `first-sample-flags` as well as durations. Flagging in `trun` alone meant the player saw no sync sample at all, the real IDR included.
- **Vega's player doesn't evict played video at all,** even with sync samples it can see: `video.buffer_behind` grew in a straight line to the quota. So `MirrorPlayer` removes played video more than 10 s behind the playhead every 5 s, as live MSE players do (hls.js's `backBufferLength`).
- That trim only works with the flags in `tfhd`. Before, a `remove()` behind the playhead cascaded to the next sync sample the player knew of. There was none, so it removed everything, including the frames about to play.
- The decoder never needs old compressed frames. A P-frame refers to decoded pictures in the decoder's own reference buffer. Flagging a P-frame as a sync sample only tells MSE where it may cut.

**Implications:**
- The flag claims a P-frame is a safe place to start decoding, which isn't strictly true. If the player ever restarts decoding at one of these frames, after a seek or a detected discontinuity, expect a moment of smearing until the references rebuild. Today the alternative is a black screen until the next real keyframe, which may never come.
- Never seek the live stream.
- The GOP cache for rebuilding a stalled player (`fmp4_resync`) still goes by real IDRs.
