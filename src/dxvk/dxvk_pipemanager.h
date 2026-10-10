#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "dxvk_compute.h"
#include "dxvk_graphics.h"
#include "dxvk_pipecompiler.h"

namespace dxvk {

  class DxvkStateCache;

  /**
   * \brief Pipeline count
   * 
   * Stores number of graphics and
   * compute pipelines, individually.
   */
  struct DxvkPipelineCount {
    uint32_t numGraphicsPipelines;
    uint32_t numComputePipelines;
  };


  /**
   * \brief Graphics pipeline library statistics
   *
   * Counters are indexed by library type: vertex input,
   * pre-rasterization, fragment shader, fragment output.
   */
  struct DxvkGplStats {
    DxvkGplStats() {
      for (uint32_t i = 0; i < 4; i++) {
        created[i] = 0;
        reused[i]  = 0;
      }
    }

    std::atomic<uint32_t> created[4];
    std::atomic<uint32_t> reused[4];
    std::atomic<uint32_t> linked     = { 0 };
    std::atomic<uint32_t> fallbacks  = { 0 };
    std::atomic<uint64_t> libMicros  = { 0 };
    std::atomic<uint64_t> linkMicros = { 0 };
  };
  
  
  /**
   * \brief Pipeline manager
   * 
   * Creates and stores graphics pipelines and compute
   * pipelines for each combination of shaders that is
   * used within the application. This is necessary
   * because DXVK does not expose the concept of shader
   * pipeline objects to the client API.
   */
  class DxvkPipelineManager {
    friend class DxvkComputePipeline;
    friend class DxvkGraphicsPipeline;
  public:
    
    DxvkPipelineManager(
            DxvkDevice*         device,
            DxvkRenderPassPool* passManager);
    
    ~DxvkPipelineManager();
    
    /**
     * \brief Retrieves a compute pipeline object
     * 
     * If a pipeline for the given shader stage object
     * already exists, it will be returned. Otherwise,
     * a new pipeline will be created.
     * \param [in] shaders Shaders for the pipeline
     * \returns Compute pipeline object
     */
    DxvkComputePipeline* createComputePipeline(
      const DxvkComputePipelineShaders& shaders);
    
    /**
     * \brief Retrieves a graphics pipeline object
     * 
     * If a pipeline for the given shader stage objects
     * already exists, it will be returned. Otherwise,
     * a new pipeline will be created.
     * \param [in] shaders Shaders for the pipeline
     * \returns Graphics pipeline object
     */
    DxvkGraphicsPipeline* createGraphicsPipeline(
      const DxvkGraphicsPipelineShaders& shaders);
    
    /*
     * \brief Registers a shader
     * 
     * Starts compiling pipelines asynchronously
     * in case the state cache contains state
     * vectors for this shader.
     * \param [in] shader Newly compiled shader
     */
    void registerShader(
      const Rc<DxvkShader>&         shader);
    
    /**
     * \brief Retrieves total pipeline count
     * \returns Number of compute/graphics pipelines
     */
    DxvkPipelineCount getPipelineCount() const;

    /**
     * \brief Checks whether async compiler is busy
     * \returns \c true if shaders are being compiled
     */
    bool isCompilingShaders() const;

    /**
     * \brief Stops async compiler threads
     */
    void stopWorkerThreads() const;
    
  private:
    
    DxvkDevice*               m_device;
    Rc<DxvkPipelineCache>     m_cache;
    Rc<DxvkStateCache>        m_stateCache;

    std::atomic<uint32_t>     m_numComputePipelines  = { 0 };
    std::atomic<uint32_t>     m_numGraphicsPipelines = { 0 };

    bool                      m_gplAsyncCache = false;
    bool                      m_asyncLog = false;
    bool                      m_gplRequested = false;

    std::atomic<bool>         m_gplLibraries = { false };
    DxvkGplStats              m_gplStats;
    
    dxvk::mutex m_mutex;
    
    std::unordered_map<
      DxvkComputePipelineShaders,
      DxvkComputePipeline,
      DxvkHash, DxvkEq> m_computePipelines;
    
    std::unordered_map<
      DxvkGraphicsPipelineShaders,
      DxvkGraphicsPipeline,
      DxvkHash, DxvkEq> m_graphicsPipelines;

    std::unique_ptr<DxvkPipelineCompiler> m_compiler;
    
  };
  
}
