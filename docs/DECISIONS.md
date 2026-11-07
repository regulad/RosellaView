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
