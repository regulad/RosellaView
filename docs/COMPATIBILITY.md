# What works with RosellaView

RosellaView mirrors the screen of an iPhone, iPad or Mac and plays its audio. For the Appstore listing and support replies; protocol details are in `.claude/docs/AIRPLAY.md`.

## Works

- **Screen mirroring** from iOS and macOS, in portrait and landscape.
- **Video inside mirrored apps** that iOS mirrors as part of the screen, such as Photos, Safari and YouTube.
- **Full-screen video from a Mac** (any frame rate).
- **Audio-only AirPlay** (music, podcasts).
- **The full range of brightness from iPhones and iPads**: near-black and near-white shades stay distinct.
- **The spoken "Preview."** (until the watermark is removed) is heard only while the sender is playing sound.

## May not work

- **TikTok:** the phone shows its "playing on the TV" screen and the TV shows that screen without the video.
- **Copy-protected (HDCP) video,** such as most paid streaming apps (films and TV shows you rent or subscribe to). iOS leaves protected video out of mirroring.
- **If the picture doesn't appear** on some device, **Compatibility Mode** (About & privacy, on the start screen) plays it through the TV's standard video player instead. Then the darkest and lightest shades from iPhones and iPads can merge.
- **Other apps that "cast" or play their video "on the TV"** rather than letting the screen be mirrored. Like TikTok, they may show only their controls. RosellaView supports mirroring, not AirPlay video handoff.

## Tested against

From Sentry (sender model, OS and AirPlay versions, as reported by the senders), 2026-11-07:

| Device | Model | Software | AirPlay |
|---|---|---|---|
| Fire TV Stick 4K Select (receiver) | — | OS 2.0 (TV Ship/532) | — |
| iPhone 17 Pro | iPhone18,1 | iOS 27.0.1 (24A446) | 980.77.1 |
| iPad mini 4 (Wi-Fi) | iPad5,1 | iPadOS 15.8.8 (19H422) | 620.8.2 |
| MacBook (Retina, 12-inch, Early 2015) | MacBook8,1 | macOS Big Sur 11.7.11 (20G1443) | 550.10 |

Update this (and the "Tested against" list in `assets/listing/long-description.txt`) when new senders are tested: query Sentry's `sender.model`, `sender.os_version`, `sender.os_build` and `sender.airplay_version` tags.
