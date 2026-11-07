# AirPlay: what senders actually do

These are findings about AirPlay senders and libairplay (UxPlay's `lib/`), seen while RosellaView ran on the Fire TV. Vega platform facts are in `~/repositories/ProjectRosella/VEGA.md`, and the design decisions that follow from both are in `DECISIONS.md`.

## libairplay (UxPlay `lib/`)

- We pin UxPlay master (`05b0275`) because the built-in `lib/mdnsd` responder isn't in the v1.73.7 release. It replaces Avahi/Bonjour, which an app can't reach on Vega anyway.
- **Video** arrives as H.264 Annex-B access units with SPS/PPS in-band. `ntp_time_remote` is the sender's timestamp, in ns.
- **Audio** arrives as decrypted packets in `audio_decode_struct`, with `rtp_time` (in samples, 44.1 kHz) and `seqnum`. Lost packets show up only as gaps in `rtp_time`. Resend requests don't always fill them in time.
- **Rotation:** when the phone rotates, the SPS changes in-band and libairplay doesn't call `video_set_codec_and_size` again. Take the picture size from the SPS.
- **Logged but harmless:** `Unhandled RTSP request "audioMode: moviePlayback"` repeats every few seconds while a Mac or iPhone plays video.
- **Volume** (`audio_set_volume`) is in dB, from -30 to 0, with -144 meaning mute.

## iOS (iPhone18,1, User-Agent AirPlay/980.77.1)

- **Video:** mirrors at up to 60 fps, at the screen's aspect ratio (498×1080 in portrait), dropping to 20–45 fps when little changes.
  - The SPS has **no VUI timing**.
  - It sends **one keyframe (IDR) per session**: one in a 9-minute run. A new decoder can't start mid-session; the only ways to get a fresh IDR are a rotation or a new session.
- **Audio:**
  - AAC-ELD (`ct=8`, 480 samples per packet, ~92 packets/s) while mirroring
  - ALAC (`ct=2`) for audio-only (music)
  - It ignores the receiver's advertised `audioFormats` and sends these regardless.
  - The audio stream stops and restarts with a fresh `audio_format` call when apps pause media.

## macOS (MacBook8,1, User-Agent AirPlay/550.10)

- **Video:** a full-screen video is mirrored at the **content's frame rate** (~24 fps for a film) at 1280×720, at 3–10 Mbit/s. So frame durations can't be assumed to be 1/60 s.
  - Two keyframes were seen in a short session.
- **Audio:** AAC-ELD, as on iOS.
- **Connections:** it connects over both IPv6 and IPv4 at once, then drops one of them (`conn_init` twice, then a `conn_destroy`).
