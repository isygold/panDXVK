#include <algorithm>

#include "dxvk_device.h"
#include "dxvk_pipemanager.h"
#include "dxvk_state_cache.h"

namespace dxvk {
  
  DxvkPipelineManager::DxvkPipelineManager(
          DxvkDevice*         device,
          DxvkRenderPassPool* passManager)
  : m_device    (device),
    m_cache     (new DxvkPipelineCache(device->vkd())) {
    const DxvkOptions& options = device->config();

    Logger::info(str::format("panDXVK: async=", options.enableAsync,
      " gplasync=", options.gplAsyncMode));

    if (options.gplAsyncMode == 2) {
      const auto& gplFeatures = device->features().extGraphicsPipelineLibrary;
      const auto& gplProps    = device->properties().extGraphicsPipelineLibrary;

      bool usable = gplFeatures.graphicsPipelineLibrary
        && gplProps.graphicsPipelineLibraryFastLinking
        && gplProps.graphicsPipelineLibraryIndependentInterpolationDecoration;

      m_gplRequested = true;
      m_gplLibraries = usable;

      Logger::info(str::format("panDXVK gplasync: mode 2 requested, library feature enabled=",
        gplFeatures.graphicsPipelineLibrary ? 1 : 0, " fast linking=",
        gplProps.graphicsPipelineLibraryFastLinking ? 1 : 0, " independent interpolation=",
        gplProps.graphicsPipelineLibraryIndependentInterpolationDecoration ? 1 : 0, " -> ",
        usable ? "graphics pipeline libraries ACTIVE"
               : "libraries unavailable on this device, running mode 1"));
    }

    if (options.enableAsync || options.enableGplAsync) {
      int32_t numThreads = options.numAsyncThreads;

      if (numThreads <= 0) {
        numThreads = int32_t(dxvk::thread::hardware_concurrency()) / 2;
        numThreads = std::clamp(numThreads, 1, 8);
      }

      m_gplAsyncCache = options.enableGplAsync;
      m_asyncLog = options.enableAsyncLog;
      m_compiler = std::make_unique<DxvkPipelineCompiler>(uint32_t(numThreads), m_asyncLog);

      if (m_asyncLog) {
        bool cacheOn = options.enableStateCache
          && env::getEnvVar("DXVK_STATE_CACHE") != "0";

        Logger::info(str::format("panDXVK async: logging on, mode=",
          options.enableGplAsync ? "gplasync" : "async",
          ", worker threads=", numThreads,
          ", state cache=", cacheOn ? "on" : "off"));
      }
    } else if (options.enableAsyncLog) {
      Logger::info("panDXVK async: PANDXVK_ASYNC_LOG is set but no async mode is enabled");
    }

    std::string useStateCache = env::getEnvVar("DXVK_STATE_CACHE");
    
    if (useStateCache != "0" && options.enableStateCache)
      m_stateCache = new DxvkStateCache(device, this, passManager);
  }
  
  
  DxvkPipelineManager::~DxvkPipelineManager() {
    if (m_compiler != nullptr)
      m_compiler->stopWorkerThreads();

    if (m_gplRequested) {
      Logger::info(str::format("panDXVK gplasync: summary, libraries created (vertex input/pre-raster/fragment shader/fragment output) ",
        m_gplStats.created[0].load(), "/", m_gplStats.created[1].load(), "/",
        m_gplStats.created[2].load(), "/", m_gplStats.created[3].load(),
        ", reused ", m_gplStats.reused[0].load(), "/", m_gplStats.reused[1].load(), "/",
        m_gplStats.reused[2].load(), "/", m_gplStats.reused[3].load(),
        ", pipelines linked ", m_gplStats.linked.load(),
        ", fallbacks to full compile ", m_gplStats.fallbacks.load(),
        ", library time ", m_gplStats.libMicros.load() / 1000, " ms",
        ", link time ", m_gplStats.linkMicros.load() / 1000, " ms",
        ", library path ", m_gplLibraries.load() ? "still on" : "off"));
    }
  }
  
  
  DxvkComputePipeline* DxvkPipelineManager::createComputePipeline(
    const DxvkComputePipelineShaders& shaders) {
    if (shaders.cs == nullptr)
      return nullptr;
    
    std::lock_guard<dxvk::mutex> lock(m_mutex);
    
    auto pair = m_computePipelines.find(shaders);
    if (pair != m_computePipelines.end())
      return &pair->second;
    
    auto iter = m_computePipelines.emplace(
      std::piecewise_construct,
      std::tuple(shaders),
      std::tuple(this, shaders));
    return &iter.first->second;
  }
  
  
  DxvkGraphicsPipeline* DxvkPipelineManager::createGraphicsPipeline(
    const DxvkGraphicsPipelineShaders& shaders) {
    if (shaders.vs == nullptr)
      return nullptr;
    
    std::lock_guard<dxvk::mutex> lock(m_mutex);
    
    auto pair = m_graphicsPipelines.find(shaders);
    if (pair != m_graphicsPipelines.end())
      return &pair->second;
    
    auto iter = m_graphicsPipelines.emplace(
      std::piecewise_construct,
      std::tuple(shaders),
      std::tuple(this, shaders));
    return &iter.first->second;
  }

  
  void DxvkPipelineManager::registerShader(
    const Rc<DxvkShader>&         shader) {
    if (m_stateCache != nullptr)
      m_stateCache->registerShader(shader);
  }


  DxvkPipelineCount DxvkPipelineManager::getPipelineCount() const {
    DxvkPipelineCount result;
    result.numComputePipelines  = m_numComputePipelines.load();
    result.numGraphicsPipelines = m_numGraphicsPipelines.load();
    return result;
  }


  bool DxvkPipelineManager::isCompilingShaders() const {
    return (m_stateCache != nullptr && m_stateCache->isCompilingShaders())
        || (m_compiler != nullptr && m_compiler->isBusy());
  }


  void DxvkPipelineManager::stopWorkerThreads() const {
    if (m_stateCache != nullptr)
      m_stateCache->stopWorkerThreads();

    if (m_compiler != nullptr)
      m_compiler->stopWorkerThreads();
  }
  
}
