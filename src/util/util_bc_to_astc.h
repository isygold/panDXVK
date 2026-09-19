#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <memory>
#include "../dxvk/dxvk_format.h"
#include "util_bc_decode.h"
#include "util_astc_encode.h"

namespace dxvk::util {

  /**
   * \brief Test-build override: force BC→ASTC even when the driver
   * claims BC support (e.g. wrapper BCN layer faking the feature).
   *
   * Opt-in via PANDXVK_FORCE_TRANSCODE=1 (any value except unset/empty/"0").
   * Explicitly a testing knob: forcing transcode adds CPU cost + ASTC loss
   * on setups where the wrapper path renders fine. Cached on first call.
   */
  inline bool forceTranscodeEnabled() {
    static const bool enabled = [] {
      const char* v = std::getenv("PANDXVK_FORCE_TRANSCODE");
      return v != nullptr && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
    }();
    return enabled;
  }

  /**
   * \brief Checks if a DXGI format is a BC compressed format
   */
  inline bool isBcFormat(DXGI_FORMAT format) {
    return format >= DXGI_FORMAT_BC1_TYPELESS
        && format <= DXGI_FORMAT_BC7_UNORM_SRGB;
  }


  /**
   * \brief Checks if a VkFormat is a BC compressed format
   */
  inline bool isBcFormat(VkFormat format) {
    return format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK
        && format <= VK_FORMAT_BC7_SRGB_BLOCK;
  }


  /**
   * \brief Maps a VkFormat BC to its BcFormat enum
   */
  inline BcFormat vkFormatToBcFormat(VkFormat format) {
    switch (format) {
      case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
      case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
      case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        return BcFormat::BC1;

      case VK_FORMAT_BC2_UNORM_BLOCK:
      case VK_FORMAT_BC2_SRGB_BLOCK:
        return BcFormat::BC2;

      case VK_FORMAT_BC3_UNORM_BLOCK:
      case VK_FORMAT_BC3_SRGB_BLOCK:
        return BcFormat::BC3;

      case VK_FORMAT_BC4_UNORM_BLOCK:
      case VK_FORMAT_BC4_SNORM_BLOCK:
        return BcFormat::BC4;

      case VK_FORMAT_BC5_UNORM_BLOCK:
      case VK_FORMAT_BC5_SNORM_BLOCK:
        return BcFormat::BC5;

      case VK_FORMAT_BC6H_UFLOAT_BLOCK:
      case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        return BcFormat::BC6H;

      case VK_FORMAT_BC7_UNORM_BLOCK:
      case VK_FORMAT_BC7_SRGB_BLOCK:
        return BcFormat::BC7;

      default:
        return BcFormat::BC1; // fallback
    }
  }


  /**
   * \brief Maps a VkFormat BC to its ASTC VkFormat equivalent
   */
  inline VkFormat bcToAstcFormat(VkFormat bcFormat) {
    switch (bcFormat) {
      // BC1 → ASTC 4x4
      case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
      case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;

      // BC2 → ASTC 4x4
      case VK_FORMAT_BC2_UNORM_BLOCK:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case VK_FORMAT_BC2_SRGB_BLOCK:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;

      // BC3 → ASTC 4x4
      case VK_FORMAT_BC3_UNORM_BLOCK:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case VK_FORMAT_BC3_SRGB_BLOCK:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;

      // BC4 → ASTC 4x4
      case VK_FORMAT_BC4_UNORM_BLOCK:
      case VK_FORMAT_BC4_SNORM_BLOCK:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;

      // BC5 → ASTC 4x4
      case VK_FORMAT_BC5_UNORM_BLOCK:
      case VK_FORMAT_BC5_SNORM_BLOCK:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;

      // BC6H (HDR float RGB) → ASTC 4x4 LDR (approximation).
      // Must match the 4x4 encoder: a 6x6 mapping would misinterpret
      // the transcoded blocks (different texel footprint/count).
      // HDR range is clamped (see decodeBc6hBlock).
      case VK_FORMAT_BC6H_UFLOAT_BLOCK:
      case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;

      // BC7 → ASTC 4x4
      case VK_FORMAT_BC7_UNORM_BLOCK:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case VK_FORMAT_BC7_SRGB_BLOCK:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;

      default:
        return VK_FORMAT_UNDEFINED;
    }
  }


  /**
   * \brief Maps a DXGI BC format to its ASTC equivalent
   *
   * \param [in] bcFormat  BC format
   * \returns Corresponding ASTC VkFormat, or VK_FORMAT_UNDEFINED
   */
  inline VkFormat bcToAstcFormat(DXGI_FORMAT bcFormat) {
    switch (bcFormat) {
      // BC1 (0.5 B/px, RGB/RGBA) → ASTC 4x4 (1 B/px)
      case DXGI_FORMAT_BC1_TYPELESS:
      case DXGI_FORMAT_BC1_UNORM:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case DXGI_FORMAT_BC1_UNORM_SRGB:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;

      // BC2 (1 B/px, RGBA) → ASTC 4x4
      case DXGI_FORMAT_BC2_TYPELESS:
      case DXGI_FORMAT_BC2_UNORM:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case DXGI_FORMAT_BC2_UNORM_SRGB:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;

      // BC3 (1 B/px, RGBA) → ASTC 4x4
      case DXGI_FORMAT_BC3_TYPELESS:
      case DXGI_FORMAT_BC3_UNORM:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case DXGI_FORMAT_BC3_UNORM_SRGB:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;

      // BC4 (0.5 B/px, R) → ASTC 4x4
      case DXGI_FORMAT_BC4_TYPELESS:
      case DXGI_FORMAT_BC4_UNORM:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case DXGI_FORMAT_BC4_SNORM:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;

      // BC5 (1 B/px, RG) → ASTC 4x4
      case DXGI_FORMAT_BC5_TYPELESS:
      case DXGI_FORMAT_BC5_UNORM:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case DXGI_FORMAT_BC5_SNORM:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;

      // BC6H (1 B/px, RGB float) → ASTC 4x4 LDR (approximation).
      // Must match the 4x4 encoder (see VkFormat overload note above).
      case DXGI_FORMAT_BC6H_TYPELESS:
      case DXGI_FORMAT_BC6H_UF16:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case DXGI_FORMAT_BC6H_SF16:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;

      // BC7 (1 B/px, RGBA) → ASTC 4x4
      case DXGI_FORMAT_BC7_TYPELESS:
      case DXGI_FORMAT_BC7_UNORM:
        return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
      case DXGI_FORMAT_BC7_UNORM_SRGB:
        return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;

      default:
        return VK_FORMAT_UNDEFINED;
    }
  }


  /**
   * \brief Maps a DXGI BC format to its BcFormat enum
   */
  inline BcFormat dxgiToBcFormat(DXGI_FORMAT format) {
    switch (format) {
      case DXGI_FORMAT_BC1_TYPELESS:
      case DXGI_FORMAT_BC1_UNORM:
      case DXGI_FORMAT_BC1_UNORM_SRGB:
        return BcFormat::BC1;

      case DXGI_FORMAT_BC2_TYPELESS:
      case DXGI_FORMAT_BC2_UNORM:
      case DXGI_FORMAT_BC2_UNORM_SRGB:
        return BcFormat::BC2;

      case DXGI_FORMAT_BC3_TYPELESS:
      case DXGI_FORMAT_BC3_UNORM:
      case DXGI_FORMAT_BC3_UNORM_SRGB:
        return BcFormat::BC3;

      case DXGI_FORMAT_BC4_TYPELESS:
      case DXGI_FORMAT_BC4_UNORM:
      case DXGI_FORMAT_BC4_SNORM:
        return BcFormat::BC4;

      case DXGI_FORMAT_BC5_TYPELESS:
      case DXGI_FORMAT_BC5_UNORM:
      case DXGI_FORMAT_BC5_SNORM:
        return BcFormat::BC5;

      case DXGI_FORMAT_BC6H_TYPELESS:
      case DXGI_FORMAT_BC6H_UF16:
      case DXGI_FORMAT_BC6H_SF16:
        return BcFormat::BC6H;

      case DXGI_FORMAT_BC7_TYPELESS:
      case DXGI_FORMAT_BC7_UNORM:
      case DXGI_FORMAT_BC7_UNORM_SRGB:
        return BcFormat::BC7;

      default:
        return BcFormat::BC1; // fallback
    }
  }


  /**
   * \brief Transcodes BC compressed data to ASTC
   *
   * \param [in]  bcFormat     Original BC DXGI format
   * \param [in]  srcData      Source BC compressed data
   * \param [in]  width        Image width in pixels
   * \param [in]  height       Image height in pixels
   * \param [in]  srcRowPitch  Source row pitch in bytes
   * \param [out] dstData      Destination ASTC data
   * \param [in]  dstRowPitch  Destination row pitch in bytes
   */
  /**
   * \brief Fused per-block BC→ASTC transcode core (no RGBA8 intermediate)
   *
   * Decodes one 4x4 BC block to a 64-byte stack buffer, applies the
   * SNORM→UNORM remap, and encodes straight to ASTC. Eliminates the old
   * width*height*4 heap intermediate (~33 MB at 4K) and halves memory
   * traffic vs the decode-full-image / encode-full-image two-pass path.
   *
   * Edge handling is bit-identical to the old path: decodeBcImage dropped
   * out-of-bounds pixels when writing the RGBA image, and
   * encodeAstcImage4x4 re-gathered with clamp-to-edge. The fused loop
   * gathers from the decoded block with the same clamp, so the encoder
   * sees exactly the same 16 pixels per block.
   *
   * \param [in] bc             Source BC format
   * \param [in] remapR         Remap R channel SNORM→UNORM (BC4/BC5 SNORM)
   * \param [in] remapG         Remap G channel SNORM→UNORM (BC5 SNORM)
   * \param [in] bc6hSigned     BC6H SFLOAT (signed) vs UFLOAT decode
   * \param [in] srcData        Source BC data
   * \param [in] width          Image width in pixels
   * \param [in] height         Image height in pixels
   * \param [in] srcBlockPitch  Source block pitch in bytes (0 = derive)
   * \param [out] dstData       Destination ASTC data
   * \param [in] dstRowPitch    Destination row pitch in bytes (0 = derive)
   */
  inline void transcodeBcBlocksToAstc(
          BcFormat       bc,
          bool           remapR,
          bool           remapG,
          bool           bc6hSigned,
    const uint8_t*       srcData,
          uint32_t       width,
          uint32_t       height,
          uint32_t       srcBlockPitch,
          uint8_t*       dstData,
          VkDeviceSize   dstRowPitch) {
    const uint32_t blockSizeBytes =
      (bc == BcFormat::BC1 || bc == BcFormat::BC4) ? 8 : 16;

    const uint32_t blockWidth  = (width + 3) / 4;
    const uint32_t blockHeight = (height + 3) / 4;

    if (srcBlockPitch == 0)
      srcBlockPitch = blockWidth * blockSizeBytes;
    if (dstRowPitch == 0)
      dstRowPitch = static_cast<VkDeviceSize>(blockWidth) * 16;

    uint8_t decoded[64];    // one 4x4 RGBA8 block from the BC decoder
    uint8_t encPixels[64];  // clamp-gathered input for the ASTC encoder
    uint8_t astcBlock[16];

    for (uint32_t by = 0; by < blockHeight; by++) {
      for (uint32_t bx = 0; bx < blockWidth; bx++) {
        const uint8_t* srcBlock = srcData
          + static_cast<VkDeviceSize>(by) * srcBlockPitch
          + static_cast<VkDeviceSize>(bx) * blockSizeBytes;

        decodeBcBlock(bc, srcBlock, decoded, bc6hSigned);

        // Gather with clamp-to-edge. Interior blocks (the common case)
        // encode straight from the decoded pixels; edge blocks replicate
        // the border pixel exactly like encodeAstcImage4x4's gather.
        const uint8_t* encSrc = decoded;
        if (bx * 4 + 4 > width || by * 4 + 4 > height) {
          for (uint32_t py = 0; py < 4; py++) {
            const uint32_t cy = std::min(by * 4 + py, height - 1) - by * 4;
            for (uint32_t px = 0; px < 4; px++) {
              const uint32_t cx = std::min(bx * 4 + px, width - 1) - bx * 4;
              const uint32_t s = (cy * 4 + cx) * 4;
              const uint32_t d = (py * 4 + px) * 4;
              encPixels[d + 0] = decoded[s + 0];
              encPixels[d + 1] = decoded[s + 1];
              encPixels[d + 2] = decoded[s + 2];
              encPixels[d + 3] = decoded[s + 3];
            }
          }
          encSrc = encPixels;
        }

        // SNORM→UNORM remap for BC4/BC5 SNORM. BC4_SNORM stores signed
        // normalized values ([-1,1]) as uint8 bytes in R; BC5_SNORM in
        // R and G. ASTC is UNORM-only: unorm = (int8_t)snorm + 128.
        // Per-pixel function, so remap-after-gather == remap-then-gather.
        if (remapR || remapG) {
          if (encSrc != encPixels)
            std::memcpy(encPixels, decoded, sizeof(encPixels));
          for (uint32_t i = 0; i < 16; i++) {
            uint8_t* px = encPixels + i * 4;
            if (remapR)
              px[0] = static_cast<uint8_t>(static_cast<int8_t>(px[0]) + 128);
            if (remapG)
              px[1] = static_cast<uint8_t>(static_cast<int8_t>(px[1]) + 128);
          }
          encSrc = encPixels;
        }

        encodeAstcBlock4x4(encSrc, astcBlock);
        std::memcpy(
          dstData + static_cast<VkDeviceSize>(by) * dstRowPitch + bx * 16,
          astcBlock, sizeof(astcBlock));
      }
    }
  }


  inline void transcodeBcToAstc(
          DXGI_FORMAT    bcFormat,
    const uint8_t*       srcData,
          uint32_t       width,
          uint32_t       height,
          VkDeviceSize   srcRowPitch,
          uint8_t*       dstData,
          VkDeviceSize   dstRowPitch) {
    BcFormat bc = dxgiToBcFormat(bcFormat);

#ifndef NDEBUG
    // Debug-gated periodic stats dump (every 1000th call).
    if (bc == BcFormat::BC7) {
      static uint32_t callCount = 0;
      if (++callCount % 1000 == 0) {
        bc7Stats().dump();
      }
    }
#endif

    const bool remapR =
      bcFormat == DXGI_FORMAT_BC4_SNORM || bcFormat == DXGI_FORMAT_BC5_SNORM;
    const bool remapG = bcFormat == DXGI_FORMAT_BC5_SNORM;
    const bool bc6hSigned = bcFormat == DXGI_FORMAT_BC6H_SF16;

    transcodeBcBlocksToAstc(bc, remapR, remapG, bc6hSigned,
      srcData, width, height,
      static_cast<uint32_t>(srcRowPitch),
      dstData, dstRowPitch);
  }


  /**
   * \brief Computes the size of ASTC 4x4 compressed data
   *
   * \param [in] width   Image width
   * \param [in] height  Image height
   * \returns Size in bytes
   */
  inline VkDeviceSize computeAstcImageDataSize(uint32_t width, uint32_t height) {
    uint32_t blockWidth  = (width + 3) / 4;
    uint32_t blockHeight = (height + 3) / 4;
    return static_cast<VkDeviceSize>(blockWidth) * blockHeight * 16;
  }


  /**
   * \brief Transcodes BC data to ASTC 4x4, heap-allocated result
   *
   * Returns nullptr on failure. Caller owns the result.
   * Use this instead of the void overload when the caller
   * needs to hold the transcoded data across scope boundaries.
   *
   * \param [in]  bcFormat    Source BC format (DXGI_FORMAT_BC1..BC7)
   * \param [in]  srcData     Source BC data
   * \param [in]  width       Image width in pixels
   * \param [in]  height      Image height in pixels
   * \param [in]  srcRowPitch Source row pitch in bytes
   * \returns Heap-allocated ASTC 4x4 data, or nullptr on failure
   */
  inline std::unique_ptr<uint8_t[]> transcodeBcToAstcAlloc(
          DXGI_FORMAT    bcFormat,
    const uint8_t*       srcData,
          uint32_t       width,
          uint32_t       height,
          VkDeviceSize   srcRowPitch) {
    VkDeviceSize dstSize = computeAstcImageDataSize(width, height);
    auto dstData = std::make_unique<uint8_t[]>(static_cast<size_t>(dstSize));

    transcodeBcToAstc(bcFormat, srcData, width, height,
                      srcRowPitch, dstData.get(), width * 4);

    return dstData;
  }


  /**
   * \brief Transcodes BC data to ASTC 4x4, heap-allocated result (VkFormat overload)
   *
   * \param [in]  bcFormat    Source BC VkFormat
   * \param [in]  srcData     Source BC data
   * \param [in]  width       Image width in pixels
   * \param [in]  height      Image height in pixels
   * \param [in]  srcRowPitch Source row pitch in bytes
   * \returns Heap-allocated ASTC 4x4 data, or nullptr on failure
   */
  inline std::unique_ptr<uint8_t[]> transcodeBcToAstcAlloc(
          VkFormat         bcFormat,
    const uint8_t*         srcData,
          uint32_t         width,
          uint32_t         height,
          VkDeviceSize     srcRowPitch) {
    VkDeviceSize dstSize = computeAstcImageDataSize(width, height);
    auto dstData = std::make_unique<uint8_t[]>(static_cast<size_t>(dstSize));

    BcFormat bc = vkFormatToBcFormat(bcFormat);

    // SNORM remap was previously missing on this path — the fused core
    // applies it, so BC4/BC5 SNORM now behave like the DXGI overload.
    const bool remapR =
      bcFormat == VK_FORMAT_BC4_SNORM_BLOCK || bcFormat == VK_FORMAT_BC5_SNORM_BLOCK;
    const bool remapG = bcFormat == VK_FORMAT_BC5_SNORM_BLOCK;
    const bool bc6hSigned = bcFormat == VK_FORMAT_BC6H_SFLOAT_BLOCK;

    transcodeBcBlocksToAstc(bc, remapR, remapG, bc6hSigned,
      srcData, width, height,
      static_cast<uint32_t>(srcRowPitch),
      dstData.get(), width * 4);

    return dstData;
  }

}
