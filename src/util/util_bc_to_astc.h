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

      // BC6H → ASTC 6x6 LDR (approximation)
      case VK_FORMAT_BC6H_UFLOAT_BLOCK:
      case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;

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

    // Log BC7 decode stats at INFO periodically (every 1000th call).
    // INFO by design (testing mode): visible in release builds so tester
    // d3d11.log files show the BC7 mode distribution.
    if (bc == BcFormat::BC7) {
      static uint32_t callCount = 0;
      if (++callCount % 1000 == 0) {
        bc7Stats().dump();
      }
    }

    // Step 1.5: SNORM remap for BC4/BC5
    // BC4_SNORM/BC5_SNORM store signed normalized values ([-1,1]) as
    // uint8 bytes: 0=-1.0, 128≈0.0, 255≈-0.008.
    // ASTC is UNORM-only, so remap: unorm = (int8_t)snorm + 128.
    if (bcFormat == DXGI_FORMAT_BC4_SNORM || bcFormat == DXGI_FORMAT_BC5_SNORM) {
      uint32_t numPixels = width * height;
      for (uint32_t i = 0; i < numPixels; i++) {
        uint8_t* px = rgbaData.get() + i * 4;
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
    encodeAstcImage4x4(rgbaData.get(), width, height,
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

    // Decode BC to RGBA8
    BcFormat bc = vkFormatToBcFormat(bcFormat);
    auto rgbaData = std::make_unique<uint8_t[]>(width * height * 4);

    VkDeviceSize bcBlockPitch = (bc == BcFormat::BC1 || bc == BcFormat::BC4)
      ? ((static_cast<VkDeviceSize>(width) + 3) / 4) * 8
      : ((static_cast<VkDeviceSize>(width) + 3) / 4) * 16;

    decodeBcImage(bc, srcData, width, height, bcBlockPitch,
                  rgbaData.get(), width * 4);

    // Encode RGBA8 to ASTC 4x4
    encodeAstcImage4x4(rgbaData.get(), width, height,
                       width * 4, dstData.get(), width * 4);

    return dstData;
  }

}
