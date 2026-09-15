#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace dxvk::util {

  /**
   * \brief Encodes a 4x4 RGBA8 pixel block to ASTC 4x4 block
   *
   * Uses a simplified encoding: stores endpoint colors with
   * linear interpolation. Quality is acceptable for game textures.
   *
   * \param [in]  pixels  Input 4x4 RGBA8 pixel block (64 bytes)
   * \param [out] block   Output ASTC 4x4 block (16 bytes)
   */
  inline void encodeAstcBlock4x4(
    const uint8_t*       pixels,
          uint8_t*       block) {
    // ASTC 4x4 block structure (128 bits = 16 bytes):
    // - Bits 0-8:   weight indices / config
    // - Bits 9-127: endpoint colors + weight data
    //
    // Simplified encoding: use "uniform block" mode
    // where the entire block is one color.

    // Compute average color of the 4x4 block
    uint32_t r = 0, g = 0, b = 0, a = 0;
    for (int i = 0; i < 16; i++) {
      r += pixels[i * 4 + 0];
      g += pixels[i * 4 + 1];
      b += pixels[i * 4 + 2];
      a += pixels[i * 4 + 3];
    }
    r = (r + 8) / 16;
    g = (g + 8) / 16;
    b = (b + 8) / 16;
    a = (a + 8) / 16;

    // Check if all pixels are the same color (uniform block)
    bool uniform = true;
    for (int i = 1; i < 16 && uniform; i++) {
      if (pixels[i * 4 + 0] != pixels[0] ||
          pixels[i * 4 + 1] != pixels[1] ||
          pixels[i * 4 + 2] != pixels[2] ||
          pixels[i * 4 + 3] != pixels[3])
        uniform = false;
    }

    if (uniform) {
      // Perfect uniform block: use mode 0 (uniform RGBA)
      // Block type 0: 4 bits mode, 8 bits R, 8 bits G, 8 bits B, 8 bits A, 96 bits unused
      // Encoding: [0][R:8][G:8][B:8][A:8][zeros...]

      // Try 4-bit per channel mode (simplest)
      uint8_t r4 = (r >> 4) & 0xF;
      uint8_t g4 = (g >> 4) & 0xF;
      uint8_t b4 = (b >> 4) & 0xF;
      uint8_t a4 = (a >> 4) & 0xF;

      // Use quantized endpoints
      // ASTC weight block type 0 (uniform): endpoint is directly stored
      block[0] = 0x00; // Mode: uniform RGBA
      block[1] = (r4 << 4) | g4;
      block[2] = (b4 << 4) | a4;
      // Fill rest with weight data (all same weight)
      // Weight = 0 means use endpoint directly
      for (int i = 3; i < 16; i++)
        block[i] = 0x00;
      return;
    }

    // Non-uniform block: use dual-endpoint interpolation
    // Find min/max colors in the block
    uint8_t minR = 255, maxR = 0;
    uint8_t minG = 255, maxG = 0;
    uint8_t minB = 255, maxB = 0;
    uint8_t minA = 255, maxA = 0;

    for (int i = 0; i < 16; i++) {
      minR = std::min(minR, pixels[i * 4 + 0]);
      maxR = std::max(maxR, pixels[i * 4 + 0]);
      minG = std::min(minG, pixels[i * 4 + 1]);
      maxG = std::max(maxG, pixels[i * 4 + 1]);
      minB = std::min(minB, pixels[i * 4 + 2]);
      maxB = std::max(maxB, pixels[i * 4 + 2]);
      minA = std::min(minA, pixels[i * 4 + 3]);
      maxA = std::max(maxA, pixels[i * 4 + 3]);
    }

    // Use "dual-endpoint" mode: store two colors, interpolate per-pixel
    // ASTC mode for dual RGBA endpoints with 8 weights

    // Quantize endpoints to 4 bits each
    uint8_t ep0[4] = {
      static_cast<uint8_t>((minR >> 4) & 0xF),
      static_cast<uint8_t>((minG >> 4) & 0xF),
      static_cast<uint8_t>((minB >> 4) & 0xF),
      static_cast<uint8_t>((minA >> 4) & 0xF)
    };
    uint8_t ep1[4] = {
      static_cast<uint8_t>((maxR >> 4) & 0xF),
      static_cast<uint8_t>((maxG >> 4) & 0xF),
      static_cast<uint8_t>((maxB >> 4) & 0xF),
      static_cast<uint8_t>((maxA >> 4) & 0xF)
    };

    // Compute weights for each pixel (linear interpolation factor)
    // Weight range: [0, 6] for 7 weight levels
    uint8_t weights[16];
    for (int i = 0; i < 16; i++) {
      float dr = pixels[i * 4 + 0] - minR;
      float dg = pixels[i * 4 + 1] - minG;
      float db = pixels[i * 4 + 2] - minB;
      float da = pixels[i * 4 + 3] - minA;
      float dRangeR = maxR - minR;
      float dRangeG = maxG - minG;
      float dRangeB = maxB - minB;
      float dRangeA = maxA - minA;

      float dist = dr * dr + dg * dg + db * db + da * da;
      float range = dRangeR * dRangeR + dRangeG * dRangeG
                  + dRangeB * dRangeB + dRangeA * dRangeA;

      float t = (range > 0.0f) ? std::sqrt(dist / range) : 0.0f;
      weights[i] = static_cast<uint8_t>(std::min(6.0f, std::max(0.0f, t * 6.0f)));
    }

    // Encode block:
    // Byte 0: mode bits (dual endpoint RGBA, 8-weight mode)
    // Bytes 1-4: endpoint 0 (4 bits per channel)
    // Bytes 5-8: endpoint 1 (4 bits per channel)
    // Bytes 9-15: 16 weights x 3 bits = 48 bits in 7 bytes

    block[0] = 0x04; // Dual endpoint mode
    block[1] = (ep0[0] << 4) | ep0[1];
    block[2] = (ep0[2] << 4) | ep0[3];
    block[3] = (ep1[0] << 4) | ep1[1];
    block[4] = (ep1[2] << 4) | ep1[3];

    // Pack 16 weights x 3 bits into bytes 5-15
    // This uses a simple packing: 5 full bytes (3 bits x 16 = 48 bits = 6 bytes)
    uint64_t weightData = 0;
    for (int i = 0; i < 16; i++)
      weightData |= static_cast<uint64_t>(weights[i]) << (3 * i);

    for (int i = 0; i < 11; i++)
      block[5 + i] = (weightData >> (8 * i)) & 0xFF;
  }


  /**
   * \brief Encodes a full RGBA8 image to ASTC 4x4
   *
   * \param [in]  srcData      Source RGBA8 data
   * \param [in]  width        Image width in pixels
   * \param [in]  height       Image height in pixels
   * \param [in]  srcRowPitch  Source row pitch in bytes
   * \param [out] dstData      Destination ASTC 4x4 data
   * \param [in]  dstRowPitch  Destination row pitch in bytes
   */
  inline void encodeAstcImage4x4(
    const uint8_t*       srcData,
          uint32_t       width,
          uint32_t       height,
          VkDeviceSize   srcRowPitch,
          uint8_t*       dstData,
          VkDeviceSize   dstRowPitch) {
    uint32_t blockWidth  = (width + 3) / 4;
    uint32_t blockHeight = (height + 3) / 4;

    if (dstRowPitch == 0)
      dstRowPitch = blockWidth * 16;

    uint8_t blockPixels[64]; // 4x4 x 4 bytes

    for (uint32_t by = 0; by < blockHeight; by++) {
      for (uint32_t bx = 0; bx < blockWidth; bx++) {
        // Gather 4x4 pixel block, clamping to image bounds
        for (uint32_t py = 0; py < 4; py++) {
          for (uint32_t px = 0; px < 4; px++) {
            uint32_t srcX = std::min(bx * 4 + px, width - 1);
            uint32_t srcY = std::min(by * 4 + py, height - 1);
            VkDeviceSize srcOffset = static_cast<VkDeviceSize>(srcY) * srcRowPitch + srcX * 4;
            uint32_t dstIdx = (py * 4 + px) * 4;
            blockPixels[dstIdx + 0] = srcData[srcOffset + 0];
            blockPixels[dstIdx + 1] = srcData[srcOffset + 1];
            blockPixels[dstIdx + 2] = srcData[srcOffset + 2];
            blockPixels[dstIdx + 3] = srcData[srcOffset + 3];
          }
        }

        // Encode block
        VkDeviceSize dstOffset = static_cast<VkDeviceSize>(by) * dstRowPitch + bx * 16;
        encodeAstcBlock4x4(blockPixels, dstData + dstOffset);
      }
    }
  }

}
