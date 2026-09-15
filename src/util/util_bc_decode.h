#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>

namespace dxvk::util {

  /**
   * \brief BC format identifiers
   */
  enum class BcFormat {
    BC1, BC2, BC3, BC4, BC5, BC6H, BC7,
  };

  /**
   * \brief Decodes a BC compressed block to 4x4 RGBA8 pixels
   *
   * \param [in]  bcFormat  BC format to decode
   * \param [in]  block     Pointer to the compressed block data
   * \param [out] pixels    Output 4x4 RGBA8 pixel buffer (16 bytes)
   */
  inline void decodeBcBlock(
          BcFormat       bcFormat,
    const uint8_t*       block,
          uint8_t*       pixels) {
    // Helper: double 6-bit color to 8-bit
    auto expand6 = [](uint32_t v) -> uint8_t {
      return static_cast<uint8_t>((v << 2) | (v >> 4));
    };

    // Helper: double 5-bit color to 8-bit
    auto expand5 = [](uint32_t v) -> uint8_t {
      return static_cast<uint8_t>((v << 3) | (v >> 2));
    };

    // Helper: interpolate between two colors
    auto lerp565 = [expand5, expand6](uint16_t c0, uint16_t c1, uint32_t t) -> uint32_t {
      uint32_t r0 = (c0 >> 11) & 0x1F;
      uint32_t g0 = (c0 >> 5)  & 0x3F;
      uint32_t b0 =  c0        & 0x1F;
      uint32_t r1 = (c1 >> 11) & 0x1F;
      uint32_t g1 = (c1 >> 5)  & 0x3F;
      uint32_t b1 =  c1        & 0x1F;
      uint32_t r = (r0 * (8 - t) + r1 * t) / 8;
      uint32_t g = (g0 * (8 - t) + g1 * t) / 8;
      uint32_t b = (b0 * (8 - t) + b1 * t) / 8;
      return (expand5(r)) | (expand6(g) << 8) | (expand5(b) << 16) | (0xFFu << 24);
    };

    // Helper: interpolate two RGBA8 colors
    auto lerpRgba = [](const uint8_t* c0, const uint8_t* c1, uint32_t t) -> uint32_t {
      uint32_t r = (c0[0] * (256 - t) + c1[0] * t) / 256;
      uint32_t g = (c0[1] * (256 - t) + c1[1] * t) / 256;
      uint32_t b = (c0[2] * (256 - t) + c1[2] * t) / 256;
      uint32_t a = (c0[3] * (256 - t) + c1[3] * t) / 256;
      return r | (g << 8) | (b << 16) | (a << 24);
    };

    switch (bcFormat) {
      case BcFormat::BC1: {
        // BC1: 8 bytes per block, 4x4 pixels, 1-bit alpha or opaque
        uint16_t c0 = block[0] | (block[1] << 8);
        uint16_t c1 = block[2] | (block[3] << 8);
        uint32_t indices = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);

        bool hasAlpha = c0 <= c1;
        uint32_t colors[4];
        colors[0] = lerp565(c0, c1, 0);
        colors[1] = lerp565(c0, c1, 2);
        colors[2] = lerp565(c0, c1, 3);
        if (hasAlpha) {
          colors[3] = 0x00000000; // transparent black
        } else {
          colors[3] = lerp565(c0, c1, 1);
        }

        for (int i = 0; i < 16; i++) {
          uint32_t idx = (indices >> (2 * i)) & 3;
          uint32_t rgba = colors[idx];
          pixels[i * 4 + 0] = (rgba)       & 0xFF;
          pixels[i * 4 + 1] = (rgba >> 8)  & 0xFF;
          pixels[i * 4 + 2] = (rgba >> 16) & 0xFF;
          pixels[i * 4 + 3] = (rgba >> 24) & 0xFF;
        }
        break;
      }

      case BcFormat::BC2: {
        // BC2: 16 bytes per block, 4-bit alpha + BC1 color
        uint16_t c0 = block[8]  | (block[9]  << 8);
        uint16_t c1 = block[10] | (block[11] << 8);
        uint32_t colorIndices = block[12] | (block[13] << 8)
                              | (block[14] << 16) | (block[15] << 24);

        uint32_t colors[4];
        colors[0] = lerp565(c0, c1, 0);
        colors[1] = lerp565(c0, c1, 2);
        colors[2] = lerp565(c0, c1, 3);
        colors[3] = lerp565(c0, c1, 1);

        // Alpha: 2 bytes of 4-bit alpha per row
        for (int i = 0; i < 16; i++) {
          uint32_t ci = (colorIndices >> (2 * i)) & 3;
          uint32_t rgba = colors[ci];

          // Extract 4-bit alpha: block[0..7], each byte holds 2 pixels
          uint32_t alphaBits;
          if (i < 8)
            alphaBits = (block[i / 2] >> (4 * (i % 2))) & 0xF;
          else
            alphaBits = (block[8 + (i - 8) / 2] >> (4 * ((i - 8) % 2))) & 0xF;

          uint32_t alpha = alphaBits | (alphaBits << 4);
          pixels[i * 4 + 0] = (rgba)       & 0xFF;
          pixels[i * 4 + 1] = (rgba >> 8)  & 0xFF;
          pixels[i * 4 + 2] = (rgba >> 16) & 0xFF;
          pixels[i * 4 + 3] = alpha;
        }
        break;
      }

      case BcFormat::BC3: {
        // BC3: 16 bytes per block, 8-bit alpha + BC1 color
        uint16_t c0 = block[8]  | (block[9]  << 8);
        uint16_t c1 = block[10] | (block[11] << 8);
        uint32_t colorIndices = block[12] | (block[13] << 8)
                              | (block[14] << 16) | (block[15] << 24);

        uint32_t colors[4];
        colors[0] = lerp565(c0, c1, 0);
        colors[1] = lerp565(c0, c1, 2);
        colors[2] = lerp565(c0, c1, 3);
        colors[3] = lerp565(c0, c1, 1);

        // Decode 8-bit alpha from block[0..7]
        uint8_t alphaBlock[16];
        uint8_t alpha0 = block[0];
        uint8_t alpha1 = block[1];
        alphaBlock[0]  = alpha0;
        alphaBlock[1]  = alpha1;

        if (alpha0 > alpha1) {
          for (int i = 1; i < 7; i++) {
            uint8_t bits = (block[2 + i / 2] >> (3 * (i % 2))) & 0x7;
            alphaBlock[i + 1] = static_cast<uint8_t>(
              ((7 - bits) * alpha0 + bits * alpha1) / 7);
          }
          alphaBlock[7] = 0;
          for (int i = 0; i < 8; i++) {
            uint8_t bits = (block[2 + (i + 6) / 2] >> (3 * ((i + 6) % 2))) & 0x7;
            alphaBlock[i + 8] = static_cast<uint8_t>(
              ((7 - bits) * alpha0 + bits * alpha1) / 7);
          }
          alphaBlock[15] = 255;
        } else {
          for (int i = 1; i < 5; i++) {
            uint8_t bits = (block[2 + i / 2] >> (3 * (i % 2))) & 0x7;
            alphaBlock[i + 1] = static_cast<uint8_t>(
              ((5 - bits) * alpha0 + bits * alpha1) / 5);
          }
          alphaBlock[5] = 0;
          alphaBlock[6] = 255;
          for (int i = 0; i < 9; i++) {
            uint8_t bits = (block[2 + (i + 4) / 2] >> (3 * ((i + 4) % 2))) & 0x7;
            alphaBlock[i + 7] = static_cast<uint8_t>(
              ((5 - bits) * alpha0 + bits * alpha1) / 5);
          }
        }

        for (int i = 0; i < 16; i++) {
          uint32_t ci = (colorIndices >> (2 * i)) & 3;
          uint32_t rgba = colors[ci];
          pixels[i * 4 + 0] = (rgba)       & 0xFF;
          pixels[i * 4 + 1] = (rgba >> 8)  & 0xFF;
          pixels[i * 4 + 2] = (rgba >> 16) & 0xFF;
          pixels[i * 4 + 3] = alphaBlock[i];
        }
        break;
      }

      case BcFormat::BC4: {
        // BC4: 8 bytes per block, single-channel (R8)
        uint8_t r0 = block[0];
        uint8_t r1 = block[1];
        uint8_t reds[8];

        if (r0 > r1) {
          reds[0] = r0;
          reds[1] = r1;
          for (int i = 0; i < 6; i++) {
            uint8_t bits = (block[2 + i / 2] >> (3 * (i % 2))) & 0x7;
            reds[i + 2] = static_cast<uint8_t>(((6 - bits) * r0 + bits * r1) / 6);
          }
        } else {
          reds[0] = r0;
          reds[1] = r1;
          for (int i = 0; i < 4; i++) {
            uint8_t bits = (block[2 + i / 2] >> (3 * (i % 2))) & 0x7;
            reds[i + 2] = static_cast<uint8_t>(((4 - bits) * r0 + bits * r1) / 4);
          }
          reds[6] = 0;
          reds[7] = 255;
        }

        uint64_t indices = 0;
        for (int i = 2; i < 8; i++)
          indices |= static_cast<uint64_t>(block[i]) << (8 * (i - 2));

        for (int i = 0; i < 16; i++) {
          uint32_t idx = (indices >> (3 * i)) & 7;
          uint8_t r = reds[idx];
          pixels[i * 4 + 0] = r;
          pixels[i * 4 + 1] = r;
          pixels[i * 4 + 2] = r;
          pixels[i * 4 + 3] = 255;
        }
        break;
      }

      case BcFormat::BC5: {
        // BC5: 16 bytes per block, two-channel (RG8)
        // Decode two BC4 blocks
        uint8_t blockR[16], blockG[16];

        // Red channel
        {
          uint8_t r0 = block[0], r1 = block[1];
          uint8_t reds[8];
          if (r0 > r1) {
            reds[0] = r0; reds[1] = r1;
            for (int i = 0; i < 6; i++) {
              uint8_t bits = (block[2 + i / 2] >> (3 * (i % 2))) & 0x7;
              reds[i + 2] = static_cast<uint8_t>(((6 - bits) * r0 + bits * r1) / 6);
            }
          } else {
            reds[0] = r0; reds[1] = r1;
            for (int i = 0; i < 4; i++) {
              uint8_t bits = (block[2 + i / 2] >> (3 * (i % 2))) & 0x7;
              reds[i + 2] = static_cast<uint8_t>(((4 - bits) * r0 + bits * r1) / 4);
            }
            reds[6] = 0; reds[7] = 255;
          }
          uint64_t indices = 0;
          for (int i = 2; i < 8; i++)
            indices |= static_cast<uint64_t>(block[i]) << (8 * (i - 2));
          for (int i = 0; i < 16; i++)
            blockR[i] = reds[(indices >> (3 * i)) & 7];
        }

        // Green channel
        {
          uint8_t r0 = block[8], r1 = block[9];
          uint8_t reds[8];
          if (r0 > r1) {
            reds[0] = r0; reds[1] = r1;
            for (int i = 0; i < 6; i++) {
              uint8_t bits = (block[10 + i / 2] >> (3 * (i % 2))) & 0x7;
              reds[i + 2] = static_cast<uint8_t>(((6 - bits) * r0 + bits * r1) / 6);
            }
          } else {
            reds[0] = r0; reds[1] = r1;
            for (int i = 0; i < 4; i++) {
              uint8_t bits = (block[10 + i / 2] >> (3 * (i % 2))) & 0x7;
              reds[i + 2] = static_cast<uint8_t>(((4 - bits) * r0 + bits * r1) / 4);
            }
            reds[6] = 0; reds[7] = 255;
          }
          uint64_t indices = 0;
          for (int i = 2; i < 8; i++)
            indices |= static_cast<uint64_t>(block[8 + i]) << (8 * (i - 2));
          for (int i = 0; i < 16; i++)
            blockG[i] = reds[(indices >> (3 * i)) & 7];
        }

        for (int i = 0; i < 16; i++) {
          pixels[i * 4 + 0] = blockR[i];
          pixels[i * 4 + 1] = blockG[i];
          pixels[i * 4 + 2] = 0;
          pixels[i * 4 + 3] = 255;
        }
        break;
      }

      case BcFormat::BC6H: {
        // BC6H: 16 bytes per block, half-float RGB
        // Simplified decode: extract endpoints and interpolate
        // This is a minimal implementation — full BC6H is very complex
        uint16_t halfRed[16], halfGreen[16], halfBlue[16];

        // For now, decode to a simple approximation
        // A proper BC6H decoder would need hundreds of lines
        // We'll output linear interpolation between endpoints
        uint64_t lo = 0, hi = 0;
        for (int i = 0; i < 8; i++) {
          lo |= static_cast<uint64_t>(block[i])     << (8 * i);
          hi |= static_cast<uint64_t>(block[8 + i]) << (8 * i);
        }

        // Extract 10-bit endpoints (simplified)
        uint32_t ep0_r = lo        & 0x3FF;
        uint32_t ep0_g = (lo >> 10) & 0x3FF;
        uint32_t ep0_b = (lo >> 20) & 0x3FF;
        uint32_t ep1_r = (lo >> 30) & 0x3FF;
        uint32_t ep1_g = (lo >> 40) & 0x3FF;
        uint32_t ep1_b = (lo >> 50) & 0x3FF;

        // Convert 10-bit to float, then to 8-bit for output
        auto toFloat8 = [](uint32_t v10) -> uint8_t {
          // Simple linear mapping [0,1023] -> [0,255]
          return static_cast<uint8_t>((v10 * 255) / 1023);
        };

        // Use color indices from remaining bits
        uint64_t indices = hi >> 16;
        for (int i = 0; i < 16; i++) {
          uint32_t idx = (indices >> (2 * i)) & 3;
          uint32_t t = idx;
          uint8_t r = static_cast<uint8_t>(
            ((4 - t) * toFloat8(ep0_r) + t * toFloat8(ep1_r)) / 4);
          uint8_t g = static_cast<uint8_t>(
            ((4 - t) * toFloat8(ep0_g) + t * toFloat8(ep1_g)) / 4);
          uint8_t b = static_cast<uint8_t>(
            ((4 - t) * toFloat8(ep0_b) + t * toFloat8(ep1_b)) / 4);
          halfRed[i]   = r;
          halfGreen[i] = g;
          halfBlue[i]  = b;
          pixels[i * 4 + 0] = r;
          pixels[i * 4 + 1] = g;
          pixels[i * 4 + 2] = b;
          pixels[i * 4 + 3] = 255;
        }
        break;
      }

      case BcFormat::BC7: {
        // BC7: 16 bytes per block, high-quality RGBA
        // Simplified decode — full BC7 has 8 modes with complex partitioning
        // For now, extract endpoints and do basic interpolation

        uint8_t mode = block[0] & 0x7;
        if (mode == 0 || mode == 1 || mode == 2 || mode == 3 ||
            mode == 4 || mode == 5 || mode == 6 || mode == 7) {
          // Extract two RGBA endpoints
          uint8_t ep0[4], ep1[4];

          if (mode == 6) {
            // Mode 6: 2 RGBA endpoints, no partitioning
            ep0[0] = (block[1] >> 3)       | ((block[2] & 0xC0) >> 2);
            ep0[1] = ((block[1] & 0x7) << 2) | ((block[2] & 0x38) >> 3);
            ep0[2] = ((block[2] & 0x7) << 2) | ((block[3] & 0xC0) >> 2);
            ep0[3] = ((block[3] & 0x3F) << 2) | ((block[4] & 0x80) >> 7);
            // Expand to 8-bit
            ep0[0] = (ep0[0] << 2) | (ep0[0] >> 4);
            ep0[1] = (ep0[1] << 2) | (ep0[1] >> 4);
            ep0[2] = (ep0[2] << 2) | (ep0[2] >> 4);
            ep0[3] = (ep0[3] << 2) | (ep0[3] >> 4);

            ep1[0] = (block[4] >> 2)       | ((block[5] & 0xF0));
            ep1[1] = ((block[4] & 0x3) << 4) | ((block[5] & 0x0F) << 0);
            ep1[2] = (block[6] >> 2)       | ((block[7] & 0xF0));
            ep1[3] = ((block[6] & 0x3) << 4) | ((block[7] & 0x0F) << 0);
            ep1[0] = (ep1[0] << 2) | (ep1[0] >> 4);
            ep1[1] = (ep1[1] << 2) | (ep1[1] >> 4);
            ep1[2] = (ep1[2] << 2) | (ep1[2] >> 4);
            ep1[3] = (ep1[3] << 2) | (ep1[3] >> 4);

            // 64-bit index starting at bit 8
            uint64_t indices = 0;
            for (int i = 1; i < 8; i++)
              indices |= static_cast<uint64_t>(block[i]) << (8 * i);
            indices >>= 8;

            for (int i = 0; i < 16; i++) {
              uint32_t idx = (indices >> (4 * i)) & 0xF;
              uint32_t t = idx;
              pixels[i * 4 + 0] = static_cast<uint8_t>(
                ((16 - t) * ep0[0] + t * ep1[0]) / 16);
              pixels[i * 4 + 1] = static_cast<uint8_t>(
                ((16 - t) * ep0[1] + t * ep1[1]) / 16);
              pixels[i * 4 + 2] = static_cast<uint8_t>(
                ((16 - t) * ep0[2] + t * ep1[2]) / 16);
              pixels[i * 4 + 3] = static_cast<uint8_t>(
                ((16 - t) * ep0[3] + t * ep1[3]) / 16);
            }
          } else {
            // Other modes: simplified — output a solid color from the first bytes
            // Full mode decode is 500+ lines; this is a placeholder
            uint8_t r = block[1];
            uint8_t g = block[2];
            uint8_t b = block[3];
            uint8_t a = (mode == 4 || mode == 5) ? block[4] : 255;
            for (int i = 0; i < 16; i++) {
              pixels[i * 4 + 0] = r;
              pixels[i * 4 + 1] = g;
              pixels[i * 4 + 2] = b;
              pixels[i * 4 + 3] = a;
            }
          }
        }
        break;
      }
    }
  }


  /**
   * \brief Computes the size of a BC compressed image in bytes
   *
   * \param [in] bcFormat  BC format
   * \param [in] width     Image width in pixels
   * \param [in] height    Image height in pixels
   * \returns Size in bytes of the compressed data
   */
  inline VkDeviceSize computeBcImageDataSize(
          BcFormat       bcFormat,
          uint32_t       width,
          uint32_t       height) {
    uint32_t blockWidth  = (width + 3) / 4;
    uint32_t blockHeight = (height + 3) / 4;
    uint32_t blockSize;

    switch (bcFormat) {
      case BcFormat::BC1:
      case BcFormat::BC4:
        blockSize = 8;
        break;
      case BcFormat::BC2:
      case BcFormat::BC3:
      case BcFormat::BC5:
      case BcFormat::BC6H:
      case BcFormat::BC7:
        blockSize = 16;
        break;
      default:
        blockSize = 16;
        break;
    }

    return static_cast<VkDeviceSize>(blockWidth) * blockHeight * blockSize;
  }


  /**
   * \brief Decodes a full BC compressed image to RGBA8
   *
   * \param [in]  bcFormat     BC format
   * \param [in]  srcData      Source BC data
   * \param [in]  width        Image width
   * \param [in]  height       Image height
   * \param [in]  srcRowPitch  Source row pitch in bytes (0 = tightly packed)
   * \param [out] dstData      Destination RGBA8 buffer
   * \param [in]  dstRowPitch  Destination row pitch in bytes (0 = tightly packed)
   */
  inline void decodeBcImage(
          BcFormat       bcFormat,
    const uint8_t*       srcData,
          uint32_t       width,
          uint32_t       height,
          VkDeviceSize   srcRowPitch,
          uint8_t*       dstData,
          VkDeviceSize   dstRowPitch) {
    uint32_t blockWidth  = (width + 3) / 4;
    uint32_t blockHeight = (height + 3) / 4;

    uint32_t srcBlockPitch;
    if (srcRowPitch > 0) {
      srcBlockPitch = static_cast<uint32_t>(srcRowPitch);
    } else {
      uint32_t blockSizeBytes = (bcFormat == BcFormat::BC1 || bcFormat == BcFormat::BC4) ? 8 : 16;
      srcBlockPitch = blockWidth * blockSizeBytes;
    }

    if (dstRowPitch == 0)
      dstRowPitch = width * 4;

    uint8_t blockPixels[64]; // 4x4 x 4 bytes

    for (uint32_t by = 0; by < blockHeight; by++) {
      for (uint32_t bx = 0; bx < blockWidth; bx++) {
        const uint8_t* block = srcData + static_cast<VkDeviceSize>(by) * srcBlockPitch + bx * ((bcFormat == BcFormat::BC1 || bcFormat == BcFormat::BC4) ? 8 : 16);

        decodeBcBlock(bcFormat, block, blockPixels);

        // Copy decoded pixels to destination, clamping to image bounds
        for (uint32_t py = 0; py < 4; py++) {
          for (uint32_t px = 0; px < 4; px++) {
            uint32_t srcX = bx * 4 + px;
            uint32_t srcY = by * 4 + py;
            if (srcX < width && srcY < height) {
              uint32_t srcIdx = (py * 4 + px) * 4;
              VkDeviceSize dstOffset = static_cast<VkDeviceSize>(srcY) * dstRowPitch + srcX * 4;
              dstData[dstOffset + 0] = blockPixels[srcIdx + 0];
              dstData[dstOffset + 1] = blockPixels[srcIdx + 1];
              dstData[dstOffset + 2] = blockPixels[srcIdx + 2];
              dstData[dstOffset + 3] = blockPixels[srcIdx + 3];
            }
          }
        }
      }
    }
  }

}
