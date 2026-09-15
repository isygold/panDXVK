#pragma once

#include <cstdint>

#include <memory>
#include "../dxvk/dxvk_format.h"
#include "util_bc_decode.h"
#include "util_astc_encode.h"

namespace dxvk::util {

  /**
   * \brief Checks if a DXGI format is a BC compressed format
   */
  inline bool isBcFormat(DXGI_FORMAT format) {
    return format >= DXGI_FORMAT_BC1_TYPELESS
        && format <= DXGI_FORMAT_BC7_SRGB;
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

      // BC6H (1 B/px, RGB float) → ASTC 6x6 LDR (approximation)
      case DXGI_FORMAT_BC6H_TYPELESS:
      case DXGI_FORMAT_BC6H_UF16:
        return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
      case DXGI_FORMAT_BC6H_SF16:
        return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;

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
  inline void transcodeBcToAstc(
          DXGI_FORMAT    bcFormat,
    const uint8_t*       srcData,
          uint32_t       width,
          uint32_t       height,
          VkDeviceSize   srcRowPitch,
          uint8_t*       dstData,
          VkDeviceSize   dstRowPitch) {
    // Step 1: Decode BC to RGBA8 (intermediate)
    // Heap allocation — RGBA8 intermediate is width*height*4 bytes,
    // can be ~33MB for 4K textures. Stack overflow risk if on stack.
    BcFormat bc = dxgiToBcFormat(bcFormat);

    auto rgbaData = std::make_unique<uint8_t[]>(width * height * 4);

    decodeBcImage(bc, srcData, width, height, srcRowPitch,
                  rgbaData.get(), width * 4);

    // Step 2: Encode RGBA8 to ASTC 4x4
    encodeAstcImage4x4(rgbaData.get(), width, height,
                       width * 4, dstData, dstRowPitch);
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

}
