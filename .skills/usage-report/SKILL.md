---
name: usage-report
description: Count RosellaView's users and summarize how they use it (installs, launches, connections, mirroring time, sender devices, purchases, failed or cut-short sessions) from what the app already sends to Sentry. Use when asked how many users there are, for analytics or usage numbers, or how a store build is doing. Read-only.
---

# Usage report from Sentry

RosellaView sends no user fields (`dataCollection: {userInfo: false}` in `app/src/Sentry.ts`) and no device IDs, as `docs/PRIVACY.md` promises. So Sentry's "users" columns are empty, and release health came back empty through the Sentry MCP (2026-11-15). Most of what follows is worked out from logs and metrics, one launch at a time. **Don't add identifiers to get better numbers:** the owner decided against it, and it would need a privacy-policy change.

**Streaming sessions** (builds after 4): each one is a transaction named `streaming session` (op `receiver.session`), from the first sender connecting to the last one leaving or the app closing. Its attributes, also sent as tags, are `device.model` (the Fire TV), `premium` (the unlock owned), `session.kind` (`connected`, `audio` or `mirroring`), `sender.kind` (iPhone, iPad, Mac…) and `session.end` (`disconnected` or `app_closed`). Count and group them with `search_traces`, e.g. query `transaction:"streaming session"` with fields `premium, device.model, count()`, or `session.kind, avg(span.duration)`. A session whose launch the OS killed never ends, so it's missing here; the logs still show it. It shares its launch's trace ID, so `trace:<id>` finds that launch's logs.

**Where:** org `regulad`, project `rosellaview`, `environment:production`. Other environments (`development`, `soak`, `soak-b`) are the owner's own testing. Production data before go-live (2026-11-12) is Amazon's review. Sentry keeps 90 days.

**The store build** is the newest `rosellaview@<n>` with no `+` suffix in `find_releases`. Versions with `+<sha>` are local builds.

**Tools:** the Sentry MCP's `search_logs`, `search_metrics`, `search_errors` and `find_releases`. Pass Sentry search syntax in `query` with explicit `fields` and `sort`. A natural-language query is sometimes used literally and returns nothing. Results stop at 100 rows, so narrow by release or period when a query hits that.

**Event attributes:** each `receiver <event>` log carries that event's fields as attributes. String fields work by name (`os_version`, `codec`, `sender_sps`, `format`). Numbers and booleans are rejected as "Unknown attribute" unless typed: `tags[ct,number]`, `tags[using_screen,boolean]`, `tags[reset_type,number]`. `native_status`'s text is `tags[message,string]`; plain `message` is the log line itself.

## 1. Signals

| What | How to tell |
| --- | --- |
| A launch | Each launch has its own trace ID. Count `start(RosellaView) -> true` lines. |
| A new install | `libairplay: *** A new Public Key has been created and stored in /data/rosellaview/pairing.key`, on the first launch after an install. The About screen's identity reset logs it again (`identity reset -> true` in the same trace), so count traces, not lines. A reinstall counts as a new install. |
| A returning launch | A trace with `start` but no new-key line. Nothing says which install it belongs to. With few users, timing (a relaunch minutes later) is the only hint. |
| A phone connecting | `receiver conn_init`, then `av delay for <model>: <n>ms`. Logs and metrics during a connection carry `sender.model` (e.g. `iPhone14,5`) and `sender.user_agent` (`AirPlay/<version>`). |
| Mirroring or audio only | `libairplay: raop_rtp_mirror starting mirroring` means the phone asked for Screen Mirroring; `receiver first_video_frame` and `receiver mirror_video_running` follow. If there's only `libairplay: raop_rtp starting audio`, check `receiver audio_format`: `using_screen` false (with `is_media` true) is AirPlay audio from an app or the audio picker, and the TV shows "Playing audio from…". That's working as designed, not a failed mirror. Only a mirroring request with no first frame is a failure. |
| Sender's OS | `receiver client_details` carries `os_name` and `os_version` (e.g. `iPhone OS`, `18.7.8`). |
| Time connected | Playback metrics arrive every 5 s while connected: the number of `av.delay` samples × 5 s. Samples with `video.fps` above 0 give the time with video. |
| Video quality | `video.fps`, `video.fps_shown`, `video.dropped`, `video.native.errors`, `audio.concealed`. |
| Purchases | `purchase -> code <n>`, a `PurchaseResponseCode` (`dist/types/IAPTypes.d.ts` in `@amazon-devices/keplerscript-appstore-iap-lib` 2.13.0): 0 SUCCESSFUL, 1 ALREADY_PURCHASED, 2 INVALID_SKU, 3 NOT_SUPPORTED, 4 FAILED. FAILED includes the customer pressing Back on the payment screen, and nothing tells the two apart (Amazon's IAP docs; an open Amazon developer forum request). A missing Appstore public key also gives 4 (SUBMISSION.md). `unlocked=true/false` after launch shows the entitlement. |
| Settings | `compatibility mode on/off`, `identity reset -> true`. |
| A clean exit | `receiver stopped`, from `Receiver.stop()` on unmount. If logs and metrics stop in the middle of a connection without it, the launch was cut short. Native crashes (SIGABRT) from this device do reach Sentry on the next launch, so if none arrives, the process was most likely killed (SIGKILL) rather than crashed. |
| Back in the foreground | Each return to the foreground logs another `unlocked=…` line in the same trace. A fresh `start` instead means the old process was gone. |
| Errors and crashes | `search_errors` with `environment:production`. Native crashes are sent on the next launch. Only error events carry the Fire TV model and OS version; logs and metrics don't. |

## 2. Queries

All are `search_logs` unless marked; add `period` (`14d`, `30d`).

1. **New installs:** query `environment:production message:"*new Public Key has been created*"`, fields `timestamp, message, trace, sentry.release`, sort `timestamp`.
2. **Launches:** query `environment:production message:"start(RosellaView)*"`, same fields.
3. **Everything one build did, per launch:** query `release:rosellaview@<n> environment:production !message:"libairplay:*"`, fields `timestamp, message, trace`, sort `timestamp`. Group the rows by trace. For one launch's full detail, query `trace:<id>` and keep the `libairplay:` lines.
4. **Senders:** query `has:sender.user_agent`, fields `sender.user_agent, sender.model, sentry.environment, count()`, sort `-count()`. An AirPlay version seen in production but never in `development` or `soak` hasn't been tested.
5. **Mirroring time and quality** (`search_metrics`): query `release:rosellaview@<n> metric.name:[av.delay,video.fps,video.fps_shown,video.dropped,audio.concealed]`, fields `timestamp, metric.name, value, trace, sender.model`, sort `timestamp`.
6. **Volume per build:** fields `sentry.release, sentry.environment, count()`, sort `-count()`.
7. **Errors:** `search_errors`, query `environment:production`, fields `release, count()`.

## 3. Report

1. **One row per install:** first seen (UTC), launches, and what happened (sender model and AirPlay version, whether video showed and for how long, purchases, settings changed, clean exit or not).
2. **Totals since go-live:** installs, launches, connections, time connected and time with video, purchases, errors.
3. **What to look into,** each with its trace ID: mirroring requests that never showed video, launches that ended mid-connection, errors, AirPlay versions the owner hasn't tested. Read a launch's full trace (including the `libairplay:` lines and the `audio_format` attributes) before calling it a failure.
4. **One line of caveats:** every count is a minimum, because launches without network access aren't sent; reinstalls count twice; the owner's own store installs are included unless they say which ones are theirs.

Link the Sentry Explore URL that each query returns, so the owner can open it.
