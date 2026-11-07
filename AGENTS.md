# AGENTS.md: RosellaView

A RosellaSoftware app, published by Parker Wahle <regulad@regulad.xyz>: a screen-mirroring receiver (iPhone, iPad, Mac → TV) for Fire TV devices on Vega OS, built on UxPlay's protocol library. The roadmap and the reasoning behind each decision are in [`docs/PLAN.md`](docs/PLAN.md).

## Conventions (don't re-litigate without asking)

- **The UI is as small as possible.** It has an idle screen ("Open Screen Mirroring and select <name>" plus a single "Remove watermark" button), the mirroring screen, and the bouncing 80%-opacity watermark until the IAP entitlement is owned. No settings screens or menus. Add UI only when the owner asks.
- **No Amazon code in our binaries.** The native module is a pure Node-API addon. It is built against `third_party/node-api-headers` (MIT), links only the SDK's `turbomoduleAPI::napiStub` (the OS's `libkeplerscript.so`), and registers via `autoLinkKeplerTurboModulesV1()` → `napi_module_register()`. JS reaches it via `globalThis.__turboModuleProxy`, never via Amazon's `TurboModuleRegistry` or the SDK's `libturbomoduleAPI.a`. CI enforces this: no `com::amazon` symbols, and only allow-listed `NEEDED` entries.
- **Every third-party dependency is a git submodule under `third_party/`, built from source and statically linked.** If a vendored library needs a code change, fork it to `regulad/<name>` on its own branch (e.g. `rosella`), point `.gitmodules`'s `url`/`branch` at the fork, and keep the diff small and clean. Never leave uncommitted changes inside a submodule.
- **UxPlay is used for `lib/` only.** `renderers/` and `uxplay.cpp` (GStreamer, X11, the CLI) are not built. Our receiver implements `raop_callbacks_t` itself.
- **Licence: AGPL-3.0-or-later** (`LICENSE.md`, the FSF Markdown text). Don't add dependencies whose licence is incompatible with the GPL, e.g. libfdk-aac or a GPL/non-free FFmpeg configuration.
- **Never mention "Vega" in Appstore listing text, and never put "AirPlay" or Apple's glyph in the app's name or icon.** Description wording is in `docs/PLAN.md` §1.3.

## Node-API runtime quirks (each one crashed the app)

- `napi_create_async_work`/`napi_queue_async_work` abort. Use your own `pthread` and settle via `napi_threadsafe_function`.
- `napi_get_value_string_utf8(env, v, NULL, 0, &len)` (the length query) aborts. Pass a real buffer.
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
