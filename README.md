# panDXVK

panDXVK is a specialized DXVK translation layer built exclusively for the panVK (Panfrost / ARM Mali Vulkan) driver.

Based on [DXVK v1.10.3](https://github.com/doitsujin/dxvk/tree/v1.10.3), forked from [pythonlover02/dxvk-sarek](https://github.com/pythonlover02/dxvk-sarek). Adds a BC texture → ASTC 4x4 transcode pipeline so that PC games using BC1–BC7 compressed textures can render correctly on Mali GPUs via PanVK, which does not report `textureCompressionBC`.

## Key Features
- **BC→ASTC Transcode**: Software decode of BC1–BC7 to RGBA8, then encode to ASTC 4x4. Mali hardware decodes ASTC natively.
- **Automatic Detection**: Transcode activates only when `DxvkAdapter::isPanVk()` (vendor ID `0x13B5`) AND `textureCompressionBC = false`. Devices with blob driver BC support are left alone.
- **Format Remap**: VkImage, SRV, and RTV formats are remapped from BC to ASTC transparently. No game-side changes required.
- **Heap-Allocated Buffers**: RGBA8 intermediate and ASTC output are heap-allocated to avoid stack overflow on DXVK's small thread stacks.
- **Transcode Timing**: Debug-level logging reports per-subresource transcode time for profiling (visible with `DXVK_LOG_LEVEL=debug`; stays on in current builds — see note below).
- **x64 + x32**: Both 64-bit and 32-bit DLLs built and verified.

## Installation (Winlator / Bannerlator)

**Option A — WCP (recommended):**
1. Download `wcp.zip` from the [Releases](https://github.com/isygold/panDXVK/releases) page.
2. In Winlator/Bannerlator, go to **Container Settings → Content**.
3. Tap **Install** and select the downloaded `wcp.zip`.
4. Launch your game. panDXVK will automatically detect Mali and transcode BC textures to ASTC.

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
| `DXVK_LOG_LEVEL` | `none`, `error`, `warn`, `info`, `debug` | Logging verbosity. Transcode diagnostics need a debug build + `debug` level. |
| `DXVK_LOG_PATH` | path | Directory for log files. |
| `DXVK_FRAME_RATE` | `0` (uncap), or FPS limit | Frame rate cap. |
| `PANDXVK_FORCE_TRANSCODE` | `0`, `1` | Test knob: force BC→ASTC on Mali even when the driver claims BC (wrapper setups). Explicit CPU overhead. Needs game restart. |

## Notes
- **I need your logs.** If you hit a crash, rendering glitch, or anything weird, grab the log file from your Wine prefix's drive_c (usually `wine_debug.log` or `d3d11.log` in the app directory) and paste it to [panDXVK Logs](https://github.com/isygold/panDXVK-logs/issues). For BC→ASTC transcode diagnostics you need a debug build with `DXVK_LOG_LEVEL=debug`. Without logs, I cannot help you.
- **ASTC 4x4 is lossy.** BC1–BC7 textures are decoded to RGBA8 and re-encoded to ASTC 4x4. This introduces compression artifacts not present in the original. For most games the visual difference is minimal, but texture-heavy UIs or screenshots may show subtle banding.
- **BC6H maps to ASTC 4x4 LDR.** BC6H (HDR float RGB) is clamped (negatives/NaN to 0, highlights saturate) and approximated as ASTC 4x4 UNORM. Full HDR fidelity is not preserved.
- **PanVK must be the active Vulkan driver.** panDXVK detects Mali via vendor ID `0x13B5`. If you are running a blob driver that already reports `textureCompressionBC = true`, the transcode is skipped entirely — the game's BC textures are uploaded as-is. Set `PANDXVK_FORCE_TRANSCODE=1` to force the transcode anyway (test mode, expect CPU overhead).
- **Transcode happens at CPU time.** Each `UpdateTexture` call triggers a full BC decode + ASTC encode on the CPU. Large textures (4K+) may take 10–30ms per subresource on mobile CPUs. This is a one-time cost per texture load, not per frame.
- **TBDR architecture.** Mali is a tile-based deferred renderer. ASTC textures are natively supported by the tile buffer. No special TBDR handling is needed for the transcode path — the ASTC data is uploaded via standard `vkCmdCopyBufferToImage` and decoded by the texture unit before fragment processing.
- **AppendSlice path not yet patched.** The `AppendSlice` D3D11 path may also encounter BC textures. This is a known gap. If you see BC format errors in `AppendSlice`, file an issue.
- **CI builds are automated.** The GitHub Actions workflow builds both x64 and x32 on Fedora 44 with MinGW-w64. If the Fedora mirror is temporarily unreachable, the workflow retries automatically.

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
The transcode gate is kept intentionally — modern wrappers carry the BCN layer, so the pipeline stays dormant behind `!textureCompressionBC` until a wrapper-free device appears. All transcode logging is debug-level; note our CI does not define `NDEBUG` (Meson `b_ndebug` defaults off and is never set), so this logging ships live in current builds — useful while testing, to be compiled out (`-Db_ndebug=true`) when stabilizing.

## Validation Status

Tested so far — all with wrapper active (`textureCompressionBC = 1`, transcode skipped):
- AIO Graphics Test on Mali-G615 (PanVK 26.2.99 and blob 44.1.0) and Mali-G720 — init parity only.
- 70 FPS panDXVK vs 51 FPS stock DXVK on AIO spin-cube (non-BC test, Us5rman).
- Mali-G99 MC3: 700+ FPS AIO with P11, 900+ FPS with P9 (Proton 9 ARM64EC) — suggests strong PanVK + panDXVK throughput headroom.

Current state:
- Real Mali hardware has no BC support (verified: gpuinfo + leegao unsupported-device list) — the `= 1` in tester logs comes from the wrapper layer.
- Transcode hot spots optimized: word-level bit extract/insert, verified bit-identical over 12,298 on-device checks.

Still missing — the one test that proves the transcode path (two options):
- Wrapper-free: select the raw PanVK driver entry (not Wrapper/Apex), run a BC-heavy game (GTA V, Skyrim SE, Dark Souls 3), confirm `textureCompressionBC = 0` in `d3d11.log`.
- Forced (release build OK): set `PANDXVK_FORCE_TRANSCODE=1`, run any BC-heavy game on any Mali setup, confirm `textureCompressionBC = 1` (wrapper active) plus the one-time `PANDXVK_FORCE_TRANSCODE` notice — proving the forced path fired where it previously skipped.

Without one of these, no log currently proves BC→ASTC works on real hardware.

## Upstream Reference
- Upstream DXVK: [doitsujin/dxvk](https://github.com/doitsujin/dxvk)
- DXVK Sarek: [pythonlover02/dxvk-sarek](https://github.com/pythonlover02/dxvk-sarek)
- Forked from: [pythonlover02/dxvk-sarek@v1.10.3](https://github.com/pythonlover02/dxvk-sarek/tree/v1.10.3)
- Upstream latest: [v3.1](https://github.com/doitsujin/dxvk/releases/tag/v3.1)
