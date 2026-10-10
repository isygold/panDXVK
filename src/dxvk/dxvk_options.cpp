#include "../util/util_env.h"

#include "dxvk_options.h"

namespace dxvk {

  static bool getFlag(const char* env, bool value) {
    std::string str = env::getEnvVar(env);
    return str.empty() ? value : str != "0";
  }


  static int32_t getMode(const char* env, int32_t value) {
    std::string str = env::getEnvVar(env);

    if (str.empty())
      return value;

    if (str == "0")
      return 0;

    return str == "2" ? 2 : 1;
  }


  DxvkOptions::DxvkOptions(const Config& config) {
    enableDebugUtils      = config.getOption<bool>    ("dxvk.enableDebugUtils",       false);
    enableStateCache      = config.getOption<bool>    ("dxvk.enableStateCache",       true);
    numCompilerThreads    = config.getOption<int32_t> ("dxvk.numCompilerThreads",     0);
    numAsyncThreads       = config.getOption<int32_t> ("dxvk.numAsyncThreads",        0);
    useRawSsbo            = config.getOption<Tristate>("dxvk.useRawSsbo",             Tristate::Auto);
    shrinkNvidiaHvvHeap   = config.getOption<Tristate>("dxvk.shrinkNvidiaHvvHeap",    Tristate::Auto);
    hud                   = config.getOption<std::string>("dxvk.hud", "");

    int32_t confMode = config.getOption<int32_t>("dxvk.gplAsyncMode", 0);

    if (confMode == 0 && config.getOption<bool>("dxvk.enableGplAsync", false))
      confMode = 1;

    gplAsyncMode   = getMode("PANDXVK_GPLASYNC", confMode);
    enableGplAsync = gplAsyncMode >= 1;

    enableAsync    = getFlag("PANDXVK_ASYNC",     config.getOption<bool>("dxvk.enableAsync",    false));
    enableDyAsync  = getFlag("PANDXVK_DYASYNC",   config.getOption<bool>("dxvk.enableDyAsync",  false));
    enableAsyncLog = getFlag("PANDXVK_ASYNC_LOG", config.getOption<bool>("dxvk.asyncLog",       false));
  }

}
