# RosellaView: game plan

A screen-mirroring receiver for iPhone, iPad and Mac on Fire TV devices running Vega OS. It is built on UxPlay's protocol library (`third_party/UxPlay/lib`, "libairplay"), with Vega-native video and audio output replacing UxPlay's GStreamer renderers, and a deliberately tiny React Native UI.

Research behind every decision here is in `~/vega-probe/` (`REPORT.md`, `research/01`–`08`). Those files are private notes and aren't part of this repo; `06` describes Amazon headers marked confidential, so never copy from it into the repo.

## 1. Product

### 1.1 The entire UI

There are two screens and one overlay. Nothing else.

**Idle screen** (shown whenever nothing is mirroring):

```
                Open Screen Mirroring and select
                         <receiver name>

                [ Remove watermark ]      ← only if not purchased
```

- `<receiver name>` is the name advertised over mDNS. The default is `RosellaView`. Later this can become the Fire TV's own device name if a public API exposes it; there is no settings screen to rename it.
- The button is the only focusable element, so the remote's OK button triggers it with no navigation.

**Mirroring screen**: the full-screen video surface, plus the watermark overlay if not purchased. No chrome or controls. Mirroring ends when the sender stops it, and Back returns to the idle screen and drops the connection.

**Watermark overlay** (until the entitlement is owned):
- A RosellaView wordmark/logo at **80% opacity** that bounces around the screen on top of the video, DVD-screensaver style: constant velocity, reflects off each edge, and changes tint on every bounce.
- It is drawn above the video surface in the React Native view tree with `pointerEvents="none"`, and animated with `Animated` using the native driver so it doesn't cost JS-thread time while video is decoding.
- Use original artwork only, not the DVD Video logo, which is a registered trademark of DVD Format/Logo Licensing Corp. "DVD-style" describes the motion, not the art.
- The idle screen's button starts the purchase. When the purchase succeeds, the overlay and the button disappear immediately and stay gone; the entitlement is restored on every launch.

### 1.2 Monetisation

- The app is free to download, because Vega doesn't support paid apps (Amazon staff, 2026-11-02, https://community.amazondeveloper.com/t/paid-app-support-on-vega/29890).
- A single **Entitlement** IAP item, SKU `rosellaview.remove_watermark`, is a one-time purchase.
- Use Amazon IAP only, with no other payment routes or donation links in the app (Amazon's monetisation policy).
- Check the entitlement on launch and when returning to the foreground (Amazon's "purchase updates" call) so that reinstalling the app or using a second device restores it.

### 1.3 Store listing

Detailed reasoning is in `research/03` and `research/08`.

- **Title:** `RosellaView`. **Brand:** RosellaSoftware. **Publisher:** Parker Wahle <regulad@regulad.xyz>, using a personal Amazon Developer account.
  - The knockout search (`research/08`) found no conflicts for RosellaView.
  - "Rosella" is usable but hard to register in class 9, because Rosella Brands Pty Ltd has a pending US application, ROSEY THE ROSELLA (SN 79453290).
  - "RosellaAir" was dropped because of the Apple association.
- **Description:** "Receive AirPlay® screen mirroring from iPhone, iPad and Mac." Use "AirPlay" once only, followed by: "RosellaView is not affiliated with or endorsed by Apple Inc. AirPlay is a trademark of Apple Inc." Then add "Based on UxPlay (GPLv3). Source code: <repo URL>."
- Never write "Works with Apple AirPlay", never show Apple's AirPlay glyph, and never mention "Vega" in any listing field (an Amazon rule).
- **Icon:** original art.
- **Reviewer notes:** these are essential, because Amazon rejected another Vega mirroring app with "Displays could not connect to the device". Spell out the steps: "same Wi-Fi network; on iPhone open Control Center → Screen Mirroring → RosellaView". Say that no account is needed, and that the watermark is the paid feature.

## 2. Architecture

```
┌──────────────────────────── React Native (Hermes) ─────────────────────────────┐
│ App.tsx: IdleScreen | MirrorScreen + Watermark   ·  iap.ts (Amazon IAP)        │
│ native.ts: globalThis.__turboModuleProxy('RosellaView')  (no Amazon TM JS)     │
└───────────────▲───────────────────────────────────────┬────────────────────────┘
                │ events (threadsafe fn)                │ start/stop/setName
┌───────────────┴───────────────────────────────────────▼────────────────────────┐
│ librosellaview.so: pure Node-API module, C, links only libkeplerscript.so (OS) │
│  ├─ bridge/   napi glue: module registration, event queue, ArrayBuffer handoff │
│  ├─ receiver/ owns raop_t + dnssd_t, implements raop_callbacks_t               │
│  ├─ video/    H.264/H.265 NAL → output backend (see §3)                        │
│  ├─ audio/    AAC-ELD/ALAC/AAC-LC/PCM → PCM → output backend                   │
│  └─ third_party (static): UxPlay lib/ (libairplay, incl. mdnsd, llhttp,        │
│     playfair), libplist, OpenSSL 3 (libcrypto), FFmpeg (LGPL build, decoders)  │
└────────────────────────────────────────────────────────────────────────────────┘
```

Key points:
- **UxPlay's `lib/` is used as-is.** `renderers/` and `uxplay.cpp` (GStreamer, X11, the CLI) are not built. Our `receiver/` code replaces `uxplay.cpp`'s role: it fills `raop_callbacks_t` (`video_process`, `audio_process`, `conn_init`/`conn_destroy`, `display_pin`, `video_set_codec_and_size`, `audio_get_format`, `mirror_video_running` and the rest) and starts the `raop_t` httpd with `dnssd` advertising `_airplay._tcp` and `_raop._tcp`.
- **mDNS uses UxPlay's built-in `lib/mdnsd`** (LGPL-2.1+, from @kgbook). It's on UxPlay `master` but not in the v1.73.7 release, which is why the submodule is pinned to a `master` commit. Avahi and dns_sd aren't available to Vega apps; the system avahi-daemon owns 5353 with `SO_REUSEADDR`, and our probe showed an app can co-bind it.
- **No Amazon code goes into our binary.** The module is a plain Node-API addon registered from `autoLinkKeplerTurboModulesV1()` via `napi_module_register()`, built against upstream `node-api-headers` (MIT) and linked only to the SDK's `turbomoduleAPI::napiStub`. The JS side calls `globalThis.__turboModuleProxy` directly instead of Amazon's `TurboModuleRegistry`. The proven prototype is `~/vega-probe/tm-napi`.
  - Runtime quirks to remember: `napi_*_async_work` aborts, so use a `pthread` with `napi_threadsafe_function`. `napi_get_value_string_utf8` with a NULL buffer aborts, so pass a real buffer. `syslog()` doesn't reach `loggingctl`; use `console.log` from JS for logging.
- **Static third-party libraries.** Every dependency is a submodule under `third_party/`, built from source with the SDK toolchain and linked statically into `librosellaview.so`. The only `NEEDED` entries allowed are `libkeplerscript.so`, `libc.so.6`, `libm.so.6`, `libpthread`/`libdl` if used, and `ld-linux-armhf.so.3`. That keeps us inside Strict ABI Check and avoids a second `.so` to version.
- **Threads.** libairplay runs its own httpd/RTP threads. Callbacks never call into JS directly; they enqueue to a threadsafe function (for state events) or to the video/audio backends.

### 2.1 Licensing posture

- The repo is **AGPL-3.0-or-later** (`LICENSE.md`, the FSF text). UxPlay's GPL-3.0 code may be combined under AGPLv3 by GPLv3 §13. Its LGPL-2.1+ parts and OpenSSL 3 (Apache-2.0) are compatible.
- FFmpeg must be configured **without** `--enable-gpl`/`--enable-nonfree`; that rules out libfdk-aac, whose licence is incompatible with the GPL. FFmpeg's native AAC decoder handles AAC-ELD.
- Use an **in-app licence screen**, reached from the idle screen by a long-press, so it doesn't count as a second control. It shows AGPLv3, the third-party notices, and the source URL with a commit hash per build. Also add a **custom EULA** in the Appstore console that defers to the AGPL, and **turn DRM off** (check what the console shows for a VPKG on the first upload).
- **CI must fail** if `nm -C` on `librosellaview.so` shows any `com::amazon` symbol, or if `readelf -d` shows an unexpected `NEEDED` entry.
- **Playfair.** `lib/playfair` ships in the binary openly, as AirRelay and the paid receivers do. We will *not* download the FairPlay tables at runtime: `research/07` found that would add risk under copyright, §1201 and Amazon's IAP terms without removing anything. Before charging money, get a lawyer's review of playfair's provenance and §1201 (`research/07` §5 lists the questions). Track `omarroth/doubletake` (LGPL-3.0+) as a possible smaller replacement.

## 3. The hard part: getting video on screen

UxPlay delivers decrypted Annex-B H.264 (or H.265) access units with NTP timestamps. Vega gives apps three possible ways to decode them. Choosing among them is Phase 0's job.

| Option | How | Pros | Unknowns |
|---|---|---|---|
| **A. W3C Media Source Extensions** (preferred) | Native code remuxes the NALs into fragmented MP4 (avcC/hvcC built from SPS/PPS; one `moof`+`mdat` per frame or GOP chunk) and hands `ArrayBuffer`s to JS, which appends them to a `SourceBuffer` on the Vega `VideoPlayer`. | Hardware decode, official API, overlay views work, and media playback keeps the 5-minute screensaver away. | End-to-end latency (MSE buffering; aim for ≤ 250 ms), whether `appendBuffer` accepts tiny fragments, whether HEVC MSE works, and JS bridge throughput at 1080p60 (~10–20 Mbit/s should be fine). |
| B. Native `IMediaFactory`/`IMediaTransform` | Feed NALs straight into the SDK's native codec interface and render to a media surface (`makeMediaSurfaceView`/`IMediaSurface`). | Lowest latency, no JS in the data path. | It's undocumented and **not in the OS 2.0 public compatibility contract**, so Appstore acceptance and stability across OS updates are at risk. |
| C. Software decode | FFmpeg H.264 decode, then GL ES texture upload. | No dependency on platform media. | 4× A55 at 1.7 GHz is marginal for 1080p60; GL linking under Strict ABI Check is unproven; the screensaver needs a media session. |

**Decided: A**, with about 0.45 s of player latency accepted (see `docs/DECISIONS.md` D1). B turned out to be unusable from React Native: native MSSE needs a native `IMediaSurface`, which a Turbo Module can't obtain for the RN window. C stays a fallback only.

**Ask the sender for H.264 only.** UxPlay can advertise H.265 support; leave that off until MSE HEVC is proven.

**Audio:**
- UxPlay reports `ct`: 1 = PCM, 2 = ALAC, 4 = AAC-LC, 8 = AAC-ELD. Mirroring uses AAC-ELD.
- Decode to PCM natively with FFmpeg (`aac`, `alac`).
- For output: preferred is the native audio output API (`VAudioStream`, which the survey found documented). The fallback is to wrap PCM or re-wrapped AAC-LC in the same MSE stream as an audio `SourceBuffer`.
- Keep A/V sync from UxPlay's NTP timestamps.

## 4. Phases

Each phase ends with something demonstrable on the real stick: 10.42.24.48 in developer mode, connected via `vega exec vda connect`.

### Phase 0: device probes (de-risking, about a week)

Probe code is throwaway, kept in `probes/` or in `~/vega-probe`.

1. **MSE smoke test:** a JS-only `VideoPlayer` with an MSE `SourceBuffer`, appending a canned fMP4 H.264 clip in 1-frame fragments. Measure glass-to-glass latency with a test pattern, and measure overlay compositing with an 80%-opacity bouncing view on top.
2. **Native → JS `ArrayBuffer` handoff:** use the `tm-napi` pattern to push 20 Mbit/s of buffers through a threadsafe function and check for jank and GC pressure.
3. **Audio output:** play PCM via `VAudioStream`, or failing that via MSE. Check latency and whether it holds a media session (does the screensaver stay off?).
4. **mDNS co-existence with the real libairplay:** advertise `_airplay._tcp`/`_raop._tcp` from inside the app and check that an iPhone lists the receiver.
5. **Strict ABI Check:** run the SDK's validator on a build that statically links OpenSSL 3, libplist and FFmpeg.
6. **IAP:** create the entitlement in the developer console. Use the SDK's IAP test tooling (App Tester or the equivalent; check the docs) to purchase and restore from JS.

Exit criteria: pick a video backend, an audio backend and a latency budget. Record them in `docs/DECISIONS.md`.

### Phase 1: the build skeleton

- Top-level CMake builds libplist, OpenSSL (libcrypto only, `no-shared`) and the UxPlay `lib/` target `airplay`, with `-DNO_MARCH_NATIVE`, mdnsd on and dns_sd off, for armv7 (and aarch64/x86_64, since the SDK builds all three) using the SDK toolchain.
- Add `librosellaview.so` (bridge plus receiver stubs), the React Native app skeleton (package ID TBD, see §6), a `package.json` workflow that installs the module as an `npm pack` tarball (not a `file:` symlink, which breaks at runtime), and the CI checks from §2.1.
- Demo: the app launches and advertises; an iPhone shows `RosellaView` in Screen Mirroring; selecting it reaches `conn_init` (logged). No video yet.

### Phase 2: first pixels

- The receiver hands video to the chosen backend.
- The JS `MirrorScreen` appears on `mirror_video_running(true)` and returns to idle on `conn_destroy`.
- Handle orientation and resolution changes (`video_set_codec_and_size`), stream resets and flushes.
- Demo: iPhone mirroring on the TV, and latency measured.

### Phase 3: audio and sync

- Decode AAC-ELD to PCM and output it, with A/V sync from NTP timestamps.
- Volume: map `audio_set_volume` to the stream gain.
- Demo: video with lip-synced audio.

### Phase 4: product polish

- Watermark overlay plus IAP purchase and restore.
- Mac sender support (it uses the same protocol; test macOS 15/26).
- PIN pairing if needed (`display_pin`). UxPlay can require a PIN; default off, with a PIN on screen if the sender asks.
- Sleep and screensaver behaviour, app backgrounding (drop the connection cleanly), and reconnect handling.
- The licence screen.

### Phase 5: store

- Run Live App Testing first (VPKG supported, up to 1,500 testers) to confirm that an Appstore-signed build keeps the same socket behaviour as the developer-mode sideload. This is the one thing we couldn't test.
- Then do the listing, reviewer notes, privacy policy (no data collected; LAN-only processing; mDNS advertisement), custom EULA, DRM off, and submission.

### Later

- AirPlay audio-only mode (music from Control Center).
- HLS "AirPlay video" URL playback (`on_video_play`, already in libairplay), which the W3C player can play directly.
- Upstreaming portability fixes to UxPlay.
- A DLNA renderer mode sharing the same shell.
- *Low priority:* find out whether `MirrorPlayer`'s back-buffer trim is still needed now that senders send keyframes every 10 s (DECISIONS.md D6). See D6's "To revisit".

## 5. Upstream and submodule policy

This follows the Blackb0x-- precedent.

- `third_party/UxPlay` points at our fork `https://github.com/regulad/UxPlay.git`, branch `rosella`. That's upstream `master` at `05b0275` (2026-11-06), because `lib/mdnsd` isn't in a release yet, plus two commits: `report_client_details` (the sender's OS and AirPlay versions, for crash reports), and the event channel with `raop_request_keyframe()` (DECISIONS.md D7). Rebase `rosella` onto the first release tag that contains `mdnsd` when one appears, and offer both upstream.
- As soon as any change inside UxPlay is needed (expected: build-system tweaks for the Vega toolchain, maybe a hook in `lib/` for codec advertisement), **fork to `regulad/UxPlay`** on a `rosella` branch, keep the diff small and clean, and point `.gitmodules` at the fork with `branch = rosella`. Never leave uncommitted patches in the submodule.
- Tell FDH2 (UxPlay's maintainer) about the port early, be upfront that it's a paid-unlock app built on his work, and offer portability patches back.
- Add more submodules as needed: `third_party/libplist` (libimobiledevice, LGPL-2.1+), `third_party/openssl` (3.x LTS tag), `third_party/ffmpeg` (LGPL configuration, decoders only), and `third_party/node-api-headers` (MIT).

## 6. Open decisions

| Decision | Default until decided |
|---|---|
| Package ID | `xyz.regulad.rosellaview` (reverse-DNS of regulad.xyz, without "amazon"). It's permanent once published. |
| Receiver name shown on iPhone | `RosellaView` |
| Logo / watermark art | Placeholder wordmark until real art exists |
| Price of the unlock | TBD (set in console) |
| H.265 | Off until MSE HEVC is proven |
| Name clearance | Knockout search done (`research/08`); a lawyer's full search before any filing |
| Lawyer review of playfair | Before the IAP goes live |
