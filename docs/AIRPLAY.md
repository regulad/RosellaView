# AirPlay: what senders actually do

These are findings about AirPlay senders and libairplay (UxPlay's `lib/`), seen while RosellaView ran on the Fire TV. Vega platform facts are in `~/repositories/ProjectRosella/VEGA.md`, and the design decisions that follow from both are in `DECISIONS.md`.

## libairplay (UxPlay `lib/`)

- We build from our fork, `regulad/UxPlay`, branch `rosella`. It's based on UxPlay master (`05b0275`) because the built-in `lib/mdnsd` responder isn't in the v1.73.7 release; mdnsd replaces Avahi/Bonjour, which an app can't reach on Vega anyway. The fork adds one thing: a `report_client_details` callback, for the sender's OS and AirPlay versions.
- **The client's SETUP request** carries `osName`, `osVersion`, `osBuildVersion`, `sourceVersion`, `model`, `name`, `deviceID`, `macAddress`, `sessionUUID`, `timingProtocol` and `isScreenMirroringSession`. Upstream passes on only `model`, `name` and `deviceID` (`report_client_request`).
- **Video** arrives as H.264 Annex-B access units with SPS/PPS in-band. `ntp_time_remote` is the sender's timestamp, in ns.
- **Audio** arrives as decrypted packets in `audio_decode_struct`, with `rtp_time` (in samples, 44.1 kHz) and `seqnum`. Lost packets show up only as gaps in `rtp_time`. Resend requests don't always fill them in time.
- **Rotation:** when the phone rotates, the SPS changes in-band and libairplay doesn't call `video_set_codec_and_size` again. Take the picture size from the SPS.
- **Logged but harmless:** `Unhandled RTSP request "audioMode: moviePlayback"` repeats every few seconds while a Mac or iPhone plays video.
- **Volume** (`audio_set_volume`) is in dB, from -30 to 0, with -144 meaning mute.

- **Event channel (our fork):** the first SETUP response returns a real `eventPort`, the sender connects to it, and `POST /command {type: "forceKeyFrame"}` gets an IDR within a frame or two. iOS 27 and macOS 11 accept it in plain text. Each IDR comes with a codec (SPS/PPS) packet that libairplay reports as `video_pause`/`video_resume`. See DECISIONS.md D7.

## iOS (iPhone18,1, User-Agent AirPlay/980.77.1)

- **Video:** mirrors at up to 60 fps, at the screen's aspect ratio (498×1080 in portrait), dropping to 20–45 fps when little changes.
  - **Video playback inside apps is mirrored at about 30 fps**, while UI scrolling reaches about 60 (2026-11-07). A 60 fps test video with an every-other-frame flasher arrived at 25–31 fps and looked frozen in one state: aliasing at the sender. The TV showed every frame it received (`video.fps_shown` equal to `video.fps`, 0 `video.dropped`). To test a true 60 fps path, use content the phone draws live, such as a page animating on every frame, not a video file.
  - The SPS has **no VUI timing**.
  - Left alone, it sends **one keyframe (IDR) per session**: one in a 9-minute run. It sends another on a rotation, a new session, or a `forceKeyFrame` request on the event channel.
- **Audio:**
  - AAC-ELD (`ct=8`, 480 samples per packet, ~92 packets/s) while mirroring
  - ALAC (`ct=2`) for audio-only (music)
  - It ignores the receiver's advertised `audioFormats` and sends these regardless.
  - The audio stream stops and restarts with a fresh `audio_format` call when apps pause media.
  - **Its audio timestamps run about 300 ms later than its video's,** relative to macOS (2026-11-07). With A/V matched by sender time, lips matched at +300 ms of audio delay for the iPhone and at 0 for the Mac, on the same TV. RosellaView keeps the delay per sender device, defaulting by kind.
  - **iOS suppresses quiet or short sounds while no media audio is playing.** Transients such as a metronome "click" used to judge lip sync sometimes didn't come through cleanly. Playing any audio in the background fixed it. So test A/V sync with continuous media audio (a video with speech), not with isolated clicks.

- **Apps whose video iOS leaves out of the mirror** (2026-11-07). Mirroring carries on, but the app's video area isn't in it.
  - **TikTok:** the phone switches to a "playing on the TV" screen; the TV shows that UI, with its progress bar advancing, but no video. The frame rate drops to the UI's updates, often 0 for seconds at a time, and iOS may send the "video stream is stopping" codec packet (libairplay's `video_pause`) and later post `/audioMode` `default`. Advertising feature bits 0 and 4 (Video, HLS), as `uxplay -hls` does, didn't make it hand the video over (`/play`), so it wants an AirPlay video protocol libairplay doesn't implement.
  - **HDCP-protected video** (FairPlay DRM, as in most paid streaming apps) is left out the same way.
  - **YouTube works:** its video is mirrored inside the phone UI.

## macOS (MacBook8,1, User-Agent AirPlay/550.10)

- **Video:** a full-screen video is mirrored at the **content's frame rate** (~24 fps for a film) at 1280×720, at 3–10 Mbit/s. So frame durations can't be assumed to be 1/60 s.
  - **It never sent more than about 30 fps** (MacBook8,1, macOS 11), even for 60 fps content, so a 60 fps flasher aliases there too.
  - Like iOS, it sends IDRs only when asked (`forceKeyFrame`) or when the display mode changes, e.g. on leaving QuickTime's full screen.
  - Two keyframes were seen in a short session.
- **Audio:** AAC-ELD, as on iOS.
- **Connections:** it connects over both IPv6 and IPv4 at once, then drops one of them (`conn_init` twice, then a `conn_destroy`).
