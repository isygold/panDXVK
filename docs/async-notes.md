# Async pipeline compilation for panDXVK: development notes

These notes cover what was added to panDXVK for asynchronous pipeline
compilation, how it works, what was tested, and what is still unproven. They are
written for anyone forking the repository.

Target: Mali GPUs under Winlator / Gamehub, through a Vulkan wrapper.

## 1. Summary

| Item | Status |
|---|---|
| Basic async (`PANDXVK_ASYNC=1`) | Finished and tested. |
| Gplasync mode 1 (`PANDXVK_GPLASYNC=1`) | Finished and tested. Async plus writing background-compiled pipelines to the state cache. |
| Gplasync mode 2 (`PANDXVK_GPLASYNC=2`) | Works and renders correctly. Speed advantage over mode 1 haven't been fully proven in games. |
| Async logging (`PANDXVK_ASYNC_LOG=1`) | Finished. |

Async removes the freeze that happens when the game needs a pipeline that has not
been compiled yet. It does not raise the FPS of a game that is already fully
compiled.

## 2. Background

DXVK translates Direct3D to Vulkan. Every combination of shaders and render state
needs a Vulkan graphics pipeline, and the driver compiles it the first time a draw
uses it. On a phone GPU that can take tens to hundreds of milliseconds while the
render thread waits. That is shader stutter.

Async compilation moves the compile to worker threads. The draw that needed the
missing pipeline is skipped, and later draws use the finished pipeline. The cost is
that objects can be missing for a moment.

Measured compile times, taken from the `finished ... in N ms` lines of the async
log (`PANDXVK_ASYNC_LOG=1`):

| Game | GPU | Pipelines compiled | Total compile time | Median | Slowest | Draw calls skipped (total / worst single pipeline) |
|---|---|---|---|---|---|---|
| Hollow Knight Silksong | Mali-G52 MC2 | 61 | 21.8 s | 117 ms | 4.2 s | 247 / 50 |
| Minutes Till Dawn | Mali-G57 MC2 | 23 | 1.3 s | 52 ms | 121 ms | 122 / 36 |
| Hades | Mali-G57 MC2 | 4 | 0.25 s | 36 ms | 147 ms | 10 / 6 |

Without async, the same compiles run on the render thread. A separate Silksong run
on the Mali-G52 MC2 with debug logging and no async measured 67 pipelines taking
8.3 s in total, with a median of 99 ms, the slowest tenth at 191 ms or more, and a
worst case of 695 ms. Each of those is a freeze of that length.

The numbers differ a lot between games and devices. The Silksong async run used
only 2 worker threads on a busy CPU, which is why its slowest compiles took seconds.
The compile count is also small for games that load most of their pipelines from the
state cache at startup. The skipped-draw column is the visible cost of async: in
every game above it stayed in the tens or low hundreds of draws over the whole
session.

## 3. How it works

### 3.1 Basic async

- `DxvkGraphicsPipeline::getPipelineHandle()` looks for a compiled pipeline for the
  current state. If none exists and async is on, it validates the state, hands the
  job to the compiler and returns a null handle.
- A null handle makes the context skip the draw. In
  `DxvkContext::updateGraphicsPipelineState()` the null check returns before the
  `GpDirtyPipelineState` flag is cleared, so the next draw looks up the pipeline
  again. No change to `dxvk_context.cpp` was needed.
- `DxvkPipelineCompiler` owns the worker threads and queue. A request that is
  already queued or compiling is not queued again.
- Workers release the queue lock before compiling, so the render thread never
  waits on a compile.
- Thread count defaults to half the CPU cores, between 1 and 8
  (`dxvk.numAsyncThreads`).

### 3.2 Gplasync mode 1

Same as basic async, and every background-compiled pipeline is also written to the
state cache. On the next launch the state cache precompiles those pipelines at
startup, so fewer draws are skipped. A pipeline the driver failed to build is not
written.

### 3.3 Gplasync mode 2 (graphics pipeline libraries)

This mode uses `VK_EXT_graphics_pipeline_library`. A pipeline is split into four
libraries, compiled separately and linked:

| Library | Contains | Cached by |
|---|---|---|
| Vertex input | input layout, topology | layout and topology |
| Pre-raster | vertex, tessellation and geometry shaders, rasterizer state | rasterizer state, binding mask, spec constants, provided vertex inputs |
| Fragment shader | fragment shader, depth-stencil and multisample state | depth-stencil state, binding mask, spec constants, colour swizzle, dual-source flag |
| Fragment output | blend, depth-stencil and multisample state | blend and colour state |

- Mode 2 is used only if the device exposes the extension, reports
  `graphicsPipelineLibrary`, fast linking and independent interpolation decoration,
  and `VK_KHR_pipeline_library` is available. Otherwise it runs as mode 1. The
  decision is by capability, not by vendor.
- If creating a library or linking fails, that pipeline is compiled normally. After
  8 failures the library path turns itself off for the session.
- By default the libraries keep their optimization data and are linked with
  link-time optimization. `PANDXVK_GPL_FASTLINK=1` selects a plain fast link
  instead (see section 7).
- Libraries cannot be shared between different shader pairs in this code base,
  because the binding numbers in a shader's SPIR-V are rewritten per pipeline.
  A library is reused only when the same shader pair appears in a different state,
  which is uncommon.

### 3.4 Device setup and reporting

- The extension and `VK_KHR_pipeline_library` are enabled on any device that
  supports them with fast linking.
- `dxgi.log` gets one line when the adapter is created, saying whether the device
  can use libraries:
  `panDXVK: graphics pipeline library: feature=1, fast linking=1, ... -> usable by PANDXVK_GPLASYNC=2`

## 4. Variables and configuration

| Variable | `dxvk.conf` key | Meaning |
|---|---|---|
| `PANDXVK_ASYNC=1` | `dxvk.enableAsync` | Basic async. |
| `PANDXVK_GPLASYNC=1` | `dxvk.enableGplAsync` | Mode 1. |
| `PANDXVK_GPLASYNC=2` | `dxvk.gplAsyncMode = 2` | Mode 2. Runs as mode 1 if the device cannot use libraries. |
| `PANDXVK_ASYNC_LOG=1` | `dxvk.asyncLog` | Log async activity. |
| `PANDXVK_GPL_FASTLINK=1` | none | Plain fast link in mode 2. |
| none | `dxvk.numAsyncThreads` | Async worker threads, 0 = automatic. |

`0` turns a variable off. When a variable and a config key are both set, the
variable wins. `dxvk.numCompilerThreads` is a separate standard DXVK option for the
state cache precompile threads.

## 5. Files

- `src/dxvk/dxvk_pipecompiler.h` / `.cpp`: new. Worker threads, queue, logging.
- `src/dxvk/dxvk_graphics_lib.cpp`: new. Library creation, caching and linking.
- `src/dxvk/dxvk_graphics.h` / `.cpp`: skipping the draw, queueing, library path in
  `createInstance`, cache writes.
- `src/dxvk/dxvk_pipemanager.h` / `.cpp`: owns the compiler, decides the mode,
  prints the summary.
- `src/dxvk/dxvk_options.h` / `.cpp`: options and environment variables.
- `src/dxvk/dxvk_adapter.cpp`, `dxvk_device_info.h`, `dxvk_extensions.h`: extension
  and feature setup, device report.
- `src/dxvk/meson.build`: the two new source files.
- `dxvk.conf`: the new options, documented.

## 6. Log reference

With `PANDXVK_ASYNC_LOG=1`:

```
panDXVK async: logging on, mode=gplasync, worker threads=4, state cache=on
panDXVK async: queued vs=<hash> fs=<hash> (pending: 1)
panDXVK async: worker 2 compiling vs=<hash> fs=<hash>
panDXVK async: finished vs=<hash> fs=<hash> in 38.2 ms, 14 draw calls skipped (pending: 0)
```

Mode 2 adds:

```
panDXVK gplasync: mode 2 requested, ... -> graphics pipeline libraries ACTIVE
panDXVK gplasync: created pre-raster library in 2.3 ms (vs=<hash>)
panDXVK gplasync: linked vs=<hash> in 2.0 ms, new libraries: vertex input=1 ...
panDXVK gplasync: first pipeline linked from libraries in ... ms, link mode optimized
panDXVK gplasync: summary, libraries created ... reused ... linked ...
```

What to look for:

- `FAILED`: PanVK returned no pipeline. Draws using it stay skipped.
- `draw dropped, invalid pipeline state`: DXVK rejected the state, with or without
  async.
- `state cache=off` with gplasync on: gplasync is doing plain async only.
- `library path failed` or `disabled`: mode 2 fell back to a normal compile.
- The summary and `stopped` lines print only on a clean exit. If the app is killed,
  the per-pipeline lines are still there.

The library creation times in mode 2 (for example 0.012 ms) are very small and are
probably the driver answering from its own pipeline cache. The figure that matters
is `finished ... in N ms`.

## 7. Findings

### Race review

I read `dxvk_context.cpp` for races between the async workers and the context and
found none. The workers touch only a pipeline's own instance list (a mutex plus the
same `sync::List` the state cache workers use) and the compiler's queue. They never
touch command lists, descriptors, semaphores or the submission queue, and they are
joined before pipelines are destroyed. Not reviewed: the dxgi presenter and the
locking inside `DxvkStateCache::addGraphicsPipeline`.

### Fast link black screens when using PanVK currently 

With `PANDXVK_GPLASYNC=2` with AIO test it showed dxvk's hud but none of the
scene, and the draw count stayed normal. No library or link failed.

Ruled out: missing depth-stencil state in the fragment output library, the dynamic
state list missing at link time, and shader modules being destroyed after library
creation.

What decided it: libraries created with retained optimization data and linked with
link-time optimization render correctly. The same libraries linked with a plain fast
link render nothing. Tested on a Mali-G615 MC6, PanVK 26.2.99, through a Vulkan
wrapper. The driver is a beta, so this may be a driver issue, but I could not confirm
that. The optimized link is the default, and `PANDXVK_GPL_FASTLINK=1` selects the
fast link for drivers where it works.

## 8. Test results

Devices seen: Mali-G52 MC2, Mali-G615 MC6 through a Vulkan wrapper, and Mali-G615
MC6 on PanVK (driver 26.2.99).

**Silksong (Mali-G52 MC2), async log on, 2 workers.** 61 pipelines compiled in the
background and none failed. Total compile time 21.8 s, median 117 ms, slowest tenth
811 ms or more, worst 4.2 s. In total 247 draw calls were skipped, and the worst
single pipeline had 50 skipped. At most 9 pipelines were ever pending. The slow
compiles come from 2 workers sharing a busy CPU.

**Mode 2 in four games** (Hades, Keep Driving, Minutes Till Dawn, This War of Mine
on D3D9): 174 pipelines linked with no library or link failure. The async queue
stayed small (2, 2, 11 and 0 compiles). Library reuse was low, because most links
were a shader pair seen for the first time.

**Fate EXTELLA LINK on PanVK, mode 2.** Libraries active, no failures. Finished
compiles took 7 to 18 ms with 1 to 28 draws skipped each. Eight of nine links
created all four libraries new.

## 9. Limitations

- No real-game comparison exists yet. "Async reduces shader stutter" is from the
  compile times and skipped-draw counts above, not on a measured frametime graph.
- Libraries stay in driver memory for reuse. The memory cost is unmeasured.
- The first draw of a new pipeline is still skipped in every mode.
- Cache keys include the whole binding mask, so a change in any unbound slot
  recreates the pre-raster and fragment libraries. This is safe and lowers reuse.

## 10. How to test

1. Set the container environment variables, for example `PANDXVK_GPLASYNC=1` and
   `PANDXVK_ASYNC_LOG=1`.
2. In `dxgi.log`, search `pipeline library` to see whether the device can use mode 2.
3. In `d3d11.log`, search `async` or `gplasync`.
4. For a comparison, run the same scene with no variables, mode 1 and mode 2, and
   keep the logs and a screenshot for each.

## 11. Credits

The idea of skipping a draw and compiling on worker threads is the same one used by
the public dxvk-async and gplasync patches. This implementation was written for this
code base and does not copy their code. 
