# panDXVK

panDXVK is a specialized DXVK translation layer built exclusively for the panVK (Panfrost / ARM Mali Vulkan) driver.

Based on [DXVK v1.10.3](https://github.com/doitsujin/dxvk/tree/v1.10.3). Adds a BC texture → ASTC 4x4 transcode pipeline so that PC games using BC1–BC7 compressed textures can render correctly on Mali GPUs via PanVK, which does not report `textureCompressionBC`.

## Key Features
- **BC→ASTC Transcode**: Software decode of BC1–BC7 to RGBA8, then encode to ASTC 4x4. Mali hardware decodes ASTC natively.
- **Automatic Detection**: Transcode activates only when `DxvkAdapter::isPanVk()` (vendor ID `0x13B5`) AND `textureCompressionBC = false`. Devices with blob driver BC support are left alone.
- **Format Remap**: VkImage, SRV, and RTV formats are remapped from BC to ASTC transparently. No game-side changes required.
- **Heap-Allocated Buffers**: RGBA8 intermediate and ASTC output are heap-allocated to avoid stack overflow on DXVK's small thread stacks.
- **Transcode Timing**: INFO-level logging reports per-subresource transcode time for profiling.
- **x64 + x32**: Both 64-bit and 32-bit DLLs built and verified.

## Installation
1. Download the latest release artifact from [Releases](https://github.com/isygold/panDXVK/releases).
2. Extract the archive.
3. Set your Wine prefix and install:
   ```
   export WINEPREFIX=/path/to/.wine-prefix
   ./setup_dxvk.sh install
   ```
4. To install with D3D10 helper libraries, add `--with-d3d10`.
5. To use symbolic links instead of copies (useful for development), add `--symlink`.
6. Verify DXVK is active by checking for `d3d11.log` in the application directory.

To uninstall:
```
export WINEPREFIX=/path/to/.wine-prefix
./setup_dxvk.sh uninstall
```

## Configuration
panDXVK uses the same configuration mechanism as upstream DXVK. Set `DXVK_CONFIG_FILE` to point to a `dxvk.conf` file, or use environment variables:

| Variable | Values | Description |
|----------|--------|-------------|
| `DXVK_HUD` | `devinfo`, `fps`, `frametimes`, `full`, etc. | HUD overlay. See upstream docs. |
| `DXVK_LOG_LEVEL` | `none`, `error`, `warn`, `info`, `debug` | Logging verbosity. `info` shows transcode timing. |
| `DXVK_LOG_PATH` | path | Directory for log files. |
| `DXVK_FRAME_RATE` | `0` (uncap), or FPS limit | Frame rate cap. |

## Notes
- **I need your logs.** If you hit a crash, rendering glitch, or anything weird, grab the log file from your Wine prefix's drive_c (usually `wine_debug.log` or `d3d11.log` in the app directory) and attach it to your issue. Alternatively, set `DXVK_LOG_LEVEL=info` before launching the game so the log captures BC→ASTC transcode activity and other useful state. Without logs, I cannot help you.
- **ASTC 4x4 is lossy.** BC1–BC7 textures are decoded to RGBA8 and re-encoded to ASTC 4x4. This introduces compression artifacts not present in the original. For most games the visual difference is minimal, but texture-heavy UIs or screenshots may show subtle banding.
- **BC6H maps to ASTC 6x6 LDR.** BC6H (HDR float RGB) is approximated as ASTC 6x6 UNORM. Full HDR fidelity is not preserved.
- **PanVK must be the active Vulkan driver.** panDXVK detects Mali via vendor ID `0x13B5`. If you are running a blob driver that already reports `textureCompressionBC = true`, the transcode is skipped entirely — the game's BC textures are uploaded as-is.
- **Transcode happens at CPU time.** Each `UpdateTexture` call triggers a full BC decode + ASTC encode on the CPU. Large textures (4K+) may take 10–30ms per subresource on mobile CPUs. This is a one-time cost per texture load, not per frame.
- **TBDR architecture.** Mali is a tile-based deferred renderer. ASTC textures are natively supported by the tile buffer. No special TBDR handling is needed for the transcode path — the ASTC data is uploaded via standard `vkCmdCopyBufferToImage` and decoded by the texture unit before fragment processing.
- **AppendSlice path not yet patched.** The `AppendSlice` D3D11 path may also encounter BC textures. This is a known gap. If you see BC format errors in `AppendSlice`, file an issue.
- **CI builds are automated.** The GitHub Actions workflow builds both x64 and x32 on Fedora 44 with MinGW-w64. If the Fedora mirror is temporarily unreachable, the workflow retries automatically.

## Upstream DXVK Reference
- Upstream: [doitsujin/dxvk](https://github.com/doitsujin/dxvk)
- Forked from: [v1.10.3](https://github.com/doitsujin/dxvk/tree/v1.10.3)
- Upstream latest: [v3.1](https://github.com/doitsujin/dxvk/releases/tag/v3.1)
