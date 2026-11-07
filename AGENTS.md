# AGENTS.md: RosellaView

A RosellaSoftware app, published by Parker Wahle <regulad@regulad.xyz>: a screen-mirroring receiver (iPhone, iPad, Mac → TV) for Fire TV devices on Vega OS, built on UxPlay's protocol library. The roadmap and the reasoning behind each decision are in [`docs/PLAN.md`](docs/PLAN.md).

## Conventions (don't re-litigate without asking)

- **The UI is as small as possible.** It has an idle screen ("Open Screen Mirroring and select <name>" plus a single "Remove watermark" button), the mirroring screen, and the bouncing 80%-opacity watermark until the IAP entitlement is owned. No settings screens or menus. Add UI only when the owner asks.
- **GPL compliance is the rule.** The binary contains third-party GPLv3 code: `lib/playfair` (Esteban Kubata) and `lib/crypto.c` (RPiPlay: Florian Draschbacher, Jaslo Ziska; modified by F. Duncanh).
  - Nothing GPL-incompatible goes in unless it's a System Library (GPLv3 §1).
  - **System Libraries:** the OS's libraries loaded at runtime (`libkeplerscript.so`, `libapmf.so`, …), and the SDK's *generated interface glue* for calling them (APMF IDL import libraries, `kepler_add_idl_import_library`). That glue serves only to use the OS, like OS SDK headers and import stubs (DECISIONS.md D10).
  - **Not System Libraries:** bundled vendor *libraries* with their own functionality (the static `libturbomoduleAPI.a`, Play-Services-style client libraries). Don't add those.
  - **The native module:** a pure Node-API addon, built against `third_party/node-api-headers` (MIT). It links only the SDK's `turbomoduleAPI::napiStub` (the OS's `libkeplerscript.so`) and registers via `autoLinkKeplerTurboModulesV1()` → `napi_module_register()`. JS reaches it via `globalThis.__turboModuleProxy`.
- **Every third-party dependency is a git submodule under `third_party/`, built from source and statically linked.** If a vendored library needs a code change, fork it to `regulad/<name>` on its own branch (e.g. `rosella`), point `.gitmodules`'s `url`/`branch` at the fork, and keep the diff small and clean. Never leave uncommitted changes inside a submodule.
- **UxPlay is used for `lib/` only.** `renderers/` and `uxplay.cpp` (GStreamer, X11, the CLI) are not built. Our receiver implements `raop_callbacks_t` itself.
- **Licence: AGPL-3.0-or-later** (`LICENSE.md`, the FSF Markdown text). Don't add dependencies whose licence is incompatible with the GPL, e.g. libfdk-aac or a GPL/non-free FFmpeg configuration.
- **Never mention "Vega" in Appstore listing text, and never put "AirPlay" or Apple's glyph in the app's name or icon.** Description wording is in `docs/PLAN.md` §1.3.

## Node-API runtime quirks (each one crashed the app)

- `napi_create_async_work`/`napi_queue_async_work` abort. Use your own `pthread` and settle via `napi_threadsafe_function`.
- `napi_get_value_string_utf8(env, v, NULL, 0, &len)` (the length query) aborts. Pass a real buffer, and NUL-terminate it yourself at the returned length: Vega doesn't.
- `napi_create_arraybuffer` is "not implemented" (it logs an error and returns no buffer). Use `napi_create_external_arraybuffer` with a finalizer.
- `syslog()` from the app doesn't reach `loggingctl log -f`, but JS `console.log` does (first line only).

## Device loop

The SDK and CLI live in the `vega` distrobox (its home is `~/vega-probe/box-home`, so the SDK installer doesn't touch host rc files). The stick is in developer mode on the laptop's AP at `10.42.24.48:5555`.

```
distrobox enter vega -- bash -lc 'vega exec vda connect 10.42.24.48:5555'
distrobox enter vega -- bash -lc 'vega device install-app --packagePath <vpkg>'
distrobox enter vega -- bash -lc 'vega device launch-app --appName <component-id>'
distrobox enter vega -- bash -lc 'vega exec vda -s 10.42.24.48:5555 shell "loggingctl log -f"'
```

Reinstall native modules into the app from an `npm pack` tarball. A `file:` symlink bundles fine but fails at runtime with `Cannot find module`. After repacking a module at the same version, run `npm uninstall` and then `npm install file:<tgz>`, because npm otherwise keeps the cached copy.

## Building and Sentry

Use `scripts/build.sh [environment] [--install]` from the host. It:
- stamps the release (`rosellaview@<version>+<git sha>`, with a unique suffix for uncommitted builds) into `app/src/buildInfo.ts`, restoring the committed default afterwards
- builds the native module and the app (Hermes with `-output-source-map`)
- composes the Hermes and Metro source maps and uploads them, with the native debug files, to Sentry (org `regulad`, project `rosellaview`)

Its uploads need `sentry-cli login`; the token needs `project:write`, `org:read` and `project:releases` scopes. A plain `npm run build` works, but its reports aren't symbolicated.

- **JS** (`app/src/Sentry.ts`, `app/src/Telemetry.ts`) uses `@sentry/core` alone, with a fetch transport and a Hermes stack parser. The React Native SDK needs iOS/Android native code. It reports:
  - errors and unhandled rejections
  - receiver events as breadcrumbs and logs
  - sender details as context and tags
  - playback stats as metrics, every 5 s while connected
  - sessions, for release health
- **Native** (`native/src/sentry.c`) uses sentry-native with the inproc backend. Crashes go into `/data/rosellaview/sentry` and are sent on the next launch, through JS (`Receiver.onSentryEnvelope`); there's no native HTTP. On armv7, an `on_crash` hook rebuilds the stack from pc, lr and a stack scan, because sentry-native's unwinder gets nothing usable there.
- **Development hooks** (remove before submission): from the component shell, `touch /scratch/dev-js-error` or `/scratch/dev-native-crash`, then relaunch. The app throws, or crashes natively, 5 s after start, to test the whole path.
  - `touch /scratch/dev-mse-video`: use the MSE video fallback instead of the native pipeline (DECISIONS.md D12).
  - The native surface logs to `/scratch/rosella-surface.log` (read it from the component shell).
