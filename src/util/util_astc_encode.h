#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>
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
   * \brief Quantizes a weight to [0, maxVal]
   *
   * Maps [0.0, 1.0] → [0, maxVal]
   */
  inline uint32_t quantizeWeightTo(float w, uint32_t maxVal) {
    return std::min(maxVal, static_cast<uint32_t>(w * maxVal + 0.5f));
  }


  /**
   * \brief Quantizes a weight to N bits
   *
   * Maps [0.0, 1.0] → [0, (1<<n)-1]
   */
  inline uint32_t quantizeWeight(float w, uint32_t n) {
    return quantizeWeightTo(w, (1u << n) - 1u);
  }


  // ─── ASTC block header layout (Khronos GL_KHR_texture_compression_astc_ldr,
  //      Appendix C — cross-checked against Mesa texcompress_astc.cpp) ───────
  //
  // Table C.2.7 (2D Block Mode Layout), column order 10 → 0, row 1:
  //     D H  B   A   R0  0 0 R2 R1   →  Width = B+4, Height = A+2
  // with bit10=D, bit9=H, bits[8:7]=B, bits[6:5]=A, bit4=R0,
  // bits[3:2]=00, bit1=R2, bit0=R1.
  //
  // D=0 (no dual plane), H=0 (low precision range), B=0, A=2 gives a
  // 4x4 weight grid — exactly the block footprint, so the weight infill
  // (C.2.18) degenerates to an identity mapping, texel i ← grid[i].
  //
  // R = {R0, R2, R1} = 101, with H=0, selects Table C.2.6 row R=101:
  //     Weight Range 0..4, 1 quint per weight.
  // R2,R1 must not both be zero — that is what disambiguates row 1 from
  // the rows whose bits[1:0] are 00.
  //
  //     bit6 = 1 (A=2), bit4 = 1 (R0=1), bit0 = 1 (R1=1)
  //     → 0b0101_0001 = 0x51
  //
  // Note: 2-bit weights (R=100) are NOT expressible on a 4x4 grid — that
  // range only appears in rows whose grids are >= 6 in one axis, which
  // would exceed the 4x4 footprint and make the whole block decode to the
  // error colour.  Quint (5 levels) is the finest range that fits.
  constexpr uint32_t kAstcBlockMode4x4Quint = 0x51u;

  // Table C.2.4 (Single-partition block layout):
  //     bits[10:0]  block mode
  //     bits[12:11] Part — 00 = single partition (left zeroed by memset)
  //     bits[16:13] CEM  — Table C.2.8: 12 = "LDR RGBA, direct"
  // CEM 12 is class 3, so n = 8 endpoint values.
  constexpr uint32_t kAstcCemRgbaDirect   = 12u;
  constexpr uint32_t kAstcCemBitOffset    = 13u;
  constexpr uint32_t kAstcConfigBits      = 17u;

  // Endpoint data: 8 values x 8 bits, packed with ISE immediately above
  // the config bits (C.2.13, "growing upwards").
  constexpr uint32_t kAstcEndpointBitOffset = 17u;

  // Weights: 16 values, 1 quint each. ISE groups 3 quints into 7 bits
  // (C.2.13, group size = 7 + 3*n with n = 0 LSB bits); the final partial
  // group of a single quint costs 3 bits:
  //     5 * 7 + 3 = 38 bits.
  // The weight stream grows DOWNWARDS from the top of the block
  // (C.2.16): stream bit n == block bit (127 - n).
  constexpr uint32_t kAstcWeightBits     = 38u;
  constexpr uint32_t kAstcWeightBitOffset = 128u - kAstcWeightBits; // 90

  // config(17) + endpoints(64) + weights(38) = 119; 9 padding bits remain
  // at [89:81].  remaining_bits for endpoint range selection is
  // 128 - 17 - 38 = 73 >= 64, so the decoder derives the 0..255 (8-bit)
  // endpoint range — the values below are stored verbatim.

  static_assert(kAstcBlockMode4x4Quint <= 0x7FFu,
    "block mode field is 11 bits wide");
  static_assert(kAstcCemRgbaDirect <= 0xFu,
    "CEM field is 4 bits wide");
  static_assert(kAstcEndpointBitOffset == kAstcConfigBits,
    "endpoint data starts immediately after the config bits");
  static_assert(kAstcEndpointBitOffset + 64u <= kAstcWeightBitOffset,
    "64 endpoint bits must not collide with the weight stream");
  static_assert(kAstcWeightBitOffset + kAstcWeightBits == 128u,
    "the weight stream must end exactly at the top of the block");


  /**
   * \brief Decodes one packed quint group (C.2.13) into q[0..2] (0..4)
   *
   * Transcribed from the spec's decode procedure.  The spec's `T[6:5]` in
   * the final else-branch is a typo for `Q[6:5]`.
   */
  inline void decodeQuintGroup(uint32_t Q, uint32_t* q) {
    if (((Q >> 1) & 0x3u) == 0x3u && ((Q >> 5) & 0x3u) == 0x0u) {
      const uint32_t b0 = (Q >> 0) & 1u;
      const uint32_t b1 = ((Q >> 4) & 1u) & (~b0 & 1u);
      const uint32_t b2 = ((Q >> 3) & 1u) & (~b0 & 1u);
      q[0] = 4;
      q[1] = 4;
      q[2] = (b0 << 2) | (b1 << 1) | b2;
      return;
    }

    uint32_t C  = 0;
    uint32_t q2 = 0;
    if (((Q >> 1) & 0x3u) == 0x3u) {
      q2 = 4;
      const uint32_t hi  = (Q >> 3) & 0x3u;
      const uint32_t inv = ((Q >> 5) & 0x3u) ^ 0x3u;
      const uint32_t lo  = (Q >> 0) & 0x1u;
      C = (hi << 3) | (inv << 1) | lo;
    } else {
      q2 = (Q >> 5) & 0x3u;
      C  = Q & 0x1Fu;
    }

    if ((C & 0x7u) == 0x5u) {
      q[1] = 4;
      q[0] = (C >> 3) & 0x3u;
    } else {
      q[1] = (C >> 3) & 0x3u;
      q[0] = C & 0x7u;
    }
    q[2] = q2;
  }


  /**
   * \brief Forward map quint triple (0..4)^3 → packed 7-bit group
   *
   * The spec does not specify an encoding, so the table is derived by
   * running the decode procedure over all 128 packed values — the result
   * is therefore correct by construction relative to the decoder.
   * Indexed as q0 + 5*q1 + 25*q2; 0xFF marks "no packed form".
   */
  inline const uint8_t* quintEncodeTable() {
    static const uint8_t* table = []() -> const uint8_t* {
      static uint8_t t[125];
      for (uint32_t i = 0; i < 125; i++)
        t[i] = 0xFFu;
      for (uint32_t Q = 0; Q < 128; Q++) {
        uint32_t q[3] = { 0, 0, 0 };
        decodeQuintGroup(Q, q);
        if (q[0] > 4 || q[1] > 4 || q[2] > 4)
          continue;
        const uint32_t idx = q[0] + 5u * q[1] + 25u * q[2];
        if (t[idx] == 0xFFu)
          t[idx] = static_cast<uint8_t>(Q);
      }
      return t;
    }();
    return table;
  }


  /**
   * \brief Packs 16 quint weights into the 38-bit ISE weight stream
   *
   * Five full groups of three (7 bits each) plus a trailing partial group
   * of one quint (3 bits).  The partial group stores the value directly:
   * with Q[6:3] = 0 and Q[2:0] <= 4 the decode yields q0 = Q[2:0].
   *
   * \param [in] w  16 weights, each 0..4
   * \returns       the weight stream, bit 0 = first bit of group 0
   */
  inline uint64_t packWeightQuints(const uint32_t* w) {
    const uint8_t* enc = quintEncodeTable();
    uint64_t stream = 0;
    uint32_t bit = 0;

    for (uint32_t i = 0; i < 16; i += 3) {
      if (16u - i >= 3u) {
        const uint32_t idx = w[i] + 5u * w[i + 1] + 25u * w[i + 2];
        const uint32_t Q = enc[idx];
        assert(Q <= 127u && "quint triple has no packed form");
        stream |= uint64_t(Q) << bit;
        bit += 7;
      } else {
        assert(w[i] <= 4u);
        stream |= uint64_t(w[i]) << bit;
        bit += 3;
      }
    }

    assert(bit == kAstcWeightBits);
    return stream;
  }


  /**
   * \brief Bit-reverses the low n bits of v
   *
   * C.2.16 stores the weight stream downwards from block bit 127, so
   * stream bit n must land on block bit (127 - n).  Writing the reversed
   * stream at kAstcWeightBitOffset (90) achieves exactly that: block
   * bit 90 + j reads stream bit 37 - j.
   */
  inline uint64_t reverseBits(uint64_t v, uint32_t n) {
    uint64_t r = 0;
    for (uint32_t i = 0; i < n; i++)
      if ((v >> i) & 1ull)
        r |= 1ull << (n - 1u - i);
    return r;
  }


  /**
   * \brief Writes bits to a block buffer (LSB-first)
   *
   * \param [in/out] block  16-byte buffer
   * \param [in]     bitOffset  Starting bit position (0 = LSB of byte 0)
   * \param [in]     value      Value to write
   * \param [in]     numBits    Number of bits to write (1..64)
   */
  // Word-level implementation: read-modify-write on 64-bit words instead
  // of a per-bit loop. Requires little-endian host. Preserves OR-into-
  // existing-bits semantics (block starts zeroed, writes never overlap).
  // 64-bit wide so the 38-bit ASTC weight stream can be written in one
  // call without truncation.
  inline void writeBits(
          uint8_t*       block,
          uint32_t       bitOffset,
          uint64_t       value,
          uint32_t       numBits) {
    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
      "writeBits requires a little-endian host");
    if (numBits == 0 || numBits > 64)
      return;
    if (uint64_t(bitOffset) + numBits > 128)
      return;
    const uint64_t mask = (numBits >= 64)
      ? ~0ull
      : ((1ull << numBits) - 1ull);
    value &= mask;
    const uint32_t endBit = static_cast<uint32_t>(bitOffset + numBits);
    if (endBit <= 64) {
      uint64_t w = 0;
      std::memcpy(&w, block, 8);
      w |= value << bitOffset;
      std::memcpy(block, &w, 8);
    } else if (bitOffset >= 64) {
      uint64_t w = 0;
      std::memcpy(&w, block + 8, 8);
      w |= value << (bitOffset - 64);
      std::memcpy(block + 8, &w, 8);
    } else {
      // Spans the 64-bit boundary.
      const uint32_t loBits = 64 - bitOffset;
      const uint64_t loMask = ((loBits >= 64)
        ? ~0ull
        : ((1ull << loBits) - 1ull));
      uint64_t lo = 0, hi = 0;
      std::memcpy(&lo, block, 8);
      std::memcpy(&hi, block + 8, 8);
      lo |= (value & loMask) << bitOffset;
      hi |= value >> loBits;
      std::memcpy(block, &lo, 8);
      std::memcpy(block + 8, &hi, 8);
    }
  }


  /**
   * \brief Writes the fixed single-partition block header
   *
   *   bits[10:0]  = 0x51  (4x4 weight grid, weight range 0..4)
   *   bits[12:11] = 00    (Part: single partition)
   *   bits[16:13] = 12    (CEM: LDR RGBA, direct)
   */
  inline void writeBlockHeader(uint8_t* block) {
    writeBits(block, 0, kAstcBlockMode4x4Quint, 11);
    writeBits(block, kAstcCemBitOffset, kAstcCemRgbaDirect, 4);
  }


  /**
   * \brief Writes the 8 Mode-12 endpoint values as 8-bit raw data
   *
   * Mode 12 (astc_spec.txt) reads the values interleaved by channel:
   *     s0 = v0+v2+v4, s1 = v1+v3+v5
   *     s1 >= s0  →  e0 = (v0,v2,v4,v6), e1 = (v1,v3,v5,v7)
   * so v0..v7 must be stored as R0,R1,G0,G1,B0,B1,A0,A1 — NOT as the
   * two RGBA endpoints back to back.
   */
  inline void writeEndpoints(
          uint8_t* block,
          uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0,
          uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1) {
    writeBits(block, kAstcEndpointBitOffset +  0, r0, 8); // v0
    writeBits(block, kAstcEndpointBitOffset +  8, r1, 8); // v1
    writeBits(block, kAstcEndpointBitOffset + 16, g0, 8); // v2
    writeBits(block, kAstcEndpointBitOffset + 24, g1, 8); // v3
    writeBits(block, kAstcEndpointBitOffset + 32, b0, 8); // v4
    writeBits(block, kAstcEndpointBitOffset + 40, b1, 8); // v5
    writeBits(block, kAstcEndpointBitOffset + 48, a0, 8); // v6
    writeBits(block, kAstcEndpointBitOffset + 56, a1, 8); // v7
  }


  /**
   * \brief Encodes a 4x4 RGBA8 block to ASTC 4x4
   *
   * Single partition, no dual plane, CEM 12 (LDR RGBA direct) with 8-bit
   * endpoint values, and a 4x4 weight grid holding 16 quint weights
   * (5 levels).  See the kAstc* constants above for the derivations.
   *
   * Block layout (128 bits):
   *   Bits [0:10]   — Block mode 0x51 (4x4 grid, weight range 0..4)
   *   Bits [12:11]  — Part = 00 (single partition)
   *   Bits [16:13]  — CEM = 12 (LDR RGBA direct)
   *   Bits [17:80]  — 8 endpoint values x 8 bits, Mode-12 order:
   *                   v0=R0 v1=R1 v2=G0 v3=G1 v4=B0 v5=B1 v6=A0 v7=A1
   *   Bits [89:81]  — padding (9 bits)
   *   Bits [127:90] — weight stream (38 bits, stored downwards from 127)
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
#ifndef NDEBUG
      astcStats().uniformBlocks++;
#endif

      writeBlockHeader(block);
      writeEndpoints(block, minR, minG, minB, minA, minR, minG, minB, minA);

      // All 16 weights = 0 → texel = e0 = the block colour.
      // The weight stream region is already zero from memset.
      return;
    }

    // ─── Step 3: Compute endpoints ─────────────────────────────────
    // CEM 12 (LDR RGBA direct): endpoint 0 = per-channel min,
    // endpoint 1 = per-channel max.  min <= max on every channel, so
    // s1 = R1+G1+B1 >= s0 = R0+G0+B0 and Mode 12 never takes its
    // blue-contract branch (see astc_spec.txt Mode 12).

#ifndef NDEBUG
    astcStats().directBlocks++;
#endif

    writeBlockHeader(block);
    writeEndpoints(block, minR, minG, minB, minA, maxR, maxG, maxB, maxA);

    // ─── Step 4: Compute quint weights ─────────────────────────────
    // For each pixel, compute interpolation factor t ∈ [0, 1] by
    // projecting onto the min→max axis in RGBA space, then quantize to
    // the block's 5-level (0..4) weight range.  Table C.2.16 maps a
    // stored quint w to {0, 16, 32, 48, 64}/64, i.e. exactly linear,
    // so rounding t to the nearest quarter is the correct quantizer.
    float rangeR = static_cast<float>(maxR) - minR;
    float rangeG = static_cast<float>(maxG) - minG;
    float rangeB = static_cast<float>(maxB) - minB;
    float rangeA = static_cast<float>(maxA) - minA;
    float rangeSq = rangeR * rangeR + rangeG * rangeG
                  + rangeB * rangeB + rangeA * rangeA;

    uint32_t weights[16];

    if (rangeSq < 1.0f) {
      // All pixels map to the same colour (cannot happen after the
      // uniform check, but handle gracefully).
      for (int i = 0; i < 16; i++)
        weights[i] = 0;
    } else {
      // t = dot(d, range)/|range|², clamped to [0,1].  Off-axis pixels
      // no longer overshoot toward max, and it costs one reciprocal per
      // block instead of a sqrt per pixel.
      float invRangeSq = 1.0f / rangeSq;

      for (int i = 0; i < 16; i++) {
        float dr = static_cast<float>(pixels[i * 4 + 0]) - minR;
        float dg = static_cast<float>(pixels[i * 4 + 1]) - minG;
        float db = static_cast<float>(pixels[i * 4 + 2]) - minB;
        float da = static_cast<float>(pixels[i * 4 + 3]) - minA;
        float t = (dr * rangeR + dg * rangeG + db * rangeB + da * rangeA) * invRangeSq;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

        weights[i] = quantizeWeightTo(t, 4u);
      }
    }

    // ─── Step 5: Pack weights into the block ───────────────────────
    // ISE-pack the 16 quints into 38 bits, then store that stream
    // downwards from block bit 127 as required by C.2.16.
    const uint64_t stream = packWeightQuints(weights);
    writeBits(block, kAstcWeightBitOffset,
              reverseBits(stream, kAstcWeightBits), kAstcWeightBits);
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
