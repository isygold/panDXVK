# FSR 1 sharpening and upscaling for panDXVK: development notes

These notes explain what was added to panDXVK for an optional picture-sharpening
feature, how to use it, how it works, what was tested, and what is still unproven.
They are written for anyone forking the repository, including people who do not
work on code every day.

Target: Mali GPUs under Winlator / Bannerlator, through the Vulkan driver or a wrapper.

## 1. How to use

1. **Turn it on.** In your container's environment variables, add
   `PANDXVK_FSR=1`.
2. **Optional: choose how strong it is.** Add `PANDXVK_FSR_SHARPNESS` with a number
   from `0.0` to `1.0`. Higher is sharper. The default is `0.8`. If you see bright
   outlines around objects, lower it, for example to `0.6`.
3. **Start your game.**
4. **Check that it is working.** Open `d3d11.log` and search for `fsr:`. You should
   see a line like this:

   ```
   panDXVK: fsr: enabled, sharpness=0.8
   panDXVK: fsr: sharpening only at 1280x720, sharpness=0.8
   ```

5. **To turn it off,** remove the variable or set it to `0`.

What to expect:

- The effect is gentle. Look at small text and thin lines to see it.
- It does not make the game run faster by itself.
- It is sharpening, not anti-aliasing. It makes edges and textures crisper, but it
  does not smooth out jagged edges.

## 2. Summary

This adds an optional sharpening feature to panDXVK, based on AMD's FSR 1. It is off
unless you turn it on, so nothing changes for anyone who does not use it.

| Item | Status |
|---|---|
| Sharpening when the game and the window are the same size (`PANDXVK_FSR=1`) | Finished. It runs and its result reaches the screen (red-tint test). |
| Enlarge and sharpen, when the game draws smaller than the window | Written and compiles. Not yet tested in a real run. |
| Strength setting (`PANDXVK_FSR_SHARPNESS`) | Finished. |

## 3. Background

[AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) is a free
tool that improves a finished picture. It only needs the final image, not extra
information from the game, which is why it fits inside a layer that translates
Direct3D to Vulkan. It has two steps:

| Step | Name | What it does |
|---|---|---|
| 1 | EASU | Enlarges the picture while keeping edges clean |
| 2 | RCAS | Sharpens the picture without creating halos around edges |

DXVK cannot make a picture bigger than what the game draws. Wine makes the window
the same size as the game's resolution, so on Winlator the two are usually equal.
That is why there is no resolution scale setting, and why the matching-size case only
sharpens.

## 4. How it works

### 4.1 Where it hooks in

- The code that copies the game's picture into the window
  (`DxvkSwapchainBlitter::presentImage`) first checks whether FSR can be used.
  If it can, the FSR steps run. If not, the normal copy runs as before.
- The settings are read once, when the program starts.
- One log line is written whenever the sizes or the on/off state change.

### 4.2 Same size

When the game's picture and the window are the same size, only the sharpening step
runs. It reads the game's picture and writes it to the window directly. No extra
image is created.

### 4.3 Enlarge and sharpen

When the game's picture is smaller than the window:

1. The first step enlarges it into a temporary image the size of the window.
2. The second step sharpens that image and writes it to the window.

The temporary image is only created when this case first happens, and it is
re-created if the window size changes.

### 4.4 When FSR is skipped

| Situation | Message in the log |
|---|---|
| The game uses multisampling (a type of anti-aliasing) | `multisampled back buffer` |
| The game set a gamma ramp (custom brightness curve) | `gamma ramp active` |
| Only part of the window is being updated | `partial present rectangle` |
| The window is smaller than the game's picture | `output is smaller than the back buffer` |

In all of these cases the normal copy runs, so nothing changes for the game.

### 4.5 Strength

`PANDXVK_FSR_SHARPNESS` is a number from 0.0 to 1.0, where higher is sharper. Behind
the scenes it is converted to FSR's own scale as `2 x (1 - strength)`.

| Strength | FSR value | Meaning |
|---|---|---|
| 1.0 | 0.0 | Strongest sharpening |
| 0.8 (default) | 0.4 | Mild |
| 0.0 | 2.0 | Effectively off |

If the value is not a valid number, a warning is logged and the default is used.
Numbers outside 0.0 to 1.0 are brought back into that range.

## 5. Variables and configuration

| Variable | Values | Default | Meaning |
|---|---|---|---|
| `PANDXVK_FSR=1` | `1` on | off | Turn the FSR feature on. |
| `PANDXVK_FSR_SHARPNESS` | 0.0 to 1.0 | 0.8 | How strong the sharpening is. |

Any other value of `PANDXVK_FSR` leaves FSR off.

## 6. Files

- `src/dxvk/dxvk_swapchain_blitter.h` / `.cpp`: reads the settings, decides when to
  skip, runs the two steps, creates the temporary image and writes the log lines.
- `src/dxvk/shaders/dxvk_fsr_easu_frag.frag`: the enlarging step.
- `src/dxvk/shaders/dxvk_fsr_rcas_frag.frag`: the sharpening step.
- `src/dxvk/shaders/ffx_a.h` and `ffx_fsr1.h`: AMD's FSR 1 files, used by the two
  steps above.
- `src/dxvk/meson.build`: the two new shader files added to the build list.

## 7. Log reference

With `PANDXVK_FSR=1`:

```
panDXVK: fsr: enabled, sharpness=0.8
panDXVK: fsr: sharpening only at 1280x720, sharpness=0.8
panDXVK: fsr: upscaling 1280x720 -> 1920x1080, sharpness=0.8
panDXVK: fsr: not applied (<reason>), back buffer 1280x720, output 1920x1080
```

What to look for:

- `not applied (...)`: the reason is one of the cases in section 4.4.
- `sharpening only`: the sizes matched, which is the usual case on Winlator.

## 8. Test results

**AIO Graphics Test, textured cube, red-tint test.** The sharpening step's output
was temporarily tinted red. The whole game window turned red, which confirms the
step runs and that its result is what you see on screen.

## 9. Limitations

- The Direct3D 9 path probably shares this code, but it has not been tested.
- The skip cases for multisampling, gamma ramps and partial updates have not been
  tested.
- The sharpening steps set the output's transparency (alpha) to fully opaque.
