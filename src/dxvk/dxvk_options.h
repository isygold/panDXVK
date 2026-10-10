#pragma once

#include "../util/config/config.h"

namespace dxvk {

  struct DxvkOptions {
    DxvkOptions() { }
    DxvkOptions(const Config& config);

    /// Enable debug utils (alternative to DXVK_PERF_EVENTS=1)
    bool enableDebugUtils;

    /// Enable state cache
    bool enableStateCache;

    /// Number of compiler threads
    /// when using the state cache
    int32_t numCompilerThreads;

    /// Compile graphics pipelines asynchronously
    bool enableAsync;

    /// Gplasync mode: 0 off, 1 async with state cache
    /// writes, 2 async using graphics pipeline libraries
    int32_t gplAsyncMode;

    /// Compile graphics pipelines asynchronously
    /// and persist them to the state cache
    bool enableGplAsync;

    /// Compile graphics pipelines asynchronously
    /// with dynamic state
    bool enableDyAsync;

    /// Number of async compiler threads
    int32_t numAsyncThreads;

    /// Log async compilation activity
    bool enableAsyncLog;

    /// Shader-related options
    Tristate useRawSsbo;

    /// Workaround for NVIDIA driver bug 3114283
    Tristate shrinkNvidiaHvvHeap;

    /// HUD elements
    std::string hud;
  };

}
