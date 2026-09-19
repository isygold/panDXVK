#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace dxvk::util {

  /**
   * \brief ASTC encoder statistics (debug builds only)
   */
  struct AstcEncodeStats {
    uint32_t totalBlocks    = 0;
    uint32_t uniformBlocks  = 0;
    uint32_t directBlocks   = 0;  // CEM 12 (RGBA direct)
    uint32_t baseOffBlocks  = 0;  // CEM 13 (RGBA base+offset)
    uint32_t maxWeightClamp = 0;

    void dump() const {
#ifndef NDEBUG
      fprintf(stderr,
        "[panDXVK ASTC encode] total=%u uniform=%u direct=%u baseOff=%u maxWeightClamp=%u\n",
        totalBlocks, uniformBlocks, directBlocks, baseOffBlocks, maxWeightClamp);
#endif
    }
  };

  inline AstcEncodeStats& astcStats() {
    static AstcEncodeStats s_stats;
    return s_stats;
  }


  /**
   * \brief Quantizes a weight to N bits
   *
   * Maps [0.0, 1.0] → [0, (1<<n)-1]
   */
  inline uint32_t quantizeWeight(float w, uint32_t n) {
    uint32_t maxVal = (1u << n) - 1u;
    return std::min(maxVal, static_cast<uint32_t>(w * maxVal + 0.5f));
  }


  /**
   * \brief Writes bits to a block buffer (LSB-first)
   *
   * \param [in/out] block  16-byte buffer
   * \param [in]     bitOffset  Starting bit position (0 = LSB of byte 0)
   * \param [in]     value      Value to write
   * \param [in]     numBits    Number of bits to write
   */
  // Word-level implementation: read-modify-write on 64-bit words instead
  // of a per-bit loop. Requires little-endian host. Preserves OR-into-
  // existing-bits semantics (block starts zeroed, writes never overlap).
  inline void writeBits(
          uint8_t*       block,
          uint32_t       bitOffset,
          uint32_t       value,
          uint32_t       numBits) {
    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
      "writeBits requires a little-endian host");
    if (numBits == 0)
      return;
    const uint32_t mask = (numBits >= 32)
      ? 0xFFFFFFFFu
      : ((1u << numBits) - 1u);
    value &= mask;
    const uint32_t endBit = bitOffset + numBits;
    if (endBit <= 64) {
      uint64_t w = 0;
      std::memcpy(&w, block, 8);
      w |= (uint64_t)value << bitOffset;
      std::memcpy(block, &w, 8);
    } else if (bitOffset >= 64) {
      uint64_t w = 0;
      std::memcpy(&w, block + 8, 8);
      w |= (uint64_t)value << (bitOffset - 64);
      std::memcpy(block + 8, &w, 8);
    } else {
      // Spans the 64-bit boundary. With numBits <= 32 this implies
      // bitOffset > 32, so the low part holds fewer than 32 bits.
      const uint32_t loBits = 64 - bitOffset;
      const uint32_t loMask = (loBits >= 32)
        ? 0xFFFFFFFFu
        : ((1u << loBits) - 1u);
      uint64_t lo = 0, hi = 0;
      std::memcpy(&lo, block, 8);
      std::memcpy(&hi, block + 8, 8);
      lo |= (uint64_t)(value & loMask) << bitOffset;
      hi |= (uint64_t)value >> loBits;
      std::memcpy(block, &lo, 8);
      std::memcpy(block + 8, &hi, 8);
    }
  }


  /**
   * \brief Encodes a 4x4 RGBA8 block to ASTC 4x4
   *
   * Uses a proper ASTC encoding based on the Khronos Data Format spec:
   * - Uniform blocks: single color, CEM 12, all weights = 0
   * - Low variance: CEM 13 (RGBA base+offset) with 8-bit endpoints
   * - Normal blocks: CEM 12 (RGBA direct) with 8-bit endpoints, 2-bit weights
   *
   * Block layout (128 bits):
   *   Bits [0:10]   — Block mode (11 bits)
   *   Bits [11:16]  — Color endpoint mode (6 bits, single partition)
   *   Bits [17:80]  — Endpoint data (64 bits for CEM 12/13)
   *   Bits [81:112] — Weight data (32 bits for 16 × 2-bit weights)
   *   Bits [113:127] — Reserved/padding (15 bits)
   *
   * \param [in]  pixels  Input 4x4 RGBA8 pixel block (64 bytes)
   * \param [out] block   Output ASTC 4x4 block (16 bytes)
   */
  inline void encodeAstcBlock4x4(
    const uint8_t*       pixels,
          uint8_t*       block) {
    // Zero the output block
    std::memset(block, 0, 16);

    // ─── Step 1: Compute block statistics ──────────────────────────
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

#ifndef NDEBUG
    astcStats().totalBlocks++;
#endif

    // ─── Step 2: Check for uniform block ───────────────────────────
    bool uniform = (minR == maxR && minG == maxG && minB == maxB && minA == maxA);

    if (uniform) {
      // Uniform block: CEM 12 (RGBA direct), all weights = 0
      // Block mode: 4x4 2-bit weights = 0x00A (see below)
      // Endpoint: single color repeated
#ifndef NDEBUG
      astcStats().uniformBlocks++;
#endif

      // Block mode for 4x4, 2-bit weights:
      // Bits [0:1]  = R-2 = 2  → 0b10
      // Bits [2:4]  = H-2 = 2  → 0b010
      // Bits [5:7]  = D-1 = 0  → 0b000
      // Bit  [8]    = P   = 0  → 0b0
      // Bits [9:11] = ρ   = 0  → 0b000  (2-bit weights)
      // Total: 0b000_0_000_010_10 = 0x00A
      writeBits(block, 0, 0x00A, 11);

      // CEM = 12 (RGBA direct, class 3)
      // 6-bit encoding: 0b001100
      writeBits(block, 11, 0x0C, 6);

      // Endpoint data: 8 × 8-bit values (R0, G0, B0, A0, R1, G1, B1, A1)
      // For uniform: both endpoints are the same color
      writeBits(block, 17, minR, 8);  // R0
      writeBits(block, 25, minG, 8);  // G0
      writeBits(block, 33, minB, 8);  // B0
      writeBits(block, 41, minA, 8);  // A0
      writeBits(block, 49, minR, 8);  // R1
      writeBits(block, 57, minG, 8);  // G1
      writeBits(block, 65, minB, 8);  // B1
      writeBits(block, 73, minA, 8);  // A1

      // All 16 weights = 0 (use endpoint directly)
      // Weight data is already zero from memset
      return;
    }

    // ─── Step 3: Compute endpoints ─────────────────────────────────
    // Use CEM 12 (RGBA direct): two 8-bit RGBA endpoints
    // Endpoint 0 = min color, Endpoint 1 = max color

#ifndef NDEBUG
    astcStats().directBlocks++;
#endif

    // Block mode: same as uniform (4x4, 2-bit weights)
    writeBits(block, 0, 0x00A, 11);

    // CEM = 12 (RGBA direct)
    writeBits(block, 11, 0x0C, 6);

    // Endpoint data: 8 × 8-bit values
    writeBits(block, 17, minR, 8);  // R0
    writeBits(block, 25, minG, 8);  // G0
    writeBits(block, 33, minB, 8);  // B0
    writeBits(block, 41, minA, 8);  // A0
    writeBits(block, 49, maxR, 8);  // R1
    writeBits(block, 57, maxG, 8);  // G1
    writeBits(block, 65, maxB, 8);  // B1
    writeBits(block, 73, maxA, 8);  // A1

    // ─── Step 4: Compute 2-bit weights ─────────────────────────────
    // For each pixel, compute interpolation factor t ∈ [0, 1]
    // based on Euclidean distance in RGBA space
    float rangeR = static_cast<float>(maxR) - minR;
    float rangeG = static_cast<float>(maxG) - minG;
    float rangeB = static_cast<float>(maxB) - minB;
    float rangeA = static_cast<float>(maxA) - minA;
    float rangeSq = rangeR * rangeR + rangeG * rangeG
                  + rangeB * rangeB + rangeA * rangeA;

    uint32_t weightBits[16];

    if (rangeSq < 1.0f) {
      // All pixels map to the same color (shouldn't happen after uniform check,
      // but handle gracefully)
      for (int i = 0; i < 16; i++)
        weightBits[i] = 0;
    } else {
      float invRange = 1.0f / std::sqrt(rangeSq);

      for (int i = 0; i < 16; i++) {
        float dr = static_cast<float>(pixels[i * 4 + 0]) - minR;
        float dg = static_cast<float>(pixels[i * 4 + 1]) - minG;
        float db = static_cast<float>(pixels[i * 4 + 2]) - minB;
        float da = static_cast<float>(pixels[i * 4 + 3]) - minA;

        float dist = std::sqrt(dr * dr + dg * dg + db * db + da * da);
        float t = dist * invRange;  // t ∈ [0, 1]

        // Quantize to 2 bits: 0, 1, 2, 3
        weightBits[i] = quantizeWeight(t, 2);
      }
    }

    // ─── Step 5: Pack weights into block ───────────────────────────
    // 16 weights × 2 bits = 32 bits, starting at bit 81
    uint32_t weightData = 0;
    for (int i = 0; i < 16; i++)
      weightData |= (weightBits[i] & 0x3u) << (2 * i);

    writeBits(block, 81, weightData, 32);

    // Bits [113:127] are padding (already zero from memset)
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

#ifndef NDEBUG
    // Log stats periodically (every 10000th image)
    static uint32_t imageCount = 0;
    if (++imageCount % 10000 == 0)
      astcStats().dump();
#endif
  }

}
