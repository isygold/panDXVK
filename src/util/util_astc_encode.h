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


  // ─── ASTC block header layout (Khronos GL_KHR_texture_compression_astc_ldr,
  //      Appendix C — cross-checked against Mesa texcompress_astc.cpp) ───────
  //
  // Table C.2.7 (2D Block Mode Layout), column order 10 → 0, row 1:
  //     D H  B   A   R0  0 0 R2 R1   →  Width = B+4, Height = A+2
  // with bit10=D, bit9=H, bits[8:7]=B, bits[6:5]=A, bit4=R0,
  // bits[3:2]=00, bit1=R2, bit0=R1.  Table C.2.6 reads the 3-bit R as
  // {R0, R2, R1} with R0 most significant.
  //
  // D=0 (no dual plane), H=1 (high precision range), B=0 → Width = 4,
  // A=2 → Height = 4 gives a 4x4 weight grid — exactly the block
  // footprint, so the weight infill (C.2.18) degenerates to an identity
  // mapping, texel i ← grid[i].
  //
  // R = {R0, R2, R1} = 010 with H=1 selects Table C.2.6 row R=010:
  //     Weight Range 0..9 — 1 quint + 1 LSB per weight, 10 levels.
  //
  //     bit9 = 1 (H=1), bit6 = 1 (A=2), bit1 = 1 (R2=1)
  //     → 0b010_0100_0010 = 0x242
  //
  // R2 and R1 cannot both be zero — that is what disambiguates row 1 from
  // rows 6-10, whose bits[1:0] are 00.
  //
  // 10 weight levels replace the former 5 (block mode 0x51, 1 quint per
  // weight).  The cost is one endpoint bit per value: remaining_bits =
  // 128 - 17 - 54 = 57, so C.2.13 selects the 0..127 (7-bit) endpoint
  // range, 8 x 7 = 56 bits, leaving exactly one padding bit.  On real
  // content this is worth +2.09 dB (measured, rtr_astc gate --rt).
  constexpr uint32_t kAstcBlockMode4x4 = 0x242u;

  // Table C.2.4 (Single-partition block layout):
  //     bits[10:0]  block mode
  //     bits[12:11] Part — 00 = single partition (left zeroed by memset)
  //     bits[16:13] CEM  — Table C.2.8: 12 = "LDR RGBA, direct"
  // CEM 12 is class 3, so n = 8 endpoint values.
  constexpr uint32_t kAstcCemRgbaDirect   = 12u;
  constexpr uint32_t kAstcCemBitOffset    = 13u;
  constexpr uint32_t kAstcConfigBits      = 17u;

  // Endpoint data: 8 values x 7 bits, packed with ISE immediately above
  // the config bits (C.2.13, "growing upwards").  Bit-only ranges pack
  // value i at kAstcEndpointBitOffset + i * kAstcEndpointValueBits.
  // C.2.13: "For bit-only representations, this is simple bit
  // replication from the most significant bit" back to 0..255.
  constexpr uint32_t kAstcEndpointBitOffset  = 17u;
  constexpr uint32_t kAstcEndpointValueBits  = 7u;
  constexpr uint32_t kAstcEndpointValues     = 8u;
  constexpr uint32_t kAstcEndpointBits       =
    kAstcEndpointValues * kAstcEndpointValueBits; // 56

  // Weights: 16 values, 1 quint + 1 LSB each (range 0..9).  C.2.12 packs
  // 3 values into a (7 + 3*n)-bit group with n = 1 LSB bit → 10 bits:
  //     m0 | Q[2:0] | m1 | Q[4:3] | m2 | Q[6:5]
  // Five full groups plus a trailing partial group of one value
  // (1 LSB + ceil(1*7/3) = 4 bits) gives 5 * 10 + 4 = 54 bits, matching
  // ceil(16*7/3) + 16*1 = 38 + 16.  The weight stream grows DOWNWARDS
  // from the top of the block (C.2.16): stream bit n == block bit
  // (127 - n).
  constexpr uint32_t kAstcWeightLevels    = 10u;
  constexpr uint32_t kAstcWeightBits      = 54u;
  constexpr uint32_t kAstcWeightBitOffset = 128u - kAstcWeightBits; // 74

  // config(17) + endpoints(56) + padding(1) + weights(54) = 128.
  // remaining_bits for endpoint range selection is 128 - 17 - 54 = 57,
  // and 8 x 7 = 56 <= 57, so the decoder derives the 0..127 (7-bit)
  // endpoint range — the values below are stored as 7-bit quantities.

  static_assert(kAstcBlockMode4x4 <= 0x7FFu,
    "block mode field is 11 bits wide");
  static_assert(kAstcCemRgbaDirect <= 0xFu,
    "CEM field is 4 bits wide");
  static_assert(kAstcEndpointBitOffset == kAstcConfigBits,
    "endpoint data starts immediately after the config bits");
  static_assert(kAstcEndpointBitOffset + kAstcEndpointBits
                  == kAstcWeightBitOffset - 1u,
    "exactly one padding bit sits between the endpoints and the weights");
  static_assert(kAstcWeightBitOffset + kAstcWeightBits == 128u,
    "the weight stream must end exactly at the top of the block");
  static_assert(128u - kAstcConfigBits - kAstcWeightBits
                  >= kAstcEndpointBits,
    "remaining_bits must be able to hold the endpoint stream");


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
   * \brief Unquantizes a weight index in range 0..9 (Table C.2.17)
   *
   * Column "0..9" of Table C.2.17 carries T Q B = 1 1 1 with
   * B = 000000000, C = 28 for the weight table and D = the quint value;
   * A is 127 when the LSB is set and 0 when it is not.  C.2.17 then
   * processes them as:
   *
   *     T = D * C + B;  T ^= A;  T = (A & 0x20) | (T >> 2);
   *     if (T > 32) T += 1;
   *
   * yielding {0, 7, 14, 21, 28, 36, 43, 50, 57, 64} — evenly spaced across
   * the 0..64 weight scale.
   *
   * The index -> value map is NOT monotonic (index 1 -> 64, index 2 -> 7),
   * exactly the scrambling C.2.13 warns about for endpoints.  An encoder
   * must therefore pick the index whose *unquantized value* is nearest
   * its target; scaling the index directly lands on the wrong weight.
   *
   * \param [in] idx  weight index, 0..9
   * \returns         unquantized weight on the 0..64 scale
   */
  inline uint32_t unquantWeightQuint1(uint32_t idx) {
    const uint32_t A = (idx & 1u) ? 127u : 0u;
    const uint32_t B = 0u;
    const uint32_t C = 28u;
    const uint32_t D = (idx >> 1) & 7u;

    uint32_t T = D * C + B;
    T = T ^ A;
    T = (A & 0x20u) | (T >> 2);
    if (T > 32u)
      T += 1u;
    return T;
  }


  /**
   * \brief Quantizes t in [0,1] to the weight index nearest it
   *
   * Targets the 0..64 scale directly and compares *unquantized* values,
   * because unquantWeightQuint1() is not monotonic in the index.
   * Ties resolve to the lowest index, which is what the independently
   * written candidate encoder in rtr_astc/gate.cpp does, so the two
   * produce identical blocks.
   *
   * \param [in] t  interpolation factor along the endpoint pair, [0,1]
   * \returns       weight index, 0..9
   */
  inline uint32_t quantizeWeightQuint1(float t) {
    if (!(t > 0.0f))
      return 0u;   // t <= 0, or NaN — treat as the block minimum
    if (t > 1.0f)
      t = 1.0f;

    const uint32_t target = static_cast<uint32_t>(t * 64.0f + 0.5f);
    uint32_t best = 0;
    uint32_t bestErr = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < kAstcWeightLevels; i++) {
      const uint32_t u = unquantWeightQuint1(i);
      const uint32_t e = (u > target) ? (u - target) : (target - u);
      if (e < bestErr) {
        bestErr = e;
        best = i;
      }
    }
    return best;
  }


  /**
   * \brief Packs 16 quint+1LSB weights into the 54-bit ISE weight stream
   *
   * C.2.12 groups 3 values into a (7 + 3*n)-bit block; with n = 1 LSB bit
   * that is 10 bits, laid out
   *     m0 | Q[2:0] | m1 | Q[4:3] | m2 | Q[6:5]
   * where Q[6:0] is the packed triple of quints from quintEncodeTable().
   * Sixteen weights are five full groups (50 bits) plus a trailing
   * partial group holding the last value in 1 + ceil(1*7/3) = 4 bits:
   * the LSB first, then the raw quint — the packed form of that value
   * when Q[6:3] are zero.  5 * 10 + 4 = 54 = kAstcWeightBits.
   *
   * \param [in] w  16 weights, each 0..9
   * \returns       the weight stream, bit 0 = first bit of group 0
   */
  inline uint64_t packWeightQuint1Bits(const uint32_t* w) {
    const uint8_t* enc = quintEncodeTable();
    uint64_t stream = 0;
    uint32_t bit = 0;
    uint32_t i = 0;

    for (; i + 3u <= 16u; i += 3u) {
      const uint32_t idx = (w[i] >> 1) + 5u * (w[i + 1] >> 1)
                         + 25u * (w[i + 2] >> 1);
      const uint32_t Q = enc[idx];
      assert(Q <= 127u && "quint triple has no packed form");
      stream |= uint64_t(w[i]      & 1u) << (bit + 0);
      stream |= uint64_t(Q         & 7u) << (bit + 1);
      stream |= uint64_t(w[i + 1]  & 1u) << (bit + 4);
      stream |= uint64_t((Q >> 3)  & 3u) << (bit + 5);
      stream |= uint64_t(w[i + 2]  & 1u) << (bit + 7);
      stream |= uint64_t((Q >> 5)  & 3u) << (bit + 8);
      bit += 10u;
    }

    // 16 = 5 * 3 + 1: exactly one value remains.  A two-value remainder
    // cannot arise for a 4x4 grid, so it is rejected rather than guessed.
    assert(16u - i == 1u && "16 weights must leave a single trailing value");
    assert(w[i] <= 9u && "weight index out of range");
    stream |= uint64_t(w[i] & 1u) << (bit + 0);
    stream |= uint64_t(w[i] >> 1) << (bit + 1);
    bit += 4u;
    i += 1u;

    assert(bit == kAstcWeightBits);
    return stream;
  }


  /**
   * \brief Bit-reverses the low n bits of v
   *
   * C.2.16 stores the weight stream downwards from block bit 127, so
   * stream bit n must land on block bit (127 - n).  Writing the reversed
   * stream at kAstcWeightBitOffset (74) achieves exactly that: block
   * bit 74 + j reads stream bit 53 - j.
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
  // 64-bit wide so the 54-bit ASTC weight stream can be written in one
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
   *   bits[10:0]  = 0x242  (4x4 weight grid, weight range 0..9)
   *   bits[12:11] = 00     (Part: single partition)
   *   bits[16:13] = 12     (CEM: LDR RGBA, direct)
   */
  inline void writeBlockHeader(uint8_t* block) {
    writeBits(block, 0, kAstcBlockMode4x4, 11);
    writeBits(block, kAstcCemBitOffset, kAstcCemRgbaDirect, 4);
  }


  /**
   * \brief Writes the 8 Mode-12 endpoint values as 7-bit raw data
   *
   * Mode 12 (astc_spec.txt) reads the values interleaved by channel:
   *     s0 = v0+v2+v4, s1 = v1+v3+v5
   *     s1 >= s0  →  e0 = (v0,v2,v4,v6), e1 = (v1,v3,v5,v7)
   * so v0..v7 must be stored as R0,R1,G0,G1,B0,B1,A0,A1 — NOT as the
   * two RGBA endpoints back to back.
   *
   * The endpoint range is 0..127 (7 bits) because remaining_bits = 57.
   * C.2.13 restores the full range by simple bit replication from the
   * MSB, so the encoder only has to drop the low bit (stored = value
   * >> 1).  The round trip is then within one LSB for every input.
   */
  inline void writeEndpoints(
          uint8_t* block,
          uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0,
          uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1) {
    const uint32_t s = kAstcEndpointValueBits;
    writeBits(block, kAstcEndpointBitOffset + 0 * s, r0 >> 1, s); // v0
    writeBits(block, kAstcEndpointBitOffset + 1 * s, r1 >> 1, s); // v1
    writeBits(block, kAstcEndpointBitOffset + 2 * s, g0 >> 1, s); // v2
    writeBits(block, kAstcEndpointBitOffset + 3 * s, g1 >> 1, s); // v3
    writeBits(block, kAstcEndpointBitOffset + 4 * s, b0 >> 1, s); // v4
    writeBits(block, kAstcEndpointBitOffset + 5 * s, b1 >> 1, s); // v5
    writeBits(block, kAstcEndpointBitOffset + 6 * s, a0 >> 1, s); // v6
    writeBits(block, kAstcEndpointBitOffset + 7 * s, a1 >> 1, s); // v7
  }


  /**
   * \brief Encodes a 4x4 RGBA8 block to ASTC 4x4
   *
   * Single partition, no dual plane, CEM 12 (LDR RGBA direct) with 7-bit
   * endpoint values, and a 4x4 weight grid holding 16 quint+1LSB weights
   * (10 levels).  See the kAstc* constants above for the derivations.
   *
   * Block layout (128 bits):
   *   Bits [0:10]   — Block mode 0x242 (4x4 grid, weight range 0..9)
   *   Bits [12:11]  — Part = 00 (single partition)
   *   Bits [16:13]  — CEM = 12 (LDR RGBA direct)
   *   Bits [17:72]  — 8 endpoint values x 7 bits, Mode-12 order:
   *                   v0=R0 v1=R1 v2=G0 v3=G1 v4=B0 v5=B1 v6=A0 v7=A1
   *   Bits [73:73]  — padding (1 bit)
   *   Bits [127:74] — weight stream (54 bits, stored downwards from 127)
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

    // ─── Step 4: Compute quint+1LSB weights ────────────────────────
    // For each pixel, compute interpolation factor t ∈ [0, 1] by
    // projecting onto the min→max axis in RGBA space, then quantize to
    // the block's 10-level (0..9) weight range.  C.2.17 maps the stored
    // indices to {0,7,14,21,28,36,43,50,57,64}/64, so the quantizer
    // targets 0..64 directly and picks the nearest *unquantized* level —
    // the index order itself is scrambled (index 1 -> 64).
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

        weights[i] = quantizeWeightQuint1(t);
      }
    }

    // ─── Step 5: Pack weights into the block ───────────────────────
    // ISE-pack the 16 quint+1LSB weights into 54 bits, then store that
    // stream downwards from block bit 127 as required by C.2.16.
    const uint64_t stream = packWeightQuint1Bits(weights);
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
