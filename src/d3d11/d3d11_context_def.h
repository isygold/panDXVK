#pragma once

#include "d3d11_buffer.h"
#include "d3d11_cmdlist.h"
#include "d3d11_context.h"
#include "d3d11_texture.h"

#include <vector>

namespace dxvk {
  
  struct D3D11DeferredContextMapEntry {
    D3D11DeferredContextMapEntry() { }
    D3D11DeferredContextMapEntry(
            ID3D11Resource*           pResource,
            UINT                      Subresource,
            D3D11_RESOURCE_DIMENSION  ResourceType,
      const D3D11_MAPPED_SUBRESOURCE& MappedResource)
    : Resource(pResource, Subresource, ResourceType),
      MapInfo(MappedResource) { }

    D3D11ResourceRef          Resource;
    D3D11_MAPPED_SUBRESOURCE  MapInfo;
    // panDXVK v5: non-empty only for BC→ASTC remapped textures mapped
    // through a deferred context. Holds the BC source buffer the app
    // writes into; the actual image update happens in Unmap, where we
    // can transcode it. Empty for buffers and non-remapped textures.
    DxvkBufferSlice           ImageSlice;
  };
  
  class D3D11DeferredContext : public D3D11DeviceContext {
    friend class D3D11DeviceContext;
  public:
    
    D3D11DeferredContext(
            D3D11Device*    pParent,
      const Rc<DxvkDevice>& Device,
            UINT            ContextFlags);
    
    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType();
    
    UINT STDMETHODCALLTYPE GetContextFlags();
    
    HRESULT STDMETHODCALLTYPE GetData(
            ID3D11Asynchronous*         pAsync,
            void*                       pData,
            UINT                        DataSize,
            UINT                        GetDataFlags);
    
    void STDMETHODCALLTYPE Begin(
            ID3D11Asynchronous*         pAsync);

    void STDMETHODCALLTYPE End(
            ID3D11Asynchronous*         pAsync);

    void STDMETHODCALLTYPE Flush();

    void STDMETHODCALLTYPE Flush1(
            D3D11_CONTEXT_TYPE          ContextType,
            HANDLE                      hEvent);

    HRESULT STDMETHODCALLTYPE Signal(
            ID3D11Fence*                pFence,
            UINT64                      Value);
    
    HRESULT STDMETHODCALLTYPE Wait(
            ID3D11Fence*                pFence,
            UINT64                      Value);

    void STDMETHODCALLTYPE ExecuteCommandList(
            ID3D11CommandList*          pCommandList,
            BOOL                        RestoreContextState);
    
    HRESULT STDMETHODCALLTYPE FinishCommandList(
            BOOL                        RestoreDeferredContextState,
            ID3D11CommandList**         ppCommandList);
    
    HRESULT STDMETHODCALLTYPE Map(
            ID3D11Resource*             pResource,
            UINT                        Subresource,
            D3D11_MAP                   MapType,
            UINT                        MapFlags,
            D3D11_MAPPED_SUBRESOURCE*   pMappedResource);
    
    void STDMETHODCALLTYPE Unmap(
            ID3D11Resource*             pResource,
            UINT                        Subresource);
    
    void STDMETHODCALLTYPE UpdateSubresource(
            ID3D11Resource*                   pDstResource,
            UINT                              DstSubresource,
      const D3D11_BOX*                        pDstBox,
      const void*                             pSrcData,
            UINT                              SrcRowPitch,
            UINT                              SrcDepthPitch);

    void STDMETHODCALLTYPE UpdateSubresource1(
            ID3D11Resource*                   pDstResource,
            UINT                              DstSubresource,
      const D3D11_BOX*                        pDstBox,
      const void*                             pSrcData,
            UINT                              SrcRowPitch,
            UINT                              SrcDepthPitch,
            UINT                              CopyFlags);

    void STDMETHODCALLTYPE SwapDeviceContextState(
           ID3DDeviceContextState*           pState,
           ID3DDeviceContextState**          ppPreviousState);

  private:
    
    const UINT m_contextFlags;
    
    // Command list that we're recording
    Com<D3D11CommandList> m_commandList;
    
    // Info about currently mapped (sub)resources. Using a vector
    // here is reasonable since there will usually only be a small
    // number of mapped resources per command list.
    std::vector<D3D11DeferredContextMapEntry> m_mappedResources;

    // panDXVK v5: slice handed out by MapImage for a remapped texture,
    // transferred into the matching map entry by Map().
    DxvkBufferSlice m_pendingImageSlice;
    
    // Begun and ended queries, will also be stored in command list
    std::vector<Com<D3D11Query, false>> m_queriesBegun;

    HRESULT MapBuffer(
            ID3D11Resource*               pResource,
            D3D11_MAPPED_SUBRESOURCE*     pMappedResource);
    
    HRESULT MapImage(
            ID3D11Resource*               pResource,
            UINT                          Subresource,
            D3D11_MAPPED_SUBRESOURCE*     pMappedResource);

    // panDXVK v5: transcode the BC bytes the app wrote into a deferred
    // map and push them into the (ASTC) image.
    void CommitPendingImageUpload(
      const D3D11DeferredContextMapEntry&   Entry);

    void CommitAllPendingImageUploads();

    void UpdateMappedBuffer(
            D3D11Buffer*                  pDstBuffer,
            UINT                          Offset,
            UINT                          Length,
      const void*                         pSrcData,
            UINT                          CopyFlags);

    void FinalizeQueries();

    Com<D3D11CommandList> CreateCommandList();
    
    void EmitCsChunk(DxvkCsChunkRef&& chunk);

    void TrackTextureSequenceNumber(
            D3D11CommonTexture*           pResource,
            UINT                          Subresource);

    void TrackBufferSequenceNumber(
            D3D11Buffer*                  pResource);

    D3D11DeferredContextMapEntry* FindMapEntry(
            ID3D11Resource*               pResource,
            UINT                          Subresource);

    void AddMapEntry(
            ID3D11Resource*               pResource,
            UINT                          Subresource,
            D3D11_RESOURCE_DIMENSION      ResourceType,
      const D3D11_MAPPED_SUBRESOURCE&     MapInfo);

    static DxvkCsChunkFlags GetCsChunkFlags(
            D3D11Device*                  pDevice);
    
  };
  
}
