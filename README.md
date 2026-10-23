# panDXVK

panDXVK is a specialized DXVK translation layer built exclusively for the panVK (Panfrost / ARM Mali Vulkan) driver.

Built on [pythonlover02/dxvk-sarek v1.14.1](https://github.com/pythonlover02/dxvk-sarek/tree/v1.14.1), which carries [DXVK v1.10.3](https://github.com/doitsujin/dxvk/tree/v1.10.3) forward with Sarek's own pipeline and renderer work. Adds a BC texture → ASTC 4x4 transcode pipeline so that PC games using BC1–BC7 compressed textures can render correctly on Mali GPUs. PanVK does not report `textureCompressionBC`; where a wrapper advertises it back instead, the transcode is activated with `PANDXVK_FORCE_TRANSCODE=1`.

> **This build runs on some panVK versions and not on others.** The difference is a driver capability, not a packaging fault, and one of the environment variables below is essential on the builds where it fails. Read [Notes](#notes) and the [release notes](https://github.com/isygold/panDXVK/releases) before installing.

## Key Features
- **Sarek v1.14.1 Base**: The translation layer tracks Sarek rather than DXVK 1.10.3, gaining lock-free pipeline instance lookup, `VK_EXT_graphics_pipeline_library` plumbing and the upstream renderer fixes accumulated since 2022.
- **gplasync Pipeline Compiler**: Asynchronous pipeline compilation, available by opt-in — building and linking libraries instead of monolithic pipelines so a draw never stalls waiting for one. Chosen over Sarek's dyasync after back-to-back comparison. Synchronous unless a config key or environment variable enables it (see [Configuration](#configuration)).
- **Parallel BC→ASTC Transcode**: Block-compressed textures are transcoded across all cores at upload time. Bit-exact against the reference decoder on all 13 format cases and 3.11–3.63× the throughput of the single-threaded encoder.
- **BC→ASTC Transcode**: Software decode of BC1–BC7 to RGBA8, then encode to ASTC 4x4. Mali hardware decodes ASTC natively.
- **Automatic Detection**: Transcode activates when `DxvkAdapter::isPanVk()` (ARM Mali, vendor ID `0x13B5`) AND `textureCompressionASTC_LDR` is available AND (`PANDXVK_FORCE_TRANSCODE=1` OR `textureCompressionBC = false`).
- **Format Remap**: VkImage, SRV, RTV and creation-time initializer uploads are remapped from BC to ASTC transparently. No game-side changes required.
- **Spec-Conformant Encoder**: ASTC 4x4 block mode `0x241`, field layout assembled per `GL_KHR_texture_compression_astc_ldr` C.2.6 / Table C.2.7. Verified **6/6** against basis-universal.
- **ASTC Spec Gate in CI**: `tests/host/rmchk.cpp` decodes block modes from the spec, sharing no code with the encoder, and fails the build if the shipped mode or the `R` field reading drifts. Both cross-build jobs depend on it.
- **Heap-Allocated Buffers**: RGBA8 intermediate and ASTC output are heap-allocated to avoid stack overflow on DXVK's small thread stacks.
- **Transcode Timing**: Per-subresource transcode time for profiling, compiled out of release builds (`#ifndef NDEBUG`). Enable with `DXVK_LOG_LEVEL=debug`.
- **x64 + x32**: Both 64-bit and 32-bit DLLs built and verified.

## Installation (Winlator / Bannerlator)

**Option A — WCP (recommended):**
1. Download `wcp.zip` from the [Releases](https://github.com/isygold/panDXVK/releases) page.
2. In Winlator/Bannerlator, go to **Container Settings → Content**.
3. Tap **Install** and select the downloaded `wcp.zip`.
4. Launch your game. panDXVK detects Mali and remaps BC textures to ASTC. If your setup reports `textureCompressionBC = 1` (wrapper), set `PANDXVK_FORCE_TRANSCODE=1` to activate the transcode.

**Option B — Manual DLL replacement:**
1. Download `merged.zip` (x64 + x32 DLLs) from the [Releases](https://github.com/isygold/panDXVK/releases) page.
2. Extract the archive.
3. Inside Winlator/Bannerlator, go to **Container Settings → Advanced → DXVK/Sarek**.
4. Replace the existing `d3d11.dll`, `dxgi.dll`, `d3d10.dll`, `d3d10_1.dll`, `d3d9.dll` with the ones from the extracted folder matching your container's architecture:
   - 64-bit containers → `x64/` folder
   - 32-bit containers → `x32/` folder
5. Alternatively, copy the DLLs into the Wine prefix's `system32` (64-bit) or `syswow64` (32-bit) directory.
6. Launch your game.

## Configuration
panDXVK uses the same configuration mechanism as upstream DXVK. Set `DXVK_CONFIG_FILE` to point to a `dxvk.conf` file, or use environment variables:

| Variable | Values | Description |
|----------|--------|-------------|
| `DXVK_HUD` | `devinfo`, `fps`, `frametimes`, `full`, etc. | HUD overlay. See upstream docs. |
| `DXVK_LOG_LEVEL` | `none`, `error`, `warn`, `info`, `debug` | Logging verbosity. Transcode markers (`remap VkImage`, `initializer upload`, `SRV`/`RTV BC→ASTC`) need `debug` — they are runtime-gated, so no special build is required. |
| `DXVK_LOG_PATH` | path | Directory for log files. |
| `DXVK_FRAME_RATE` | `0` (uncap), or FPS limit | Frame rate cap. |
| `PANDXVK_FORCE_TRANSCODE` | `0`, `1`, or any value other than unset/empty/`0` | **Essential on wrapper setups.** Forces BC→ASTC even when the driver claims BC support, which is what actually activates the transcode where a wrapper's BCN layer fakes the feature. Adds CPU overhead and ASTC loss — leave it unset on builds that already transcode automatically. Needs game restart. ⚠️ **Never combine with an updated wrapper BCN layer** — see [Why Some Users Need the BC Wrapper](#why-some-users-need-the-bc-wrapper-and-others-dont). |
| `PANDXVK_TELEMETRY` | `1` | Enables the CSV timeline and report writer. Off by default. |
| `PANDXVK_REPORT_DIR` | path | Directory for telemetry output. |

### Pipeline compilation

Synchronous by default — the draw that needs a pipeline builds it inline. Every row below is
opt-in and none is required for a normal setup. Each can be given as a `dxvk.conf` key or as
the environment variable beside it.

| `dxvk.conf` key | Environment variable | Default | Description |
|----------|--------|-------------|-------------|
| `dxvk.enableAsync` | `PANDXVK_ASYNC` | `False` | Compile pipelines on worker threads instead of stalling the draw. |
| `dxvk.enableGplAsync` | `PANDXVK_GPLASYNC` | `False` | The same, using graphics pipeline libraries. Consulted only when `gplAsyncMode` is `0`, in which case it is treated as mode `1`. |
| `dxvk.gplAsyncMode` | `PANDXVK_GPLASYNC` | `0` | `0` off, `1` async, `2` async with graphics pipeline libraries. Any value ≥ 1 turns `enableGplAsync` on. The two live modes are described below. |
| `dxvk.enableDyAsync` | `PANDXVK_DYASYNC` | `False` | Dyasync: draw with a fallback pipeline for this render pass while the real one compiles, instead of dropping the draw. The first draw of a pass still compiles inline, because no fallback exists yet. When set, this takes the branch and `PANDXVK_ASYNC` is not used. |
| `dxvk.numAsyncThreads` | — | `0` | Worker threads. `0` picks half the CPU cores, clamped to 1–8. |
| `dxvk.asyncLog` | `PANDXVK_ASYNC_LOG` | `False` | Log compilation, and warn on the first ten draws dropped for invalid pipeline state. |
| — | `PANDXVK_GPL_FASTLINK` | unset | Experiments with the faster GPL link path. |

The two live modes differ in *how* a missing pipeline is built:

- **`1`** queues the draw to worker threads and compiles one monolithic pipeline, then writes
  every background-compiled pipeline to the DXVK state cache so the next launch precompiles
  them and skips far fewer draws. `dxvk.enableAsync` on its own gives the worker threads and
  nothing more, so **its skipped draws recur on every launch** — that is the documented
  difference between basic async and the gplAsync modes (`docs/async-notes.md`, §3.1 and
  §3.2), which makes `enableAsync` alone a poor choice in practice. Use mode `1` or `2`.
- **`2`** does the same, but builds the four stages separately — vertex input, pre-raster,
  fragment shader, fragment output — and links them. It probes the driver for
  `VK_EXT_graphics_pipeline_library`, fast linking and independent interpolation decoration at
  startup, logs which of the three it found, and quietly runs mode `1` if any is missing. If
  linking proves unreliable in practice it falls back to a full compile per pipeline and
  **switches the library path off by itself after 8 such failures** — asynchronous
  compilation stays on, only the libraries stop. Only mode `2` prints library statistics at
  shutdown, covering libraries created and reused per stage, pipelines linked, fallbacks to
  full compile, library and link time, and whether the path was still on at teardown.

The startup line `panDXVK: async=<0|1> gplasync=<0|1|2>` reports what the game actually took
— read that rather than assuming your config applied.

## Version String

The HUD and the startup log carry the same two-part identity:

```
panDXVK v01 (bab7459)      ← HUD
panDXVK: v01 (bab7459)     ← log
```

| Part | Example | Meaning |
|------|---------|---------|
| **release** | `v01` | The panDXVK release, derived from the git tag `v01`. Built on the tag itself it is exactly `v01`. Built on a later commit it becomes the full `git describe` string (`v01-6-gbab7459`) instead, because `panDxvkRelease()` pins only on the `-panVK.` tag shape and passes anything else through verbatim — so the release field is exact at tag time and descriptive after it. |
| **commit** | `bab7459` | Short hash of the exact commit, so a report resolves to a commit in one step. |

The log also prints a second line with the raw `git describe` output, which on a post-tag
build carries the commit count and hash (`DXVK: v01-6-gbab7459`) and on a tagged build is
the tag alone (`DXVK: v01`).

Both values are derived at build time, so **no manual bump is needed per release**:

| Where | What |
|-------|------|
| `meson.build` | `git describe --tags` → `version.h` (`DXVK_VERSION`); `git rev-parse --short HEAD` → `pandxvk_version.h` (`PAN_DXVK_COMMIT`) |
| `src/util/util_version.h` | passes `DXVK_VERSION` through `panDxvkRelease()` and appends `(commit)` |
| `src/dxvk/dxvk_instance.cpp` | startup log line |
| `src/dxvk/hud/dxvk_hud_item.cpp` | HUD line |

> **`--tags` is mandatory.** panDXVK releases are **lightweight** (unannotated) tags, and plain `git describe` refuses to use them and fails instead. Meson then silently falls back to the project version, which is why older builds logged a bare `DXVK: v1.10.3` with no release suffix at all.

## Notes
- **It works on some panVK versions and not on others.** Transcode only enables when three conditions hold together: the adapter is Mali (`0x13B5`), the driver reports `textureCompressionASTC_LDR`, and either the driver does **not** report `textureCompressionBC` or `PANDXVK_FORCE_TRANSCODE` is set. A wrapper's BCN layer makes the driver report BC, which closes the automatic arm — **on those builds `PANDXVK_FORCE_TRANSCODE=1` is what actually activates the transcode, and without it you get black or garbled textures.** If your driver reports no ASTC, nothing can help. Read the `panDXVK: BC→ASTC gate:` line rather than inferring your case.
- **Check `WRAPPER_NO_PATCH_OPCONSTCOMP` before the session.** It is a **wrapper-side** variable — panDXVK contains zero references to it and can neither see nor set it — and Winlator Mali defaults it to `1`. The value published for panDXVK is **`WRAPPER_NO_PATCH_OPCONSTCOMP = 0`**; Bannerlator exposes the same toggle as "Don't patch OpConstantComposite". Because it is wrapper-side you must set it before launch — it cannot be diagnosed out of a log afterwards. The short spellings `WRAPPER_NO_PATCH` and `wrapper_no_patch` are read by nothing; only the full name reaches the wrapper.
- **Pipeline compilation is synchronous unless you opt in.** With no setting the first draw that needs a pipeline state builds it inline. Turning on `dxvk.enableAsync`, `dxvk.enableGplAsync` or `dxvk.gplAsyncMode` hands that work to worker threads, trading a multi-second hitch on first use of a material for a brief lower-detail frame — titles sensitive to a skipped first draw may show a one-frame artefact on scene load. The keys and their environment-variable equivalents are tabulated under [Configuration](#configuration), including `dxvk.enableDyAsync` and `PANDXVK_DYASYNC`, which draw a fallback pipeline for the render pass instead of dropping the draw.
- **I need your logs.** If you hit a crash, rendering glitch, or anything weird, grab the log file from your Wine prefix's drive_c (usually `wine_debug.log` or `d3d11.log` in the app directory) and paste it to [panDXVK Logs](https://github.com/isygold/panDXVK-logs/issues). For BC→ASTC transcode diagnostics, set `DXVK_LOG_LEVEL=debug` — the markers are runtime-gated, so no special build is needed. Without logs, I cannot help you.
- **ASTC 4x4 is lossy.** BC1–BC7 textures are decoded to RGBA8 and re-encoded to ASTC 4x4. This introduces compression artifacts not present in the original. For most games the visual difference is minimal, but texture-heavy UIs or screenshots may show subtle banding.
- **BC6H maps to ASTC 4x4 LDR.** BC6H (HDR float RGB) is clamped (negatives/NaN to 0, highlights saturate) and approximated as ASTC 4x4 UNORM. Full HDR fidelity is not preserved.
- **panDXVK detects Mali, not a particular driver.** `isPanVk()` tests the ARM vendor ID `0x13B5`, so it returns true for the blob driver and for PanVK alike — it cannot tell the two apart. If the device reports `textureCompressionBC = true` (typical with a wrapper's BCN layer) and force is off, the transcode is skipped and BC textures are uploaded as-is. Set `PANDXVK_FORCE_TRANSCODE=1` to force it (test mode, expect CPU overhead). **In wrapper setups the automatic path stays closed, so force is what actually activates the transcode.**
- **Transcode happens at CPU time.** Each `UpdateTexture` call triggers a full BC decode + ASTC encode on the CPU. Large textures (4K+) may take 10–30ms per subresource on mobile CPUs. This is a one-time cost per texture load, not per frame.
- **TBDR architecture.** Mali is a tile-based deferred renderer. ASTC textures are natively supported by the tile buffer. No special TBDR handling is needed for the transcode path — the ASTC data is uploaded via standard `vkCmdCopyBufferToImage` and decoded by the texture unit before fragment processing.
- **AppendSlice path not yet patched.** The `AppendSlice` D3D11 path may also encounter BC textures. This is a known gap. If you see BC format errors in `AppendSlice`, file an issue.
- **CI builds are automated.** The workflow runs the `ASTC spec gate` (`tests/host/rmchk`) first, then builds both x64 and x32 on Fedora 44 with MinGW-w64 — a gate failure blocks both builds. Release builds set `-Db_ndebug=true`. If the Fedora mirror is temporarily unreachable, the workflow retries automatically.

## Why Some Users Need the BC Wrapper and Others Don't

The wrapper does this:
- It's a Vulkan layer that tells the GPU: "I support BC textures"
- panDXVK sees this and says: "OK, I'll skip the transcode and upload BC textures directly"

The problem:
- On some devices, the blob driver actually handles BC textures fine → game works
- On other devices, the driver genuinely can't handle BC → game breaks

So:
- Users where it works = their device's blob driver secretly supports BC even though PanVK doesn't
- Users where it doesn't work = their device truly can't handle BC textures

The fix:
The transcode gate is kept intentionally — modern wrappers carry the BCN layer, so the pipeline stays dormant behind `!textureCompressionBC` until a wrapper-free device appears. **In practice that means the automatic arm does not open on a wrapped device, and `PANDXVK_FORCE_TRANSCODE=1` is what actually activates BC→ASTC.** Release builds now set `-Db_ndebug=true`, so per-block timing, statistics and the encoder's runtime `assert()`s are compiled out. The four transcode markers (`remap VkImage`, `initializer upload`, `SRV`/`RTV BC→ASTC`) are deliberately *not* `NDEBUG`-gated — they are functional evidence, not diagnostics — and are gated at runtime on `DXVK_LOG_LEVEL=debug`, so they cost nothing by default but remain available on demand.

> **⚠️ Do not pair the transcode with an updated wrapper BCN layer.**
>
> Charan's build (`GunaCharanTeja/WinlatorMali`, branch `bionic-mali-1.0`) carries an
> updated wrapper BCN layer that is already verified working as intended — it performs the
> same BC→usable-texture conversion in effect that panDXVK's transcode performs. Setting
> `PANDXVK_FORCE_TRANSCODE=1` alongside it puts both mechanisms on the same textures, and
> they conflict.
>
> **Use one or the other, never both.** If your setup's wrapper BCN layer is already
> delivering correct textures, leave `PANDXVK_FORCE_TRANSCODE` unset. Set it only where a
> wrapper advertises `textureCompressionBC` but its layer does *not* produce working
> textures — in that case the flag is what activates BC→ASTC.

## Validation Status

> **panDXVK v01 is not yet device-validated.** It is a large cross-project merge that
> has passed structural verification only — no unresolved conflicts, balanced sources, no
> references to removed symbols — and the CI build is the gate that confirms it compiles.
> The results below are from the **v1.10.3** line and describe the transcode path, which
> this release inherits, not the 1.14.1 merge itself.

**v1.10.3-panVK.8 — BC→ASTC proven end to end on device:**

| Device / driver | Title (build) | `remap VkImage` | errors / backtraces |
|---|---|---|---|
| Mali-G52 MC2 / 49.1.0 | Nine Sols (v8) | **715** | 0 / 0 |
| Mali-G52 MC2 / 49.1.0 | Hollow Knight: Silksong (v6) | 227 | 0 / 0 |
| Apex(Mali-G615 MC6) / 44.1.0 | GTA V (v8) | **295** | 0 / 0 |

- Testers (Gustave and others) confirm textures render cleanly and substantially better than prior releases.
- Independent spec decoder **6/6** vs basis-universal (pre-fix layouts **2/6**); `rtr2/format_gate` **100/100**; `qdiag` **39.92 dB** on `crop_clean`, 0/2820 failures; CI `ASTC spec gate` green.
- All of these ran with the wrapper active (`textureCompressionBC = 1`), so the automatic arm was closed and the runs used `PANDXVK_FORCE_TRANSCODE=1`.

Earlier / non-BC results:
- AIO Graphics Test on Mali-G615 (PanVK 26.2.99 and blob 44.1.0) and Mali-G720 — init parity only.
- 70 FPS panDXVK vs 51 FPS stock DXVK on AIO spin-cube (non-BC test, Us5rman).
- Mali-G99 MC3: 700+ FPS AIO with P11, 900+ FPS with P9 (Proton 9 ARM64EC).

Current state:
- Real Mali hardware has no BC support (gpuinfo + leegao unsupported-device list) — the `= 1` in tester logs comes from the wrapper layer.
- Because the wrapper reports BC, the automatic `!textureCompressionBC` arm never opens in-container; force is what activates the transcode.
- Transcode hot spots optimized: word-level bit extract/insert, verified bit-identical over 12,298 checks.

Not pursued (deliberately left open — hard to arrange, not a blocker):
- A wrapper-free run confirming `textureCompressionBC = 0` in `d3d11.log`.
- Lossless screenshot pair; Lloyd GTA V `logcat`/tombstone; DMC5 log; Duckten logs.

> Reading a `d3d11.log`: `panDXVK: BC→ASTC gate: BC=… ASTC_LDR=… force=… -> transcode ENABLED/disabled` is printed once at device creation and states the gate's inputs directly — read it instead of inferring them. The `PANDXVK_FORCE_TRANSCODE` one-time notice only fires on the `UpdateSubresource` path, so titles that upload through the initializer show **zero** of them even with force on. The authoritative per-texture markers are the four `BC→ASTC` lines above.

## Testers' Findings

Reported by testers in the field — recorded here as observed, not independently measured:

- ✅ **The transcode resolves Mali's black / bad graphics.** Testers found that running BC textures through the transcode actually fixed the black or badly rendered graphics commonly seen on Mali, which come from BCN not being supported at hardware level in the first place.
- ⚠️ **The BCN software layer is still not improved.** The wrapper's BCN software layer remains unimproved, with the reported exception of Charan's build. *(reported, not independently verified.)*
- ✅ **RAM usage dropped ~5–8%.** Tested **twice by Gustave**, both runs landing in the **5–8%** range. Tester-measured rather than measured by our own instrumentation, so quote it as a range, not a benchmark figure.
- ⚠️ **Some titles still misrender under transcode.** This reads as a transcode / game-compatibility issue rather than a hardware one. For those games the combination of **`WRAPPER_NO_PATCH_OPCONSTCOMP` + PanVK + panDXVK** together fixed it — reported by **Lloyd**. Recorded as a stack that worked, **not** as a per-run cause.
- ⚠️ **Always check `WRAPPER_NO_PATCH_OPCONSTCOMP` before using panDXVK for gaming.** It is a **default environment variable on Winlator Mali**, set there to `1`, and the same applies to any wrapper that exposes the variable — worth checking it **first**, since it is a wrapper-side setting (**0 references** in panDXVK's own source and shipped DLLs) and panDXVK neither sees nor sets it for you. The value published for panDXVK is **`= 0`**; Bannerlator labels the same toggle "Don't patch OpConstantComposite". The short spellings `WRAPPER_NO_PATCH` and `wrapper_no_patch` are read by nothing — only the full name reaches the wrapper.

## Credits

- [doitsujin/dxvk](https://github.com/doitsujin/dxvk) — upstream DXVK
- [pythonlover02/dxvk-sarek](https://github.com/pythonlover02/dxvk-sarek) — Sarek fork
- [Us5rman](https://github.com/Us5rman) — author of PR #1, "Async and GPlasync", and the one behind the asynchronous pipeline idea itself: `dxvk_pipecompiler.*`, `dxvk_graphics_lib.cpp`, the `PANDXVK_ASYNC` / `PANDXVK_GPLASYNC` modes, the GPL device gating and `docs/async-notes.md`
- [Ph42oN/dxvk-gplasync](https://github.com/Ph42oN/dxvk-gplasync) — the gplasync implementation that work derives from
- [Wonderkast](https://github.com/wonderkast) — BC7/ASTC technical review, lead of panVK for Mali G720
- [leegao/bcn_layer](https://github.com/leegao/bcn_layer) — BCN wrapper layer reference
- [richgel999/bc7enc](https://github.com/richgel999/bc7enc) — BC7 differential oracle
- [iOrange/bcdec](https://github.com/iOrange/bcdec) — BC6H differential oracle (MIT)
- [isygold/panDXVK-logs](https://github.com/isygold/panDXVK-logs) — community log repo

### Testers

Every finding in [Testers' Findings](#testers-findings) above is theirs, not ours — they ran these builds on Mali hardware we cannot test on ourselves, and every diagnosis in the early releases traces back to their files. Where a finding is marked reported rather than independently verified, that is deliberate: it is their observation, recorded as they gave it.

- **Gustave** (Mali-G52) — the most consistent logger of the project. Nine Sols, Silksong and Sugary Spire logs, plus the wine logs that named the early crash. Delivered the 9-zip A/B matrix (3 games × {no force, force wrapper 0, force wrapper 1}); first reported the in-game texture-corruption pattern and the "hazy fog" artifact that subsequently drove a dedicated ground-truth A/B investigation; measured the RAM drop twice, both runs landing in the 5–8% band.
- **Us5rman** (Mali-G615) — AIO/FPS baselines and the FF7, MOTORSLICE and NFS reports that mapped the early territory. Author of PR #1, and the one behind the asynchronous pipeline idea — see the contributor credit above.
- **@UriYuanKerr69** (Mali-G52) — Cult of PiN on the v2 and v3 builds with screenshots, Dead Cells, Streets of Rogue and Loopler reports.
- **LORD** (Mali-G720) — Silksong and long-session NFS13 logs on new silicon.
- **Lloyd** (Mali-G615) — DMC5 and GTA V runs, crash forensics and shader dumps. Ran the stock DXVK 1.10.3 control on the same driver and same game, which isolated panDXVK itself as the variable rather than panVK or the blob driver. Established that the real variable name is `WRAPPER_NO_PATCH_OPCONSTCOMP` and not the shorter spellings, and found the `WRAPPER_NO_PATCH_OPCONSTCOMP` + PanVK + panDXVK stack that fixed the misrendering titles. Maintains the GTA V personal build, [`panDXVK-lloyd262`](https://github.com/isygold/panDXVK-lloyd262).
- **DUCKTEN** (Mali-G615) — NFS and AC3SP sessions.
- **Charan** — [`GunaCharanTeja/WinlatorMali`](https://github.com/GunaCharanTeja/WinlatorMali), branch `bionic-mali-1.0`. His wrapper BCN layer is the reported working exception to the otherwise unimproved BCN software layer, and the direct basis for the transcode/wrapper conflict warning above.

## Upstream DXVK Reference
- Upstream DXVK: [doitsujin/dxvk](https://github.com/doitsujin/dxvk)
- DXVK Sarek: [pythonlover02/dxvk-sarek](https://github.com/pythonlover02/dxvk-sarek)
- Forked from: [pythonlover02/dxvk-sarek@v1.14.1](https://github.com/pythonlover02/dxvk-sarek/tree/v1.14.1)
- Upstream latest: [v3.1](https://github.com/doitsujin/dxvk/releases/tag/v3.1)

## Disclaimer

*AI assistance tools were used only in the calculative performance metrics and debugging of
this project.*
