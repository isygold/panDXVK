#include <array>
#include <chrono>
#include <utility>
#include <vector>

#include "../util/util_env.h"
#include "../util/util_time.h"

#include "dxvk_device.h"
#include "dxvk_graphics.h"
#include "dxvk_pipemanager.h"
#include "dxvk_spec_const.h"

namespace dxvk {

  namespace {

    constexpr uint32_t LibVertexInput    = 0;
    constexpr uint32_t LibPreRaster      = 1;
    constexpr uint32_t LibFragmentShader = 2;
    constexpr uint32_t LibFragmentOutput = 3;

    const char* const LibraryNames[4] = {
      "vertex input", "pre-raster", "fragment shader", "fragment output",
    };


    // Libraries are linked with optimization by default. Some drivers
    // produce pipelines that draw nothing when libraries are linked
    // without it, so fast linking has to be requested explicitly.
    bool useFastLink() {
      static const bool value = env::getEnvVar("PANDXVK_GPL_FASTLINK") == "1";
      return value;
    }


    VkSampleCountFlagBits getSampleCount(
      const DxvkGraphicsPipelineStateInfo& state) {
      VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

      if (state.ms.sampleCount())
        sampleCount = VkSampleCountFlagBits(state.ms.sampleCount());
      else if (state.rs.sampleCount())
        sampleCount = VkSampleCountFlagBits(state.rs.sampleCount());

      return sampleCount;
    }


    uint32_t getProvidedVertexInputs(
      const DxvkGraphicsPipelineStateInfo& state) {
      uint32_t mask = 0;

      for (uint32_t i = 0; i < state.il.attributeCount(); i++)
        mask |= 1u << state.ilAttributes[i].location();

      return mask;
    }


    bool usesDualSourceBlend(
      const DxvkGraphicsPipelineStateInfo& state) {
      return state.omBlend[0].blendEnable() && (
        util::isDualSourceBlendFactor(state.omBlend[0].srcColorBlendFactor()) ||
        util::isDualSourceBlendFactor(state.omBlend[0].dstColorBlendFactor()) ||
        util::isDualSourceBlendFactor(state.omBlend[0].srcAlphaBlendFactor()) ||
        util::isDualSourceBlendFactor(state.omBlend[0].dstAlphaBlendFactor()));
    }


    DxvkSpecConstants buildSpecConstants(
      const DxvkGraphicsPipelineStateInfo& state,
            VkSampleCountFlagBits          sampleCount,
            uint32_t                       bindingCount,
            uint32_t                       fsOutputs,
            bool                           colorMappings) {
      DxvkSpecConstants specData;
      specData.set(uint32_t(DxvkSpecConstantId::RasterizerSampleCount), sampleCount, VK_SAMPLE_COUNT_1_BIT);

      for (uint32_t i = 0; i < bindingCount; i++)
        specData.set(i, state.bsBindingMask.test(i), true);

      if (colorMappings) {
        for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
          if ((fsOutputs & (1 << i)) != 0) {
            specData.set(uint32_t(DxvkSpecConstantId::ColorComponentMappings) + i,
              state.omSwizzle[i].rIndex() << 0 | state.omSwizzle[i].gIndex() << 4 |
              state.omSwizzle[i].bIndex() << 8 | state.omSwizzle[i].aIndex() << 12, 0x3210u);
          }
        }
      }

      for (uint32_t i = 0; i < MaxNumSpecConstants; i++)
        specData.set(getSpecId(i), state.sc.specConstants[i], 0u);

      return specData;
    }

  }


  VkPipeline DxvkGraphicsPipeline::linkPipeline(
    const DxvkGraphicsPipelineStateInfo& state,
    const DxvkRenderPass*                renderPass) {
    bool created[4] = { };

    VkPipeline libs[4];
    libs[LibVertexInput]    = this->getVertexInputLibrary(state, created[LibVertexInput]);
    libs[LibPreRaster]      = this->getPreRasterLibrary(state, renderPass, created[LibPreRaster]);
    libs[LibFragmentShader] = this->getFragmentShaderLibrary(state, renderPass, created[LibFragmentShader]);
    libs[LibFragmentOutput] = this->getFragmentOutputLibrary(state, renderPass, created[LibFragmentOutput]);

    for (uint32_t i = 0; i < 4; i++) {
      if (libs[i] == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;
    }

    VkPipelineLibraryCreateInfoKHR libInfo = { };
    libInfo.sType        = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
    libInfo.libraryCount = 4;
    libInfo.pLibraries   = libs;

    VkGraphicsPipelineCreateInfo info = { };
    info.sType              = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext              = &libInfo;
    info.flags              = 0;
    info.layout             = m_layout->pipelineLayout();
    info.renderPass         = renderPass->getDefaultHandle();
    info.subpass            = 0;
    info.basePipelineHandle = VK_NULL_HANDLE;
    info.basePipelineIndex  = -1;

    if (!useFastLink())
      info.flags |= VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT;

    auto t0 = dxvk::high_resolution_clock::now();

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult vr = m_vkd->vkCreateGraphicsPipelines(m_vkd->device(),
      m_pipeMgr->m_cache->handle(), 1, &info, nullptr, &pipeline);

    auto t1 = dxvk::high_resolution_clock::now();
    uint64_t us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    if (vr != VK_SUCCESS) {
      Logger::err(str::format("panDXVK gplasync: linking libraries failed, VkResult ",
        int32_t(vr), ", vs=", m_shaders.vs->debugName()));
      return VK_NULL_HANDLE;
    }

    m_pipeMgr->m_gplStats.linkMicros += us;

    if (m_pipeMgr->m_gplStats.linked.fetch_add(1) == 0) {
      Logger::info(str::format("panDXVK gplasync: first pipeline linked from libraries in ",
        us / 1000.0, " ms, the library path works on this device (vs=",
        m_shaders.vs->debugName(), "), link mode ",
        useFastLink() ? "fast" : "optimized"));
    }

    if (m_pipeMgr->m_asyncLog) {
      Logger::info(str::format("panDXVK gplasync: linked vs=", m_shaders.vs->debugName(),
        " in ", us / 1000.0, " ms, new libraries: vertex input=", created[LibVertexInput] ? 1 : 0,
        " pre-raster=", created[LibPreRaster] ? 1 : 0,
        " fragment shader=", created[LibFragmentShader] ? 1 : 0,
        " fragment output=", created[LibFragmentOutput] ? 1 : 0));
    }

    return pipeline;
  }


  VkPipeline DxvkGraphicsPipeline::findLibrary(
    const std::vector<LibraryEntry>&     libs,
    const LibraryKey&                    key) const {
    for (const auto& lib : libs) {
      if (lib.key == key)
        return lib.pipeline;
    }

    return VK_NULL_HANDLE;
  }


  VkPipeline DxvkGraphicsPipeline::createLibrary(
          VkGraphicsPipelineCreateInfo&     info,
          VkGraphicsPipelineLibraryFlagsEXT flags,
          uint32_t                          index) const {
    VkGraphicsPipelineLibraryCreateInfoEXT libInfo = { };
    libInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
    libInfo.flags = flags;

    info.pNext = &libInfo;
    info.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;

    if (!useFastLink())
      info.flags |= VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;

    auto t0 = dxvk::high_resolution_clock::now();

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult vr = m_vkd->vkCreateGraphicsPipelines(m_vkd->device(),
      m_pipeMgr->m_cache->handle(), 1, &info, nullptr, &pipeline);

    auto t1 = dxvk::high_resolution_clock::now();
    uint64_t us = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    info.pNext = nullptr;

    if (vr != VK_SUCCESS) {
      Logger::err(str::format("panDXVK gplasync: failed to create ", LibraryNames[index],
        " library, VkResult ", int32_t(vr), ", vs=", m_shaders.vs->debugName()));
      return VK_NULL_HANDLE;
    }

    m_pipeMgr->m_gplStats.created[index] += 1;
    m_pipeMgr->m_gplStats.libMicros += us;

    if (m_pipeMgr->m_asyncLog) {
      Logger::info(str::format("panDXVK gplasync: created ", LibraryNames[index],
        " library in ", us / 1000.0, " ms (vs=", m_shaders.vs->debugName(), ")"));
    }

    return pipeline;
  }


  VkPipeline DxvkGraphicsPipeline::getVertexInputLibrary(
    const DxvkGraphicsPipelineStateInfo& state,
          bool&                          created) {
    created = false;

    LibraryKey key;
    key.state.ia = state.ia;
    key.state.il = state.il;

    for (uint32_t i = 0; i < MaxNumVertexAttributes; i++)
      key.state.ilAttributes[i] = state.ilAttributes[i];

    for (uint32_t i = 0; i < MaxNumVertexBindings; i++)
      key.state.ilBindings[i] = state.ilBindings[i];

    VkPipeline pipeline = this->findLibrary(m_vertexInputLibs, key);

    if (pipeline != VK_NULL_HANDLE) {
      m_pipeMgr->m_gplStats.reused[LibVertexInput] += 1;
      return pipeline;
    }

    // Generate per-instance attribute divisors
    std::array<VkVertexInputBindingDivisorDescriptionEXT, MaxNumVertexBindings> viDivisorDesc;
    uint32_t                                                                    viDivisorCount = 0;

    for (uint32_t i = 0; i < state.il.bindingCount(); i++) {
      if (state.ilBindings[i].inputRate() == VK_VERTEX_INPUT_RATE_INSTANCE
       && state.ilBindings[i].divisor()   != 1) {
        const uint32_t id = viDivisorCount++;

        viDivisorDesc[id].binding = i;
        viDivisorDesc[id].divisor = state.ilBindings[i].divisor();
      }
    }

    // Compact vertex bindings so that we can more easily update vertex buffers
    std::array<VkVertexInputAttributeDescription, MaxNumVertexAttributes> viAttribs;
    std::array<VkVertexInputBindingDescription,   MaxNumVertexBindings>   viBindings;
    std::array<uint32_t,                          MaxNumVertexBindings>   viBindingMap = { };

    for (uint32_t i = 0; i < state.il.bindingCount(); i++) {
      viBindings[i] = state.ilBindings[i].description();
      viBindings[i].binding = i;
      viBindingMap[state.ilBindings[i].binding()] = i;
    }

    for (uint32_t i = 0; i < state.il.attributeCount(); i++) {
      viAttribs[i] = state.ilAttributes[i].description();
      viAttribs[i].binding = viBindingMap[state.ilAttributes[i].binding()];
    }

    VkPipelineVertexInputDivisorStateCreateInfoEXT viDivisorInfo;
    viDivisorInfo.sType                     = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT;
    viDivisorInfo.pNext                     = nullptr;
    viDivisorInfo.vertexBindingDivisorCount = viDivisorCount;
    viDivisorInfo.pVertexBindingDivisors    = viDivisorDesc.data();

    VkPipelineVertexInputStateCreateInfo viInfo;
    viInfo.sType                            = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    viInfo.pNext                            = &viDivisorInfo;
    viInfo.flags                            = 0;
    viInfo.vertexBindingDescriptionCount    = state.il.bindingCount();
    viInfo.pVertexBindingDescriptions       = viBindings.data();
    viInfo.vertexAttributeDescriptionCount  = state.il.attributeCount();
    viInfo.pVertexAttributeDescriptions     = viAttribs.data();

    if (viDivisorCount == 0)
      viInfo.pNext = viDivisorInfo.pNext;

    if (!m_pipeMgr->m_device->features().extVertexAttributeDivisor.vertexAttributeInstanceRateDivisor)
      viInfo.pNext = viDivisorInfo.pNext;

    VkPipelineInputAssemblyStateCreateInfo iaInfo;
    iaInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaInfo.pNext                  = nullptr;
    iaInfo.flags                  = 0;
    iaInfo.topology               = state.ia.primitiveTopology();
    iaInfo.primitiveRestartEnable = state.ia.primitiveRestart();

    VkGraphicsPipelineCreateInfo info = { };
    info.sType                = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pVertexInputState    = &viInfo;
    info.pInputAssemblyState  = &iaInfo;
    info.layout               = m_layout->pipelineLayout();
    info.basePipelineHandle   = VK_NULL_HANDLE;
    info.basePipelineIndex    = -1;

    pipeline = this->createLibrary(info,
      VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT, LibVertexInput);

    if (pipeline == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;

    m_vertexInputLibs.push_back({ key, pipeline });
    created = true;
    return pipeline;
  }


  VkPipeline DxvkGraphicsPipeline::getPreRasterLibrary(
    const DxvkGraphicsPipelineStateInfo& state,
    const DxvkRenderPass*                renderPass,
          bool&                          created) {
    created = false;

    VkSampleCountFlagBits sampleCount = getSampleCount(state);

    LibraryKey key;
    key.state.bsBindingMask = state.bsBindingMask;
    key.state.ia            = state.ia;
    key.state.rs            = state.rs;
    key.state.sc            = state.sc;
    key.renderPass          = renderPass;
    key.extra               = (uint64_t(sampleCount) << 32) | getProvidedVertexInputs(state);

    VkPipeline pipeline = this->findLibrary(m_preRasterLibs, key);

    if (pipeline != VK_NULL_HANDLE) {
      m_pipeMgr->m_gplStats.reused[LibPreRaster] += 1;
      return pipeline;
    }

    DxvkSpecConstants specData = buildSpecConstants(state,
      sampleCount, m_layout->bindingCount(), m_fsOut, false);

    VkSpecializationInfo specInfo = specData.getSpecInfo();

    auto vsm  = createShaderModule(m_shaders.vs,  state);
    auto tcsm = createShaderModule(m_shaders.tcs, state);
    auto tesm = createShaderModule(m_shaders.tes, state);
    auto gsm  = createShaderModule(m_shaders.gs,  state);

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    if (vsm)  stages.push_back(vsm.stageInfo(&specInfo));
    if (tcsm) stages.push_back(tcsm.stageInfo(&specInfo));
    if (tesm) stages.push_back(tesm.stageInfo(&specInfo));
    if (gsm)  stages.push_back(gsm.stageInfo(&specInfo));

    int32_t rasterizedStream = m_shaders.gs != nullptr
      ? m_shaders.gs->info().xfbRasterizedStream
      : 0;

    std::array<VkDynamicState, 3> dynamicStates;
    uint32_t                      dynamicStateCount = 0;

    dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
    dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;

    if (state.useDynamicDepthBias())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;

    VkPipelineTessellationStateCreateInfo tsInfo;
    tsInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tsInfo.pNext                  = nullptr;
    tsInfo.flags                  = 0;
    tsInfo.patchControlPoints     = state.ia.patchVertexCount();

    VkPipelineViewportStateCreateInfo vpInfo;
    vpInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpInfo.pNext                  = nullptr;
    vpInfo.flags                  = 0;
    vpInfo.viewportCount          = state.rs.viewportCount();
    vpInfo.pViewports             = nullptr;
    vpInfo.scissorCount           = state.rs.viewportCount();
    vpInfo.pScissors              = nullptr;

    VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeInfo;
    conservativeInfo.sType        = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT;
    conservativeInfo.pNext        = nullptr;
    conservativeInfo.flags        = 0;
    conservativeInfo.conservativeRasterizationMode = state.rs.conservativeMode();
    conservativeInfo.extraPrimitiveOverestimationSize = 0.0f;

    VkPipelineRasterizationStateStreamCreateInfoEXT xfbStreamInfo;
    xfbStreamInfo.sType           = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT;
    xfbStreamInfo.pNext           = nullptr;
    xfbStreamInfo.flags           = 0;
    xfbStreamInfo.rasterizationStream = uint32_t(rasterizedStream);

    VkPipelineRasterizationDepthClipStateCreateInfoEXT rsDepthClipInfo;
    rsDepthClipInfo.sType         = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT;
    rsDepthClipInfo.pNext         = nullptr;
    rsDepthClipInfo.flags         = 0;
    rsDepthClipInfo.depthClipEnable = state.rs.depthClipEnable();

    VkPipelineRasterizationStateCreateInfo rsInfo;
    rsInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsInfo.pNext                  = nullptr;
    rsInfo.flags                  = 0;
    rsInfo.depthClampEnable       = VK_TRUE;
    rsInfo.rasterizerDiscardEnable = rasterizedStream < 0;
    rsInfo.polygonMode            = state.rs.polygonMode();
    rsInfo.cullMode               = state.rs.cullMode();
    rsInfo.frontFace              = state.rs.frontFace();
    rsInfo.depthBiasEnable        = state.rs.depthBiasEnable();
    rsInfo.depthBiasConstantFactor= 0.0f;
    rsInfo.depthBiasClamp         = 0.0f;
    rsInfo.depthBiasSlopeFactor   = 0.0f;
    rsInfo.lineWidth              = 1.0f;

    if (rasterizedStream > 0)
      xfbStreamInfo.pNext = std::exchange(rsInfo.pNext, &xfbStreamInfo);

    if (conservativeInfo.conservativeRasterizationMode != VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT)
      conservativeInfo.pNext = std::exchange(rsInfo.pNext, &conservativeInfo);

    if (m_pipeMgr->m_device->features().extDepthClipEnable.depthClipEnable)
      rsDepthClipInfo.pNext = std::exchange(rsInfo.pNext, &rsDepthClipInfo);
    else
      rsInfo.depthClampEnable = !state.rs.depthClipEnable();

    VkPipelineDynamicStateCreateInfo dyInfo;
    dyInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyInfo.pNext                  = nullptr;
    dyInfo.flags                  = 0;
    dyInfo.dynamicStateCount      = dynamicStateCount;
    dyInfo.pDynamicStates         = dynamicStates.data();

    VkGraphicsPipelineCreateInfo info = { };
    info.sType                = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount           = stages.size();
    info.pStages              = stages.data();
    info.pTessellationState   = tsInfo.patchControlPoints ? &tsInfo : nullptr;
    info.pViewportState       = &vpInfo;
    info.pRasterizationState  = &rsInfo;
    info.pDynamicState        = &dyInfo;
    info.layout               = m_layout->pipelineLayout();
    info.renderPass           = renderPass->getDefaultHandle();
    info.subpass              = 0;
    info.basePipelineHandle   = VK_NULL_HANDLE;
    info.basePipelineIndex    = -1;

    pipeline = this->createLibrary(info,
      VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT, LibPreRaster);

    if (pipeline == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;

    m_preRasterLibs.push_back({ key, pipeline });
    created = true;
    return pipeline;
  }


  VkPipeline DxvkGraphicsPipeline::getFragmentShaderLibrary(
    const DxvkGraphicsPipelineStateInfo& state,
    const DxvkRenderPass*                renderPass,
          bool&                          created) {
    created = false;

    VkSampleCountFlagBits sampleCount = getSampleCount(state);

    LibraryKey key;
    key.state.bsBindingMask = state.bsBindingMask;
    key.state.ms            = state.ms;
    key.state.ds            = state.ds;
    key.state.sc            = state.sc;
    key.state.dsFront       = state.dsFront;
    key.state.dsBack        = state.dsBack;

    for (uint32_t i = 0; i < MaxNumRenderTargets; i++)
      key.state.omSwizzle[i] = state.omSwizzle[i];

    key.renderPass          = renderPass;
    key.extra               = (uint64_t(sampleCount) << 32) | (usesDualSourceBlend(state) ? 1u : 0u);

    VkPipeline pipeline = this->findLibrary(m_fragmentShaderLibs, key);

    if (pipeline != VK_NULL_HANDLE) {
      m_pipeMgr->m_gplStats.reused[LibFragmentShader] += 1;
      return pipeline;
    }

    DxvkRenderPassFormat passFormat = renderPass->format();

    DxvkSpecConstants specData = buildSpecConstants(state,
      sampleCount, m_layout->bindingCount(), m_fsOut, true);

    VkSpecializationInfo specInfo = specData.getSpecInfo();

    auto fsm = createShaderModule(m_shaders.fs, state);

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    if (fsm) stages.push_back(fsm.stageInfo(&specInfo));

    std::array<VkDynamicState, 2> dynamicStates;
    uint32_t                      dynamicStateCount = 0;

    if (state.useDynamicDepthBounds())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;

    if (state.useDynamicStencilRef())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;

    uint32_t sampleMask = state.ms.sampleMask();

    VkPipelineMultisampleStateCreateInfo msInfo;
    msInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msInfo.pNext                  = nullptr;
    msInfo.flags                  = 0;
    msInfo.rasterizationSamples   = sampleCount;
    msInfo.sampleShadingEnable    = m_common.msSampleShadingEnable;
    msInfo.minSampleShading       = m_common.msSampleShadingFactor;
    msInfo.pSampleMask            = &sampleMask;
    msInfo.alphaToCoverageEnable  = state.ms.enableAlphaToCoverage();
    msInfo.alphaToOneEnable       = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo dsInfo;
    dsInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsInfo.pNext                  = nullptr;
    dsInfo.flags                  = 0;
    dsInfo.depthTestEnable        = state.ds.enableDepthTest();
    dsInfo.depthWriteEnable       = state.ds.enableDepthWrite() && !util::isDepthReadOnlyLayout(passFormat.depth.layout);
    dsInfo.depthCompareOp         = state.ds.depthCompareOp();
    dsInfo.depthBoundsTestEnable  = state.ds.enableDepthBoundsTest();
    dsInfo.stencilTestEnable      = state.ds.enableStencilTest();
    dsInfo.front                  = state.dsFront.state();
    dsInfo.back                   = state.dsBack.state();
    dsInfo.minDepthBounds         = 0.0f;
    dsInfo.maxDepthBounds         = 1.0f;

    VkPipelineDynamicStateCreateInfo dyInfo;
    dyInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyInfo.pNext                  = nullptr;
    dyInfo.flags                  = 0;
    dyInfo.dynamicStateCount      = dynamicStateCount;
    dyInfo.pDynamicStates         = dynamicStates.data();

    VkGraphicsPipelineCreateInfo info = { };
    info.sType                = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount           = stages.size();
    info.pStages              = stages.data();
    info.pMultisampleState    = &msInfo;
    info.pDepthStencilState   = &dsInfo;
    info.pDynamicState        = &dyInfo;
    info.layout               = m_layout->pipelineLayout();
    info.renderPass           = renderPass->getDefaultHandle();
    info.subpass              = 0;
    info.basePipelineHandle   = VK_NULL_HANDLE;
    info.basePipelineIndex    = -1;

    pipeline = this->createLibrary(info,
      VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT, LibFragmentShader);

    if (pipeline == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;

    m_fragmentShaderLibs.push_back({ key, pipeline });
    created = true;
    return pipeline;
  }


  VkPipeline DxvkGraphicsPipeline::getFragmentOutputLibrary(
    const DxvkGraphicsPipelineStateInfo& state,
    const DxvkRenderPass*                renderPass,
          bool&                          created) {
    created = false;

    VkSampleCountFlagBits sampleCount = getSampleCount(state);

    LibraryKey key;
    key.state.ms      = state.ms;
    key.state.om      = state.om;
    key.state.ds      = state.ds;
    key.state.dsFront = state.dsFront;
    key.state.dsBack  = state.dsBack;

    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      key.state.omBlend[i]   = state.omBlend[i];
      key.state.omSwizzle[i] = state.omSwizzle[i];
    }

    key.renderPass = renderPass;
    key.extra      = uint64_t(sampleCount) << 32;

    VkPipeline pipeline = this->findLibrary(m_fragmentOutputLibs, key);

    if (pipeline != VK_NULL_HANDLE) {
      m_pipeMgr->m_gplStats.reused[LibFragmentOutput] += 1;
      return pipeline;
    }

    DxvkRenderPassFormat passFormat = renderPass->format();

    // Fix up color write masks using the component mappings
    std::array<VkPipelineColorBlendAttachmentState, MaxNumRenderTargets> omBlendAttachments;

    const VkColorComponentFlags fullMask
      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    for (uint32_t i = 0; i < MaxNumRenderTargets; i++) {
      auto formatInfo = imageFormatInfo(passFormat.color[i].format);
      omBlendAttachments[i] = state.omBlend[i].state();

      if (!(m_fsOut & (1 << i)) || !formatInfo) {
        omBlendAttachments[i].colorWriteMask = 0;
      } else {
        if (omBlendAttachments[i].colorWriteMask != fullMask) {
          omBlendAttachments[i].colorWriteMask = util::remapComponentMask(
            state.omBlend[i].colorWriteMask(), state.omSwizzle[i].mapping());
        }

        omBlendAttachments[i].colorWriteMask &= formatInfo->componentMask;

        if (omBlendAttachments[i].colorWriteMask == formatInfo->componentMask) {
          omBlendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                               | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }
      }
    }

    std::array<VkDynamicState, 3> dynamicStates;
    uint32_t                      dynamicStateCount = 0;

    if (state.useDynamicBlendConstants())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_BLEND_CONSTANTS;

    if (state.useDynamicDepthBounds())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;

    if (state.useDynamicStencilRef())
      dynamicStates[dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;

    uint32_t sampleMask = state.ms.sampleMask();

    VkPipelineMultisampleStateCreateInfo msInfo;
    msInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msInfo.pNext                  = nullptr;
    msInfo.flags                  = 0;
    msInfo.rasterizationSamples   = sampleCount;
    msInfo.sampleShadingEnable    = m_common.msSampleShadingEnable;
    msInfo.minSampleShading       = m_common.msSampleShadingFactor;
    msInfo.pSampleMask            = &sampleMask;
    msInfo.alphaToCoverageEnable  = state.ms.enableAlphaToCoverage();
    msInfo.alphaToOneEnable       = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo dsInfo;
    dsInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsInfo.pNext                  = nullptr;
    dsInfo.flags                  = 0;
    dsInfo.depthTestEnable        = state.ds.enableDepthTest();
    dsInfo.depthWriteEnable       = state.ds.enableDepthWrite() && !util::isDepthReadOnlyLayout(passFormat.depth.layout);
    dsInfo.depthCompareOp         = state.ds.depthCompareOp();
    dsInfo.depthBoundsTestEnable  = state.ds.enableDepthBoundsTest();
    dsInfo.stencilTestEnable      = state.ds.enableStencilTest();
    dsInfo.front                  = state.dsFront.state();
    dsInfo.back                   = state.dsBack.state();
    dsInfo.minDepthBounds         = 0.0f;
    dsInfo.maxDepthBounds         = 1.0f;

    VkPipelineColorBlendStateCreateInfo cbInfo;
    cbInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbInfo.pNext                  = nullptr;
    cbInfo.flags                  = 0;
    cbInfo.logicOpEnable          = state.om.enableLogicOp();
    cbInfo.logicOp                = state.om.logicOp();
    cbInfo.attachmentCount        = DxvkLimits::MaxNumRenderTargets;
    cbInfo.pAttachments           = omBlendAttachments.data();

    for (uint32_t i = 0; i < 4; i++)
      cbInfo.blendConstants[i] = 0.0f;

    VkPipelineDynamicStateCreateInfo dyInfo;
    dyInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyInfo.pNext                  = nullptr;
    dyInfo.flags                  = 0;
    dyInfo.dynamicStateCount      = dynamicStateCount;
    dyInfo.pDynamicStates         = dynamicStates.data();

    VkGraphicsPipelineCreateInfo info = { };
    info.sType                = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pMultisampleState    = &msInfo;
    info.pDepthStencilState   = &dsInfo;
    info.pColorBlendState     = &cbInfo;
    info.pDynamicState        = &dyInfo;
    info.layout               = m_layout->pipelineLayout();
    info.renderPass           = renderPass->getDefaultHandle();
    info.subpass              = 0;
    info.basePipelineHandle   = VK_NULL_HANDLE;
    info.basePipelineIndex    = -1;

    pipeline = this->createLibrary(info,
      VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT, LibFragmentOutput);

    if (pipeline == VK_NULL_HANDLE)
      return VK_NULL_HANDLE;

    m_fragmentOutputLibs.push_back({ key, pipeline });
    created = true;
    return pipeline;
  }

}
