#pragma once

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
    BcFormat bc = dxgiToBcFormat(bcFormat);

    std::vector<uint8_t> rgbaData(width * height * 4);

    decodeBcImage(bc, srcData, width, height, srcRowPitch,
                  rgbaData.data(), width * 4);

#ifndef NDEBUG
    // Log BC7 decode stats periodically (every 1000th call)
    if (bc == BcFormat::BC7) {
      static uint32_t callCount = 0;
      if (++callCount % 1000 == 0) {
        bc7Stats().dump();
      }
    }
#endif

    // Step 1.5: SNORM remap for BC4/BC5
    // BC4_SNORM/BC5_SNORM store signed normalized values ([-1,1]) as
    // uint8 bytes: 0=-1.0, 128≈0.0, 255≈-0.008.
    // ASTC is UNORM-only, so remap: unorm = (int8_t)snorm + 128.
    if (bcFormat == DXGI_FORMAT_BC4_SNORM || bcFormat == DXGI_FORMAT_BC5_SNORM) {
      uint32_t numPixels = width * height;
      for (uint32_t i = 0; i < numPixels; i++) {
        uint8_t* px = &rgbaData[i * 4];
        // BC4_SNORM: R channel only; BC5_SNORM: R and G channels
        int8_t s = static_cast<int8_t>(px[0]);
        px[0] = static_cast<uint8_t>(s + 128);
        if (bcFormat == DXGI_FORMAT_BC5_SNORM) {
          s = static_cast<int8_t>(px[1]);
          px[1] = static_cast<uint8_t>(s + 128);
        }
      }
    }

    // Step 2: Encode RGBA8 to ASTC 4x4
    encodeAstcImage4x4(rgbaData.data(), width, height,
                       width * 4, dstData, dstRowPitch);
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
