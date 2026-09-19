#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>

#include "log/log.h"
#include "util_string.h"

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
  // Helper: extract N bits from a bitstream starting at startBit (LSB order)
  // Word-level implementation: one 64-bit load + shift/mask per field
  // instead of a per-bit loop. Requires little-endian host (all ARM64
  // Android and x86_64 build targets). Callers use numBits <= 16.
  inline uint32_t extractBits(const uint8_t* block, int startBit, int numBits) {
    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
      "extractBits requires a little-endian host");
    if (numBits <= 0)
      return 0;
    uint64_t lo = 0, hi = 0;
    std::memcpy(&lo, block, 8);
    std::memcpy(&hi, block + 8, 8);
    const uint32_t mask = (numBits >= 32)
      ? 0xFFFFFFFFu
      : ((1u << (unsigned)numBits) - 1u);
    if (startBit + numBits <= 64)
      return (uint32_t)((lo >> (unsigned)startBit) & mask);
    if (startBit >= 64)
      return (uint32_t)((hi >> (unsigned)(startBit - 64)) & mask);
    // Spans the 64-bit boundary. With numBits <= 32 this implies
    // startBit > 32, so the low part holds fewer than 32 bits.
    const unsigned loBits = (unsigned)(64 - startBit);
    const uint32_t loPart = (uint32_t)(lo >> (unsigned)startBit);
    const uint32_t hiPart = (uint32_t)(hi & (mask >> loBits));
    return (loPart | (hiPart << loBits)) & mask;
  }

  // BC7 weight tables (from Khronos spec / bc7enc reference)
  static constexpr uint32_t g_bc7_weights2[4] = { 0, 21, 43, 64 };
  static constexpr uint32_t g_bc7_weights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };

  // 2-subset partition table (Modes 1, 3, 7)
  // 64 partitions × 16 pixels, each pixel is 0 or 1
  static constexpr uint8_t g_bc7_partition2[64 * 16] = {
    0,0,1,1, 0,0,1,1, 0,0,1,1, 0,0,1,1, // 0
    0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1, // 1
    0,1,1,1, 0,1,1,1, 0,1,1,1, 0,1,1,1, // 2
    0,0,0,1, 0,0,1,1, 0,0,1,1, 0,1,1,1, // 3
    0,0,0,0, 0,0,0,1, 0,0,0,1, 0,0,1,1, // 4
    0,0,1,1, 0,1,1,1, 0,1,1,1, 1,1,1,1, // 5
    0,0,0,1, 0,0,1,1, 0,1,1,1, 1,1,1,1, // 6
    0,0,0,0, 0,0,0,1, 0,0,1,1, 0,1,1,1, // 7
    0,0,0,0, 0,0,0,0, 0,0,0,1, 0,0,1,1, // 8
    0,0,1,1, 0,1,1,1, 1,1,1,1, 1,1,1,1, // 9
    0,0,0,0, 0,0,0,1, 0,1,1,1, 1,1,1,1, // 10
    0,0,0,0, 0,0,0,0, 0,0,0,1, 0,1,1,1, // 11
    0,0,0,1, 0,1,1,1, 1,1,1,1, 1,1,1,1, // 12
    0,0,0,0, 0,0,0,0, 1,1,1,1, 1,1,1,1, // 13
    0,0,0,0, 1,1,1,1, 1,1,1,1, 1,1,1,1, // 14
    0,0,0,0, 0,0,0,0, 0,0,0,0, 1,1,1,1, // 15
    0,0,0,0, 1,0,0,0, 1,1,1,0, 1,1,1,1, // 16
    0,1,1,1, 0,0,0,1, 0,0,0,0, 0,0,0,0, // 17
    0,0,0,0, 0,0,0,0, 1,0,0,0, 1,1,1,0, // 18
    0,1,1,1, 0,0,1,1, 0,0,0,1, 0,0,0,0, // 19
    0,0,1,1, 0,0,0,1, 0,0,0,0, 0,0,0,0, // 20
    0,0,0,0, 1,0,0,0, 1,1,0,0, 1,1,1,0, // 21
    0,0,0,0, 0,0,0,0, 1,0,0,0, 1,1,0,0, // 22
    0,1,1,1, 0,0,1,1, 0,0,1,1, 0,0,0,1, // 23
    0,0,1,1, 0,0,0,1, 0,0,0,1, 0,0,0,0, // 24
    0,0,0,0, 1,0,0,0, 1,0,0,0, 1,1,0,0, // 25
    0,1,1,0, 0,1,1,0, 0,1,1,0, 0,1,1,0, // 26
    0,0,1,1, 0,1,1,0, 0,1,1,0, 1,1,0,0, // 27
    0,0,0,1, 0,1,1,1, 1,1,1,0, 1,0,0,0, // 28
    0,0,0,0, 1,1,1,1, 1,1,1,1, 0,0,0,0, // 29
    0,1,1,1, 0,0,0,1, 1,0,0,0, 1,1,1,0, // 30
    0,0,1,1, 1,0,0,1, 1,0,0,1, 1,1,0,0, // 31
    0,1,0,1, 0,1,0,1, 0,1,0,1, 0,1,0,1, // 32
    0,0,0,0, 1,1,1,1, 0,0,0,0, 1,1,1,1, // 33
    0,1,0,1, 1,0,1,0, 0,1,0,1, 1,0,1,0, // 34
    0,0,1,1, 0,0,1,1, 1,1,0,0, 1,1,0,0, // 35
    0,0,1,1, 1,1,0,0, 0,0,1,1, 1,1,0,0, // 36
    0,1,0,1, 0,1,0,1, 1,0,1,0, 1,0,1,0, // 37
    0,1,1,0, 1,0,0,1, 0,1,1,0, 1,0,0,1, // 38
    0,1,0,1, 1,0,1,0, 1,0,1,0, 0,1,0,1, // 39
    0,1,1,1, 0,0,1,1, 1,1,0,0, 1,1,1,0, // 40
    0,0,0,1, 0,0,1,1, 1,1,0,0, 1,0,0,0, // 41
    0,0,1,1, 0,0,1,0, 0,1,0,0, 1,1,0,0, // 42
    0,0,1,1, 1,0,1,1, 1,1,0,1, 1,1,0,0, // 43
    0,1,1,0, 1,0,0,1, 1,0,0,1, 0,1,1,0, // 44
    0,0,1,1, 1,1,0,0, 1,1,0,0, 0,0,1,1, // 45
    0,1,1,0, 0,1,1,0, 1,0,0,1, 1,0,0,1, // 46
    0,0,0,0, 0,1,1,0, 0,1,1,0, 0,0,0,0, // 47
    0,1,0,0, 1,1,1,0, 0,1,0,0, 0,0,0,0, // 48
    0,0,1,0, 0,1,1,1, 0,0,1,0, 0,0,0,0, // 49
    0,0,0,0, 0,0,1,0, 0,1,1,1, 0,0,1,0, // 50
    0,0,0,0, 0,1,0,0, 1,1,1,0, 0,1,0,0, // 51
    0,1,1,0, 1,1,0,0, 1,0,0,1, 0,0,1,1, // 52
    0,0,1,1, 0,1,1,0, 1,1,0,0, 1,0,0,1, // 53
    0,1,1,0, 0,0,1,1, 1,0,0,1, 1,1,0,0, // 54
    0,0,1,1, 1,0,0,1, 1,1,0,0, 0,1,1,0, // 55
    0,1,1,0, 1,1,0,0, 1,1,0,0, 1,0,0,1, // 56
    0,1,1,0, 0,0,1,1, 0,0,1,1, 1,0,0,1, // 57
    0,1,1,1, 1,1,1,0, 1,0,0,0, 0,0,0,1, // 58
    0,0,0,1, 1,0,0,0, 1,1,1,0, 0,1,1,1, // 59
    0,0,0,0, 1,1,1,1, 0,0,1,1, 0,0,1,1, // 60
    0,0,1,1, 0,0,1,1, 1,1,1,1, 0,0,0,0, // 61
    0,0,1,0, 0,0,1,0, 1,1,1,0, 1,1,1,0, // 62
    0,1,0,0, 0,1,0,0, 0,1,1,1, 0,1,1,1  // 63
  };

  // 3-subset partition table (Modes 0, 2)
  // 64 partitions × 16 pixels, each pixel is 0, 1, or 2
  static constexpr uint8_t g_bc7_partition3[64 * 16] = {
    0,0,1,1, 0,0,1,1, 0,2,2,1, 2,2,2,2, // 0
    0,0,0,1, 0,0,1,1, 2,2,1,1, 2,2,2,1, // 1
    0,0,0,0, 2,0,0,1, 2,2,1,1, 2,2,1,1, // 2
    0,2,2,2, 0,0,2,2, 0,0,1,1, 0,1,1,1, // 3
    0,0,0,0, 0,0,0,0, 1,1,2,2, 1,1,2,2, // 4
    0,0,1,1, 0,0,1,1, 0,0,2,2, 0,0,2,2, // 5
    0,0,2,2, 0,0,2,2, 1,1,1,1, 1,1,1,1, // 6
    0,0,1,1, 0,0,1,1, 2,2,1,1, 2,2,1,1, // 7
    0,0,0,0, 0,0,0,0, 1,1,1,1, 2,2,2,2, // 8
    0,0,0,0, 1,1,1,1, 1,1,1,1, 2,2,2,2, // 9
    0,0,0,0, 1,1,1,1, 2,2,2,2, 2,2,2,2, // 10
    0,0,1,2, 0,0,1,2, 0,0,1,2, 0,0,1,2, // 11
    0,1,1,2, 0,1,1,2, 0,1,1,2, 0,1,1,2, // 12
    0,1,2,2, 0,1,2,2, 0,1,2,2, 0,1,2,2, // 13
    0,0,1,1, 0,1,1,2, 1,1,2,2, 1,2,2,2, // 14
    0,0,1,1, 2,0,0,1, 2,2,0,0, 2,2,2,0, // 15
    0,0,0,1, 0,0,1,1, 0,1,1,2, 1,1,2,2, // 16
    0,1,1,1, 0,0,1,1, 2,0,0,1, 2,2,0,0, // 17
    0,0,0,0, 1,1,2,2, 1,1,2,2, 1,1,2,2, // 18
    0,0,2,2, 0,0,2,2, 0,0,2,2, 1,1,1,1, // 19
    0,1,1,1, 0,1,1,1, 0,2,2,2, 0,2,2,2, // 20
    0,0,0,1, 0,0,0,1, 2,2,2,1, 2,2,2,1, // 21
    0,0,0,0, 0,0,1,1, 0,1,2,2, 0,1,2,2, // 22
    0,0,0,0, 1,1,0,0, 2,2,1,0, 2,2,1,0, // 23
    0,1,2,2, 0,1,2,2, 0,0,1,1, 0,0,0,0, // 24
    0,0,1,2, 0,0,1,2, 1,1,2,2, 2,2,2,2, // 25
    0,1,1,0, 1,2,2,1, 1,2,2,1, 0,1,1,0, // 26
    0,0,0,0, 0,1,1,0, 1,2,2,1, 1,2,2,1, // 27
    0,0,2,2, 1,1,0,2, 1,1,0,2, 0,0,2,2, // 28
    0,1,1,0, 0,1,1,0, 2,0,0,2, 2,2,2,2, // 29
    0,0,1,1, 0,1,2,2, 0,1,2,2, 0,0,1,1, // 30
    0,0,0,0, 2,0,0,0, 2,2,1,1, 2,2,2,1, // 31
    0,0,0,0, 0,0,0,2, 1,1,2,2, 1,2,2,2, // 32
    0,2,2,2, 0,0,2,2, 0,0,1,2, 0,0,1,1, // 33
    0,0,1,1, 0,0,1,2, 0,0,2,2, 0,2,2,2, // 34
    0,1,2,0, 0,1,2,0, 0,1,2,0, 0,1,2,0, // 35
    0,0,0,0, 1,1,1,1, 2,2,2,2, 0,0,0,0, // 36
    0,1,2,0, 1,2,0,1, 2,0,1,2, 0,1,2,0, // 37
    0,1,2,0, 2,0,1,2, 1,2,0,1, 0,1,2,0, // 38
    0,0,1,1, 2,2,0,0, 1,1,2,2, 0,0,1,1, // 39
    0,0,1,1, 1,1,2,2, 2,2,0,0, 0,0,1,1, // 40
    0,1,0,1, 0,1,0,1, 2,2,2,2, 2,2,2,2, // 41
    0,0,0,0, 0,0,0,0, 2,1,2,1, 2,1,2,1, // 42
    0,0,2,2, 1,1,2,2, 0,0,2,2, 1,1,2,2, // 43
    0,0,2,2, 0,0,1,1, 0,0,2,2, 0,0,1,1, // 44
    0,2,2,0, 1,2,2,1, 0,2,2,0, 1,2,2,1, // 45
    0,1,0,1, 2,2,2,2, 2,2,2,2, 0,1,0,1, // 46
    0,0,0,0, 2,1,2,1, 2,1,2,1, 2,1,2,1, // 47
    0,1,0,1, 0,1,0,1, 0,1,0,1, 2,2,2,2, // 48
    0,2,2,2, 0,1,1,1, 0,2,2,2, 0,1,1,1, // 49
    0,0,0,2, 1,1,1,2, 0,0,0,2, 1,1,1,2, // 50
    0,0,0,0, 2,1,1,2, 2,1,1,2, 2,1,1,2, // 51
    0,2,2,2, 0,1,1,1, 0,1,1,1, 0,2,2,2, // 52
    0,0,0,2, 1,1,1,2, 1,1,1,2, 0,0,0,2, // 53
    0,1,1,0, 0,1,1,0, 0,1,1,0, 2,2,2,2, // 54
    0,0,0,0, 0,0,0,0, 2,1,1,2, 2,1,1,2, // 55
    0,1,1,0, 0,1,1,0, 2,2,2,2, 2,2,2,2, // 56
    0,0,2,2, 0,0,1,1, 0,0,1,1, 0,0,2,2, // 57
    0,0,2,2, 1,1,2,2, 1,1,2,2, 0,0,2,2, // 58
    0,0,0,0, 0,0,0,0, 0,0,0,0, 2,1,1,2, // 59
    0,0,0,2, 0,0,0,1, 0,0,0,2, 0,0,0,1, // 60
    0,2,2,2, 1,2,2,2, 0,2,2,2, 1,2,2,2, // 61
    0,1,0,1, 2,2,2,2, 2,2,2,2, 2,2,2,2, // 62
    0,1,1,1, 2,0,1,1, 2,2,0,1, 2,2,2,0  // 63
  };

  // Anchor index for 2nd subset of 2-subset partitions (Modes 1, 3, 7)
  static constexpr uint8_t g_bc7_anchor2[64] = {
    15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15,
    15, 2, 8, 2,  2, 8, 8,15,  2, 8, 2, 2,  8, 8, 2, 2,
    15,15, 6, 8,  2, 8,15,15,  2, 8, 2, 2,  2,15,15, 6,
     6, 2, 6, 8, 15,15, 2, 2, 15,15,15,15, 15, 2, 2,15
  };

  // Anchor index for 2nd subset of 3-subset partitions (Modes 0, 2)
  static constexpr uint8_t g_bc7_anchor3_1[64] = {
     3, 3,15,15,  8, 3,15,15,  8, 8, 6, 6,  6, 5, 3, 3,
     3, 3, 8,15,  3, 3, 6,10,  5, 8, 8, 6,  8, 5,15,15,
     8,15, 3, 5,  6,10, 8,15, 15, 3,15, 5, 15,15,15,15,
     3,15, 5, 5,  5, 8, 5,10,  5,10, 8,13, 15,12, 3, 3
  };

  // Anchor index for 3rd subset of 3-subset partitions (Modes 0, 2)
  static constexpr uint8_t g_bc7_anchor3_2[64] = {
    15, 8, 8, 3, 15,15, 3, 8, 15,15,15,15, 15,15,15, 8,
    15, 8,15, 3, 15, 8,15, 8,  3,15, 6,10, 15,15,10, 8,
    15, 3,15,10, 10, 8, 9,10,  6,15, 8,15,  3, 6, 6, 8,
    15, 3,15,15, 15,15,15,15, 15,15,15,15,  3,15,15, 8
  };

  // Helper: compute BC7 4-bit index weight
  inline uint32_t bc7Weight4(uint32_t index) {
    return (index << 2) + (index >> 2) + ((index >> 1) & 1);
  }

  // Helper: BC7 endpoint interpolation
  // result = ((64 - weight) * e0 + weight * e1 + 32) >> 6
  inline uint8_t bc7Interp(uint8_t e0, uint8_t e1, uint32_t weight) {
    return static_cast<uint8_t>(
      ((64 - weight) * static_cast<uint32_t>(e0)
        + weight * static_cast<uint32_t>(e1) + 32) >> 6);
  }

  // Helper: expand N-bit color to 8-bit via bit replication
  inline uint8_t expandBits(uint32_t value, int bits) {
    uint8_t result = static_cast<uint8_t>(value << (8 - bits));
    // Replicate MSBs into the lower bits
    for (int shift = 8 - bits; shift > 0; shift >>= 1)
      result |= (result >> shift);
    return result;
  }

  // ============================================================
  // Debug logging for BC7 decode (debug builds only)
  // ============================================================
  struct Bc7DecodeStats {
    uint32_t modeCounts[8]    = {};  // How many blocks per mode
    uint32_t unhandledBlocks  = 0;   // Blocks with unimplemented modes
    uint32_t totalBlocks      = 0;

    void reset() {
      for (int i = 0; i < 8; i++) modeCounts[i] = 0;
      unhandledBlocks = 0;
      totalBlocks = 0;
    }

    void dump() const {
#ifndef NDEBUG
      // Debug-gated by design: zero CPU cost in release builds.
      Logger::debug(str::format("[panDXVK] BC7 decode stats: total=", totalBlocks));
      for (int i = 0; i < 8; i++) {
        if (modeCounts[i]) {
          double pct = totalBlocks ? 100.0 * modeCounts[i] / totalBlocks : 0.0;
          Logger::debug(str::format("  mode ", i, ": ", modeCounts[i], " blocks (", pct, "%)"));
        }
      }
      if (unhandledBlocks)
        Logger::debug(str::format("  unhandled: ", unhandledBlocks, " blocks"));
#endif
    }
  };

  inline Bc7DecodeStats& bc7Stats() {
    static Bc7DecodeStats s_stats;
    return s_stats;
  }

  // ---- BC6H decoding ----
  // Field layout per Khronos BPTC spec; endpoint math (transform inverse,
  // unquantize, half-float conversion) validated against the bcdec
  // reference implementation (MIT, (c) 2022 Sergii Kudlai), which is also
  // the fuzz oracle. HDR output is clamped to UNORM8 (LDR approximation):
  // negatives and NaN map to 0, values above 1.0 saturate.
  struct Bc6hBitstream { uint64_t lo, hi; };

  inline uint32_t bc6hReadBits(Bc6hBitstream& bs, int n) {
    // Precondition: 1 <= n <= 31 (all BC6H reads are <= 10 bits).
    uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1u << (unsigned)n) - 1u);
    uint32_t bits = (uint32_t)(bs.lo & mask);
    bs.lo >>= n;
    bs.lo |= (bs.hi & mask) << (64 - n);
    bs.hi >>= n;
    return bits;
  }

  inline uint32_t bc6hReadBitsR(Bc6hBitstream& bs, int n) {
    // Reversed-bit read (used by single-region high-precision modes).
    uint32_t bits = bc6hReadBits(bs, n), result = 0;
    while (n--) {
      result <<= 1;
      result |= (bits & 1u);
      bits >>= 1;
    }
    return result;
  }

  inline int bc6hExtendSign(int val, int bits) {
    return (val << (32 - bits)) >> (32 - bits);
  }

  inline int bc6hTransformInverse(int val, int a0, int bits, bool isSigned) {
    // Delta endpoints wrap around base precision: B += A (mod 2^p).
    val = (val + a0) & ((1 << bits) - 1);
    if (isSigned)
      val = bc6hExtendSign(val, bits);
    return val;
  }

  inline int bc6hUnquantize(int val, int bits, bool isSigned) {
    // Bit-replicate quantized endpoints to full 16-bit range.
    int unq, s = 0;
    if (!isSigned) {
      if (bits >= 15) {
        unq = val;
      } else if (!val) {
        unq = 0;
      } else if (val == ((1 << bits) - 1)) {
        unq = 0xFFFF;
      } else {
        unq = ((val << 16) + 0x8000) >> bits;
      }
    } else {
      if (bits >= 16) {
        unq = val;
      } else {
        if (val < 0) {
          s = 1;
          val = -val;
        }
        if (val == 0) {
          unq = 0;
        } else if (val >= ((1 << (bits - 1)) - 1)) {
          unq = 0x7FFF;
        } else {
          unq = ((val << 15) + 0x4000) >> (bits - 1);
        }
        if (s)
          unq = -unq;
      }
    }
    return unq;
  }

  inline float bc6hHalfToFloat(uint16_t half) {
    // Standard half->float bit manipulation.
    union { uint32_t u; float f; } o;
    o.u = (half & 0x7FFFu) << 13;
    uint32_t exp = (0x7C00u << 13) & o.u;
    o.u += (127 - 15) << 23;
    if (exp == (0x7C00u << 13)) {
      o.u += (128 - 16) << 23;      // Inf/NaN
    } else if (exp == 0) {
      static const union { uint32_t u; float f; } magic = { 113u << 23 };
      o.u += 1u << 23;
      o.f -= magic.f;               // denormal renormalize
    }
    o.u |= (half & 0x8000u) << 16;  // sign
    return o.f;
  }

  inline uint8_t bc6hFloatToUnorm8(float f) {
    if (!(f >= 0.0f))
      f = 0.0f;   // negatives and NaN saturate to 0
    if (f > 1.0f)
      f = 1.0f;   // HDR highlights saturate (LDR approximation)
    return static_cast<uint8_t>(f * 255.0f + 0.5f);
  }

  inline uint16_t bc6hFinishHalf(int val, bool isSigned) {
    // Scale interpolated magnitude to half-float bit pattern.
    if (!isSigned)
      return static_cast<uint16_t>((val * 31) >> 6);
    int v = (val < 0) ? -(((-val) * 31) >> 5) : (val * 31) >> 5;
    int s = 0;
    if (v < 0) {
      s = 0x8000;
      v = -v;
    }
    return static_cast<uint16_t>(s | v);
  }

  // Base endpoint precision per internal mode 0..13 (W = base, dR/dG/dB = deltas).
  static constexpr int8_t g_bc6hBaseBits[4][14] = {
    { 10, 7, 11, 11, 11, 9, 8, 8, 8, 6, 10, 11, 12, 16 },  // W
    {  5, 6,  5,  4,  4, 5, 6, 5, 5, 6, 10,  9,  8,  4 },  // dR
    {  5, 6,  4,  5,  4, 5, 5, 6, 5, 6, 10,  9,  8,  4 },  // dG
    {  5, 6,  4,  4,  5, 5, 5, 5, 6, 6, 10,  9,  8,  4 },  // dB
  };

  // 32 two-region partitions. Values 0/1 select the subset;
  // bit 0x80 marks fixup pixels (one fewer index bit).
  static constexpr uint8_t g_bc6hPartition[32][4][4] = {
    { {128,0,1,1},   {0,0,1,1},     {0,0,1,1},     {0,0,1,129} },   // 0
    { {128,0,0,1},   {0,0,0,1},     {0,0,0,1},     {0,0,0,129} },   // 1
    { {128,1,1,1},   {0,1,1,1},     {0,1,1,1},     {0,1,1,129} },   // 2
    { {128,0,0,1},   {0,0,1,1},     {0,0,1,1},     {0,1,1,129} },   // 3
    { {128,0,0,0},   {0,0,0,1},     {0,0,0,1},     {0,0,1,129} },   // 4
    { {128,0,1,1},   {0,1,1,1},     {0,1,1,1},     {1,1,1,129} },   // 5
    { {128,0,0,1},   {0,0,1,1},     {0,1,1,1},     {1,1,1,129} },   // 6
    { {128,0,0,0},   {0,0,0,1},     {0,0,1,1},     {0,1,1,129} },   // 7
    { {128,0,0,0},   {0,0,0,0},     {0,0,0,1},     {0,0,1,129} },   // 8
    { {128,0,1,1},   {0,1,1,1},     {1,1,1,1},     {1,1,1,129} },   // 9
    { {128,0,0,0},   {0,0,0,1},     {0,1,1,1},     {1,1,1,129} },   // 10
    { {128,0,0,0},   {0,0,0,0},     {0,0,0,1},     {0,1,1,129} },   // 11
    { {128,0,0,1},   {0,1,1,1},     {1,1,1,1},     {1,1,1,129} },   // 12
    { {128,0,0,0},   {0,0,0,0},     {1,1,1,1},     {1,1,1,129} },   // 13
    { {128,0,0,0},   {1,1,1,1},     {1,1,1,1},     {1,1,1,129} },   // 14
    { {128,0,0,0},   {0,0,0,0},     {0,0,0,0},     {1,1,1,129} },   // 15
    { {128,0,0,0},   {1,0,0,0},     {1,1,1,0},     {1,1,1,129} },   // 16
    { {128,1,129,1}, {0,0,0,1},     {0,0,0,0},     {0,0,0,0} },     // 17
    { {128,0,0,0},   {0,0,0,0},     {129,0,0,0},   {1,1,1,0} },     // 18
    { {128,1,129,1}, {0,0,1,1},     {0,0,0,1},     {0,0,0,0} },     // 19
    { {128,0,129,1}, {0,0,0,1},     {0,0,0,0},     {0,0,0,0} },     // 20
    { {128,0,0,0},   {1,0,0,0},     {129,1,0,0},   {1,1,1,0} },     // 21
    { {128,0,0,0},   {0,0,0,0},     {129,0,0,0},   {1,1,0,0} },     // 22
    { {128,1,1,1},   {0,0,1,1},     {0,0,1,1},     {0,0,0,129} },   // 23
    { {128,0,129,1}, {0,0,0,1},     {0,0,0,1},     {0,0,0,0} },     // 24
    { {128,0,0,0},   {1,0,0,0},     {129,0,0,0},   {1,1,0,0} },     // 25
    { {128,1,129,0}, {0,1,1,0},     {0,1,1,0},     {0,1,1,0} },     // 26
    { {128,0,129,1}, {0,1,1,0},     {0,1,1,0},     {1,1,0,0} },     // 27
    { {128,0,0,1},   {0,1,1,1},     {129,1,1,0},   {1,0,0,0} },     // 28
    { {128,0,0,0},   {1,1,1,1},     {129,1,1,1},   {0,0,0,0} },     // 29
    { {128,1,129,1}, {0,0,0,1},     {1,0,0,0},     {1,1,1,0} },     // 30
    { {128,0,129,1}, {1,0,0,1},     {1,0,0,1},     {1,1,0,0} },     // 31
  };


  // Full BC6H block decoder.
  // Field-read order follows the Khronos BPTC bit layout (cross-checked
  // against the bcdec reference implementation, MIT (c) 2022 Sergii Kudlai;
  // endpoint math below is our own transcription, fuzz-validated).
  // HDR output is clamped to UNORM8 (LDR approximation, A forced opaque).
  inline void decodeBc6hBlock(
    const uint8_t*       block,
          uint8_t*       pixels,
          bool           isSigned) {
    Bc6hBitstream bs;
    std::memcpy(&bs.lo, block, 8);
    std::memcpy(&bs.hi, block + 8, 8);

    int r[4] = {0,0,0,0}, g[4] = {0,0,0,0}, b[4] = {0,0,0,0};
    int mode = (int)bc6hReadBits(bs, 2);
    if (mode > 1)
      mode |= (int)bc6hReadBits(bs, 3) << 2;
    int partition = 0;
    bool reserved = false;

    switch (mode) {

        /* mode 1 */
        case 0b00: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 75 bits (10.555, 10.555, 10.555) */
            g[2] |= bc6hReadBits(bs, 1) << 4;       /* gy[4]   */
            b[2] |= bc6hReadBits(bs, 1) << 4;       /* by[4]   */
            b[3] |= bc6hReadBits(bs, 1) << 4;       /* bz[4]   */
            r[0] |= bc6hReadBits(bs, 10);       /* rw[9:0] */
            g[0] |= bc6hReadBits(bs, 10);       /* gw[9:0] */
            b[0] |= bc6hReadBits(bs, 10);       /* bw[9:0] */
            r[1] |= bc6hReadBits(bs, 5);        /* rx[4:0] */
            g[3] |= bc6hReadBits(bs, 1) << 4;       /* gz[4]   */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 5);        /* gx[4:0] */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            g[3] |= bc6hReadBits(bs, 4);        /* gz[3:0] */
            b[1] |= bc6hReadBits(bs, 5);        /* bx[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 5);        /* ry[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            r[3] |= bc6hReadBits(bs, 5);        /* rz[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 0;
        } break;

        /* mode 2 */
        case 0b01: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 75 bits (7666, 7666, 7666) */
            g[2] |= bc6hReadBits(bs, 1) << 5;       /* gy[5]   */
            g[3] |= bc6hReadBits(bs, 1) << 4;       /* gz[4]   */
            g[3] |= bc6hReadBits(bs, 1) << 5;       /* gz[5]   */
            r[0] |= bc6hReadBits(bs, 7);        /* rw[6:0] */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[2] |= bc6hReadBits(bs, 1) << 4;       /* by[4]   */
            g[0] |= bc6hReadBits(bs, 7);        /* gw[6:0] */
            b[2] |= bc6hReadBits(bs, 1) << 5;       /* by[5]   */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            g[2] |= bc6hReadBits(bs, 1) << 4;       /* gy[4]   */
            b[0] |= bc6hReadBits(bs, 7);        /* bw[6:0] */
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            b[3] |= bc6hReadBits(bs, 1) << 5;       /* bz[5]   */
            b[3] |= bc6hReadBits(bs, 1) << 4;       /* bz[4]   */
            r[1] |= bc6hReadBits(bs, 6);        /* rx[5:0] */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 6);        /* gx[5:0] */
            g[3] |= bc6hReadBits(bs, 4);        /* gz[3:0] */
            b[1] |= bc6hReadBits(bs, 6);        /* bx[5:0] */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 6);        /* ry[5:0] */
            r[3] |= bc6hReadBits(bs, 6);        /* rz[5:0] */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 1;
        } break;

        /* mode 3 */
        case 0b00010: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 72 bits (11.555, 11.444, 11.444) */
            r[0] |= bc6hReadBits(bs, 10);       /* rw[9:0] */
            g[0] |= bc6hReadBits(bs, 10);       /* gw[9:0] */
            b[0] |= bc6hReadBits(bs, 10);       /* bw[9:0] */
            r[1] |= bc6hReadBits(bs, 5);        /* rx[4:0] */
            r[0] |= bc6hReadBits(bs, 1) << 10;      /* rw[10]  */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 4);        /* gx[3:0] */
            g[0] |= bc6hReadBits(bs, 1) << 10;      /* gw[10]  */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            g[3] |= bc6hReadBits(bs, 4);        /* gz[3:0] */
            b[1] |= bc6hReadBits(bs, 4);        /* bx[3:0] */
            b[0] |= bc6hReadBits(bs, 1) << 10;      /* bw[10]  */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 5);        /* ry[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            r[3] |= bc6hReadBits(bs, 5);        /* rz[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 2;
        } break;

        /* mode 4 */
        case 0b00110: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 72 bits (11.444, 11.555, 11.444) */
            r[0] |= bc6hReadBits(bs, 10);       /* rw[9:0] */
            g[0] |= bc6hReadBits(bs, 10);       /* gw[9:0] */
            b[0] |= bc6hReadBits(bs, 10);       /* bw[9:0] */
            r[1] |= bc6hReadBits(bs, 4);        /* rx[3:0] */
            r[0] |= bc6hReadBits(bs, 1) << 10;      /* rw[10]  */
            g[3] |= bc6hReadBits(bs, 1) << 4;       /* gz[4]   */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 5);        /* gx[4:0] */
            g[0] |= bc6hReadBits(bs, 1) << 10;      /* gw[10]  */
            g[3] |= bc6hReadBits(bs, 4);        /* gz[3:0] */
            b[1] |= bc6hReadBits(bs, 4);        /* bx[3:0] */
            b[0] |= bc6hReadBits(bs, 1) << 10;      /* bw[10]  */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 4);        /* ry[3:0] */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            r[3] |= bc6hReadBits(bs, 4);        /* rz[3:0] */
            g[2] |= bc6hReadBits(bs, 1) << 4;       /* gy[4]   */
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 3;
        } break;

        /* mode 5 */
        case 0b01010: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 72 bits (11.444, 11.444, 11.555) */
            r[0] |= bc6hReadBits(bs, 10);       /* rw[9:0] */
            g[0] |= bc6hReadBits(bs, 10);       /* gw[9:0] */
            b[0] |= bc6hReadBits(bs, 10);       /* bw[9:0] */
            r[1] |= bc6hReadBits(bs, 4);        /* rx[3:0] */
            r[0] |= bc6hReadBits(bs, 1) << 10;      /* rw[10]  */
            b[2] |= bc6hReadBits(bs, 1) << 4;       /* by[4]   */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 4);        /* gx[3:0] */
            g[0] |= bc6hReadBits(bs, 1) << 10;      /* gw[10]  */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            g[3] |= bc6hReadBits(bs, 4);        /* gz[3:0] */
            b[1] |= bc6hReadBits(bs, 5);        /* bx[4:0] */
            b[0] |= bc6hReadBits(bs, 1) << 10;      /* bw[10]  */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 4);        /* ry[3:0] */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            r[3] |= bc6hReadBits(bs, 4);        /* rz[3:0] */
            b[3] |= bc6hReadBits(bs, 1) << 4;       /* bz[4]   */ 
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 4;
        } break;

        /* mode 6 */
        case 0b01110: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 72 bits (9555, 9555, 9555) */
            r[0] |= bc6hReadBits(bs, 9);        /* rw[8:0] */
            b[2] |= bc6hReadBits(bs, 1) << 4;       /* by[4]   */
            g[0] |= bc6hReadBits(bs, 9);        /* gw[8:0] */
            g[2] |= bc6hReadBits(bs, 1) << 4;       /* gy[4]   */
            b[0] |= bc6hReadBits(bs, 9);        /* bw[8:0] */
            b[3] |= bc6hReadBits(bs, 1) << 4;       /* bz[4]   */
            r[1] |= bc6hReadBits(bs, 5);        /* rx[4:0] */
            g[3] |= bc6hReadBits(bs, 1) << 4;       /* gz[4]   */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 5);        /* gx[4:0] */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            g[3] |= bc6hReadBits(bs, 4);        /* gx[3:0] */
            b[1] |= bc6hReadBits(bs, 5);        /* bx[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 5);        /* ry[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            r[3] |= bc6hReadBits(bs, 5);        /* rz[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 5;
        } break;

        /* mode 7 */
        case 0b10010: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 72 bits (8666, 8555, 8555) */
            r[0] |= bc6hReadBits(bs, 8);        /* rw[7:0] */
            g[3] |= bc6hReadBits(bs, 1) << 4;       /* gz[4]   */
            b[2] |= bc6hReadBits(bs, 1) << 4;       /* by[4]   */
            g[0] |= bc6hReadBits(bs, 8);        /* gw[7:0] */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            g[2] |= bc6hReadBits(bs, 1) << 4;       /* gy[4]   */
            b[0] |= bc6hReadBits(bs, 8);        /* bw[7:0] */
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            b[3] |= bc6hReadBits(bs, 1) << 4;       /* bz[4]   */
            r[1] |= bc6hReadBits(bs, 6);        /* rx[5:0] */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 5);        /* gx[4:0] */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            g[3] |= bc6hReadBits(bs, 4);        /* gz[3:0] */
            b[1] |= bc6hReadBits(bs, 5);        /* bx[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 6);        /* ry[5:0] */
            r[3] |= bc6hReadBits(bs, 6);        /* rz[5:0] */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 6;
        } break;

        /* mode 8 */
        case 0b10110: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 72 bits (8555, 8666, 8555) */
            r[0] |= bc6hReadBits(bs, 8);        /* rw[7:0] */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            b[2] |= bc6hReadBits(bs, 1) << 4;       /* by[4]   */
            g[0] |= bc6hReadBits(bs, 8);        /* gw[7:0] */
            g[2] |= bc6hReadBits(bs, 1) << 5;       /* gy[5]   */
            g[2] |= bc6hReadBits(bs, 1) << 4;       /* gy[4]   */
            b[0] |= bc6hReadBits(bs, 8);        /* bw[7:0] */
            g[3] |= bc6hReadBits(bs, 1) << 5;       /* gz[5]   */
            b[3] |= bc6hReadBits(bs, 1) << 4;       /* bz[4]   */
            r[1] |= bc6hReadBits(bs, 5);        /* rx[4:0] */
            g[3] |= bc6hReadBits(bs, 1) << 4;       /* gz[4]   */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 6);        /* gx[5:0] */
            g[3] |= bc6hReadBits(bs, 4);        /* zx[3:0] */
            b[1] |= bc6hReadBits(bs, 5);        /* bx[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 5);        /* ry[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            r[3] |= bc6hReadBits(bs, 5);        /* rz[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 7;
        } break;

        /* mode 9 */
        case 0b11010: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 72 bits (8555, 8555, 8666) */
            r[0] |= bc6hReadBits(bs, 8);        /* rw[7:0] */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[2] |= bc6hReadBits(bs, 1) << 4;       /* by[4]   */
            g[0] |= bc6hReadBits(bs, 8);        /* gw[7:0] */
            b[2] |= bc6hReadBits(bs, 1) << 5;       /* by[5]   */
            g[2] |= bc6hReadBits(bs, 1) << 4;       /* gy[4]   */
            b[0] |= bc6hReadBits(bs, 8);        /* bw[7:0] */
            b[3] |= bc6hReadBits(bs, 1) << 5;       /* bz[5]   */
            b[3] |= bc6hReadBits(bs, 1) << 4;       /* bz[4]   */
            r[1] |= bc6hReadBits(bs, 5);        /* bw[4:0] */
            g[3] |= bc6hReadBits(bs, 1) << 4;       /* gz[4]   */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 5);        /* gx[4:0] */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            g[3] |= bc6hReadBits(bs, 4);        /* gz[3:0] */
            b[1] |= bc6hReadBits(bs, 6);        /* bx[5:0] */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 5);        /* ry[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            r[3] |= bc6hReadBits(bs, 5);        /* rz[4:0] */
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 8;
        } break;

        /* mode 10 */
        case 0b11110: {
            /* Partitition indices: 46 bits
               Partition: 5 bits
               Color Endpoints: 72 bits (6666, 6666, 6666) */
            r[0] |= bc6hReadBits(bs, 6);        /* rw[5:0] */
            g[3] |= bc6hReadBits(bs, 1) << 4;       /* gz[4]   */
            b[3] |= bc6hReadBits(bs, 1);            /* bz[0]   */
            b[3] |= bc6hReadBits(bs, 1) << 1;       /* bz[1]   */
            b[2] |= bc6hReadBits(bs, 1) << 4;       /* by[4]   */
            g[0] |= bc6hReadBits(bs, 6);        /* gw[5:0] */
            g[2] |= bc6hReadBits(bs, 1) << 5;       /* gy[5]   */
            b[2] |= bc6hReadBits(bs, 1) << 5;       /* by[5]   */
            b[3] |= bc6hReadBits(bs, 1) << 2;       /* bz[2]   */
            g[2] |= bc6hReadBits(bs, 1) << 4;       /* gy[4]   */
            b[0] |= bc6hReadBits(bs, 6);        /* bw[5:0] */
            g[3] |= bc6hReadBits(bs, 1) << 5;       /* gz[5]   */
            b[3] |= bc6hReadBits(bs, 1) << 3;       /* bz[3]   */
            b[3] |= bc6hReadBits(bs, 1) << 5;       /* bz[5]   */
            b[3] |= bc6hReadBits(bs, 1) << 4;       /* bz[4]   */
            r[1] |= bc6hReadBits(bs, 6);        /* rx[5:0] */
            g[2] |= bc6hReadBits(bs, 4);        /* gy[3:0] */
            g[1] |= bc6hReadBits(bs, 6);        /* gx[5:0] */
            g[3] |= bc6hReadBits(bs, 4);        /* gz[3:0] */
            b[1] |= bc6hReadBits(bs, 6);        /* bx[5:0] */
            b[2] |= bc6hReadBits(bs, 4);        /* by[3:0] */
            r[2] |= bc6hReadBits(bs, 6);        /* ry[5:0] */
            r[3] |= bc6hReadBits(bs, 6);        /* rz[5:0] */
            partition = bc6hReadBits(bs, 5);    /* d[4:0]  */
            mode = 9;
        } break;

        /* mode 11 */
        case 0b00011: {
            /* Partitition indices: 63 bits
               Partition: 0 bits
               Color Endpoints: 60 bits (10.10, 10.10, 10.10) */
            r[0] |= bc6hReadBits(bs, 10);       /* rw[9:0] */
            g[0] |= bc6hReadBits(bs, 10);       /* gw[9:0] */
            b[0] |= bc6hReadBits(bs, 10);       /* bw[9:0] */
            r[1] |= bc6hReadBits(bs, 10);       /* rx[9:0] */
            g[1] |= bc6hReadBits(bs, 10);       /* gx[9:0] */
            b[1] |= bc6hReadBits(bs, 10);       /* bx[9:0] */
            mode = 10;
        } break;

        /* mode 12 */
        case 0b00111: {
            /* Partitition indices: 63 bits
               Partition: 0 bits
               Color Endpoints: 60 bits (11.9, 11.9, 11.9) */
            r[0] |= bc6hReadBits(bs, 10);       /* rw[9:0] */
            g[0] |= bc6hReadBits(bs, 10);       /* gw[9:0] */
            b[0] |= bc6hReadBits(bs, 10);       /* bw[9:0] */
            r[1] |= bc6hReadBits(bs, 9);        /* rx[8:0] */
            r[0] |= bc6hReadBits(bs, 1) << 10;      /* rw[10]  */
            g[1] |= bc6hReadBits(bs, 9);        /* gx[8:0] */
            g[0] |= bc6hReadBits(bs, 1) << 10;      /* gw[10]  */
            b[1] |= bc6hReadBits(bs, 9);        /* bx[8:0] */
            b[0] |= bc6hReadBits(bs, 1) << 10;      /* bw[10]  */
            mode = 11;
        } break;

        /* mode 13 */
        case 0b01011: {
            /* Partitition indices: 63 bits
               Partition: 0 bits
               Color Endpoints: 60 bits (12.8, 12.8, 12.8) */
            r[0] |= bc6hReadBits(bs, 10);       /* rw[9:0] */
            g[0] |= bc6hReadBits(bs, 10);       /* gw[9:0] */
            b[0] |= bc6hReadBits(bs, 10);       /* bw[9:0] */
            r[1] |= bc6hReadBits(bs, 8);        /* rx[7:0] */
            r[0] |= bc6hReadBitsR(bs, 2) << 10;/* rx[10:11] */
            g[1] |= bc6hReadBits(bs, 8);        /* gx[7:0] */
            g[0] |= bc6hReadBitsR(bs, 2) << 10;/* gx[10:11] */
            b[1] |= bc6hReadBits(bs, 8);        /* bx[7:0] */
            b[0] |= bc6hReadBitsR(bs, 2) << 10;/* bx[10:11] */
            mode = 12;
        } break;

        /* mode 14 */
        case 0b01111: {
            /* Partitition indices: 63 bits
               Partition: 0 bits
               Color Endpoints: 60 bits (16.4, 16.4, 16.4) */
            r[0] |= bc6hReadBits(bs, 10);       /* rw[9:0] */
            g[0] |= bc6hReadBits(bs, 10);       /* gw[9:0] */
            b[0] |= bc6hReadBits(bs, 10);       /* bw[9:0] */
            r[1] |= bc6hReadBits(bs, 4);        /* rx[3:0] */
            r[0] |= bc6hReadBitsR(bs, 6) << 10;/* rw[10:15] */
            g[1] |= bc6hReadBits(bs, 4);        /* gx[3:0] */
            g[0] |= bc6hReadBitsR(bs, 6) << 10;/* gw[10:15] */
            b[1] |= bc6hReadBits(bs, 4);        /* bx[3:0] */
            b[0] |= bc6hReadBitsR(bs, 6) << 10;/* bw[10:15] */
            mode = 13;
        } break;

              default: {
        // Reserved patterns: spec mandates zeroes (alpha stays opaque here).
        reserved = true;
      } break;
    }

    if (reserved) {
      for (int i = 0; i < 16; i++) {
        pixels[i * 4 + 0] = 0;
        pixels[i * 4 + 1] = 0;
        pixels[i * 4 + 2] = 0;
        pixels[i * 4 + 3] = 255;
      }
      return;
    }

    int numPartitions = (mode >= 10) ? 0 : 1;
    int baseBits = (int)g_bc6hBaseBits[0][mode];
    if (isSigned) {
      r[0] = bc6hExtendSign(r[0], baseBits);
      g[0] = bc6hExtendSign(g[0], baseBits);
      b[0] = bc6hExtendSign(b[0], baseBits);
    }

    // Delta endpoints are stored relative to base (modes 0-8 only).
    if ((mode != 9 && mode != 10) || isSigned) {
      for (int i = 1; i < (numPartitions + 1) * 2; ++i) {
        r[i] = bc6hExtendSign(r[i], (int)g_bc6hBaseBits[1][mode]);
        g[i] = bc6hExtendSign(g[i], (int)g_bc6hBaseBits[2][mode]);
        b[i] = bc6hExtendSign(b[i], (int)g_bc6hBaseBits[3][mode]);
      }
    }

    if (mode != 9 && mode != 10) {
      for (int i = 1; i < (numPartitions + 1) * 2; ++i) {
        r[i] = bc6hTransformInverse(r[i], r[0], baseBits, isSigned);
        g[i] = bc6hTransformInverse(g[i], g[0], baseBits, isSigned);
        b[i] = bc6hTransformInverse(b[i], b[0], baseBits, isSigned);
      }
    }

    for (int i = 0; i < (numPartitions + 1) * 2; ++i) {
      r[i] = bc6hUnquantize(r[i], baseBits, isSigned);
      g[i] = bc6hUnquantize(g[i], baseBits, isSigned);
      b[i] = bc6hUnquantize(b[i], baseBits, isSigned);
    }

    for (int pi = 0; pi < 16; pi++) {
      int py = pi / 4, px = pi % 4;
      int subs, nbits;
      if (mode >= 10) {
        // Single-region modes: one subset, fixup (short index) on pixel 0.
        subs = 0;
        nbits = (pi == 0) ? 3 : 4;
      } else {
        int ps = g_bc6hPartition[partition][py][px];
        nbits = 3;
        if (ps & 0x80)
          nbits--;  // fixup pixel: one fewer index bit
        subs = ps & 0x01;
      }
      int idx = (int)bc6hReadBits(bs, nbits);
      int ep = subs * 2;
      int w = (mode >= 10)
        ? (int)bc7Weight4((uint32_t)idx)
        : (int)g_bc7_weights3[idx];
      int ri = ((64 - w) * r[ep] + w * r[ep + 1] + 32) >> 6;
      int gi = ((64 - w) * g[ep] + w * g[ep + 1] + 32) >> 6;
      int bi = ((64 - w) * b[ep] + w * b[ep + 1] + 32) >> 6;
      pixels[pi * 4 + 0] = bc6hFloatToUnorm8(bc6hHalfToFloat(bc6hFinishHalf(ri, isSigned)));
      pixels[pi * 4 + 1] = bc6hFloatToUnorm8(bc6hHalfToFloat(bc6hFinishHalf(gi, isSigned)));
      pixels[pi * 4 + 2] = bc6hFloatToUnorm8(bc6hHalfToFloat(bc6hFinishHalf(bi, isSigned)));
      pixels[pi * 4 + 3] = 255;
    }
  }

  inline void decodeBcBlock(
          BcFormat       bcFormat,
    const uint8_t*       block,
          uint8_t*       pixels,
          bool           bc6hSigned = false) {
    // Helper: double 6-bit color to 8-bit
    auto expand6 = [](uint32_t v) -> uint8_t {
      return static_cast<uint8_t>((v << 2) | (v >> 4));
    };

    // Helper: double 5-bit color to 8-bit
    auto expand5 = [](uint32_t v) -> uint8_t {
      return static_cast<uint8_t>((v << 3) | (v >> 2));
    };

    // Helper: build the 4-entry BC1 color palette (spec-correct).
    // fourColor=true: [c0, c1, (2c0+c1)/3, (c0+2c1)/3], all opaque.
    // fourColor=false: [c0, c1, (c0+c1)/2, transparent black].
    // BC1 passes (c0 > c1); BC2/BC3 color always passes true.
    auto bc1Palette = [expand5, expand6](uint16_t c0, uint16_t c1,
                                         bool fourColor, uint8_t pal[4][4]) {
      uint8_t e0[4] = { expand5((c0 >> 11) & 0x1F), expand6((c0 >> 5) & 0x3F),
                        expand5(c0 & 0x1F), 255 };
      uint8_t e1[4] = { expand5((c1 >> 11) & 0x1F), expand6((c1 >> 5) & 0x3F),
                        expand5(c1 & 0x1F), 255 };
      std::memcpy(pal[0], e0, 4);
      std::memcpy(pal[1], e1, 4);
      if (fourColor) {
        for (int c = 0; c < 3; c++) {
          pal[2][c] = static_cast<uint8_t>((2 * e0[c] + e1[c]) / 3);
          pal[3][c] = static_cast<uint8_t>((e0[c] + 2 * e1[c]) / 3);
        }
        pal[2][3] = 255;
        pal[3][3] = 255;
      } else {
        for (int c = 0; c < 3; c++)
          pal[2][c] = static_cast<uint8_t>((e0[c] + e1[c]) / 2);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
      }
    };

    // Helper: decode one 8-byte BC4 block to 16 channel values
    // (spec-correct palette: /7 eight-value when e0>e1,
    // /5 six-value + 0/255 otherwise; 3-bit indices packed LSB-first).
    // Doubles as the BC3 alpha decoder (BC3 alpha IS a BC4 block).
    auto decodeBc4Channel = [](const uint8_t* blk, uint8_t out[16]) {
      const uint8_t e0 = blk[0], e1 = blk[1];
      uint8_t pal[8];
      pal[0] = e0;
      pal[1] = e1;
      if (e0 > e1) {
        for (int k = 2; k < 8; k++)
          pal[k] = static_cast<uint8_t>(((8 - k) * e0 + (k - 1) * e1) / 7);
      } else {
        for (int k = 2; k < 6; k++)
          pal[k] = static_cast<uint8_t>(((6 - k) * e0 + (k - 1) * e1) / 5);
        pal[6] = 0;
        pal[7] = 255;
      }
      uint64_t idx = 0;
      for (int i = 0; i < 6; i++)
        idx |= static_cast<uint64_t>(blk[2 + i]) << (8 * i);
      for (int i = 0; i < 16; i++)
        out[i] = pal[(idx >> (3 * i)) & 7];
    };

    switch (bcFormat) {
      case BcFormat::BC1: {
        // BC1: 8 bytes per block. c0 > c1 selects 4-opaque-color mode,
        // c0 <= c1 selects 3-color + transparent mode.
        uint16_t c0 = block[0] | (block[1] << 8);
        uint16_t c1 = block[2] | (block[3] << 8);
        uint32_t indices = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);

        uint8_t colors[4][4];
        bc1Palette(c0, c1, c0 > c1, colors);

        for (int i = 0; i < 16; i++) {
          uint32_t idx = (indices >> (2 * i)) & 3;
          std::memcpy(pixels + i * 4, colors[idx], 4);
        }
        break;
      }

      case BcFormat::BC2: {
        // BC2: 16 bytes per block, 4-bit alpha + BC1 color
        uint16_t c0 = block[8]  | (block[9]  << 8);
        uint16_t c1 = block[10] | (block[11] << 8);
        uint32_t colorIndices = block[12] | (block[13] << 8)
                              | (block[14] << 16) | (block[15] << 24);

        // BC2 color is always 4-opaque-color (no endpoint comparison).
        uint8_t colors[4][4];
        bc1Palette(c0, c1, true, colors);

        // Alpha: bytes 0..7, two 4-bit nibbles per byte, pixel i in byte[i/2].
        for (int i = 0; i < 16; i++) {
          uint32_t ci = (colorIndices >> (2 * i)) & 3;

          uint32_t alphaBits = (block[i / 2] >> (4 * (i % 2))) & 0xF;

          uint32_t alpha = alphaBits | (alphaBits << 4);
          pixels[i * 4 + 0] = colors[ci][0];
          pixels[i * 4 + 1] = colors[ci][1];
          pixels[i * 4 + 2] = colors[ci][2];
          pixels[i * 4 + 3] = static_cast<uint8_t>(alpha);
        }
        break;
      }

      case BcFormat::BC3: {
        // BC3: 16 bytes per block, 8-bit alpha + BC1 color
        uint16_t c0 = block[8]  | (block[9]  << 8);
        uint16_t c1 = block[10] | (block[11] << 8);
        uint32_t colorIndices = block[12] | (block[13] << 8)
                              | (block[14] << 16) | (block[15] << 24);

        // BC3 color is always 4-opaque-color (no endpoint comparison).
        uint8_t colors[4][4];
        bc1Palette(c0, c1, true, colors);

        // BC3 alpha block (bytes 0..7) IS a BC4 block.
        uint8_t alphaVals[16];
        decodeBc4Channel(block, alphaVals);

        for (int i = 0; i < 16; i++) {
          uint32_t ci = (colorIndices >> (2 * i)) & 3;
          pixels[i * 4 + 0] = colors[ci][0];
          pixels[i * 4 + 1] = colors[ci][1];
          pixels[i * 4 + 2] = colors[ci][2];
          pixels[i * 4 + 3] = alphaVals[i];
        }
        break;
      }

      case BcFormat::BC4: {
        // BC4: 8 bytes per block, single-channel (R8). Others set to
        // replicate the channel (G, B) and opaque alpha, matching how
        // games sample .r/.g lookups from BC4 textures.
        uint8_t chan[16];
        decodeBc4Channel(block, chan);

        for (int i = 0; i < 16; i++) {
          uint8_t r = chan[i];
          pixels[i * 4 + 0] = r;
          pixels[i * 4 + 1] = r;
          pixels[i * 4 + 2] = r;
          pixels[i * 4 + 3] = 255;
        }
        break;
      }

      case BcFormat::BC5: {
        // BC5: 16 bytes per block, two-channel (RG8) = two BC4 blocks.
        uint8_t blockR[16], blockG[16];
        decodeBc4Channel(block, blockR);
        decodeBc4Channel(block + 8, blockG);

        for (int i = 0; i < 16; i++) {
          pixels[i * 4 + 0] = blockR[i];
          pixels[i * 4 + 1] = blockG[i];
          pixels[i * 4 + 2] = 0;
          pixels[i * 4 + 3] = 255;
        }
        break;
      }

      case BcFormat::BC6H: {
        // BC6H: 16 bytes per block, HDR RGB (UF16/SF16 half-float).
        // Full 14-mode decoder (see decodeBc6hBlock); HDR clamped to LDR.
        decodeBc6hBlock(block, pixels, bc6hSigned);
        break;
      }

      case BcFormat::BC7: {
        // BC7: 16 bytes per block (128 bits), high-quality RGBA
        // Mode is identified by the lowest set bit in block[0]:
        //   Mode 0 = bit0 set, Mode 1 = bit1 set, ..., Mode 7 = bit7 set
        //
        // Supported modes:
        //   Mode 0: 3 subsets, RGBP 4.4.4.1, 3-bit indices
        //   Mode 1: 2 subsets, RGBP 6.6.6.1, 3-bit indices
        //   Mode 2: 3 subsets, RGB 5.5.5, 2-bit indices
        //   Mode 3: 2 subsets, RGBP 7.7.7.1, 2-bit indices
        //   Mode 4: 1 subset, RGB 5.5.5 + A 6-bit, 2/3-bit mixed indices
        //   Mode 5: 1 subset, RGB 7.7.7 + A 8-bit, 2/2-bit indices
        //   Mode 6: 1 subset, RGBAP 7.7.7.7.1, 4-bit indices (~70% of blocks)
        //   Mode 7: 2 subsets, RGBAP 5.5.5.5.1, 2-bit indices

        // Detect mode: find position of lowest set bit in block[0]
        int mode = -1;
        for (int i = 0; i < 8; i++) {
          if (block[0] & (1 << i)) {
            mode = i;
            break;
          }
        }

#ifndef NDEBUG
        bc7Stats().totalBlocks++;
        if (mode >= 0 && mode < 8)
          bc7Stats().modeCounts[mode]++;
#endif

        if (mode == 6) {
          // Mode 6: 1 subset, RGBAP 7.7.7.7.1 endpoints, 16×4-bit indices
          // Layout: [0:6]=mode, [7:13]=R0, [14:20]=R1, [21:27]=G0,
          //         [28:34]=G1, [35:41]=B0, [42:48]=B1, [49:55]=A0,
          //         [56:62]=A1, [63]=EPB0, [64]=EPB1, [65:127]=indices

          uint8_t r0_7 = extractBits(block, 7,  7);
          uint8_t r1_7 = extractBits(block, 14, 7);
          uint8_t g0_7 = extractBits(block, 21, 7);
          uint8_t g1_7 = extractBits(block, 28, 7);
          uint8_t b0_7 = extractBits(block, 35, 7);
          uint8_t b1_7 = extractBits(block, 42, 7);
          uint8_t a0_7 = extractBits(block, 49, 7);
          uint8_t a1_7 = extractBits(block, 56, 7);

          uint8_t epb0 = extractBits(block, 63, 1);
          uint8_t epb1 = extractBits(block, 64, 1);

          uint8_t r0 = (r0_7 << 1) | epb0;
          uint8_t r1 = (r1_7 << 1) | epb1;
          uint8_t g0 = (g0_7 << 1) | epb0;
          uint8_t g1 = (g1_7 << 1) | epb1;
          uint8_t b0 = (b0_7 << 1) | epb0;
          uint8_t b1 = (b1_7 << 1) | epb1;
          uint8_t a0 = (a0_7 << 1) | epb0;
          uint8_t a1 = (a1_7 << 1) | epb1;

          // Pixel 0 anchor: 3 bits (MSB fixed to 0), pixels 1–15: 4 bits each
          uint32_t idx0 = extractBits(block, 65, 3);
          uint32_t w0   = bc7Weight4(idx0);

          pixels[0] = bc7Interp(r0, r1, w0);
          pixels[1] = bc7Interp(g0, g1, w0);
          pixels[2] = bc7Interp(b0, b1, w0);
          pixels[3] = bc7Interp(a0, a1, w0);

          for (int i = 1; i < 16; i++) {
            uint32_t idx = extractBits(block, 68 + (i - 1) * 4, 4);
            uint32_t w   = bc7Weight4(idx);
            pixels[i * 4 + 0] = bc7Interp(r0, r1, w);
            pixels[i * 4 + 1] = bc7Interp(g0, g1, w);
            pixels[i * 4 + 2] = bc7Interp(b0, b1, w);
            pixels[i * 4 + 3] = bc7Interp(a0, a1, w);
          }

        } else if (mode == 2) {
          // Mode 2: 3 subsets, RGB 5.5.5 endpoints, 2-bit indices
          // Layout: [0:2]=mode(001), [3:8]=partition(6 bits),
          //         [9:13]=R0, [14:18]=R1, [19:23]=R2, [24:28]=R3,
          //         [29:33]=R4, [34:38]=R5, [39:43]=G0, [44:48]=G1,
          //         [49:53]=G2, [54:58]=G3, [59:63]=G4, [64:68]=G5,
          //         [69:73]=B0, [74:78]=B1, [79:83]=B2, [84:88]=B3,
          //         [89:93]=B4, [94:98]=B5, [99:127]=indices(29 bits)
          //
          // 6 endpoints: (R0,G0,B0), (R1,G1,B1), (R2,G2,B2),
          //              (R3,G3,B3), (R4,G4,B4), (R5,G5,B5)
          // Subset 0 uses ep0,ep1; Subset 1 uses ep2,ep3; Subset 2 uses ep4,ep5

          uint32_t partition = extractBits(block, 3, 6);

          // Extract 5-bit channel values and expand to 8-bit via bit replication
          auto expand5 = [](uint32_t v5) -> uint8_t {
            return static_cast<uint8_t>((v5 << 3) | (v5 >> 2));
          };

          uint8_t ep[6][3]; // [endpoint_pair][channel: R=0,G=1,B=2]
          ep[0][0] = expand5(extractBits(block,  9, 5));
          ep[1][0] = expand5(extractBits(block, 14, 5));
          ep[2][0] = expand5(extractBits(block, 19, 5));
          ep[3][0] = expand5(extractBits(block, 24, 5));
          ep[4][0] = expand5(extractBits(block, 29, 5));
          ep[5][0] = expand5(extractBits(block, 34, 5));

          ep[0][1] = expand5(extractBits(block, 39, 5));
          ep[1][1] = expand5(extractBits(block, 44, 5));
          ep[2][1] = expand5(extractBits(block, 49, 5));
          ep[3][1] = expand5(extractBits(block, 54, 5));
          ep[4][1] = expand5(extractBits(block, 59, 5));
          ep[5][1] = expand5(extractBits(block, 64, 5));

          ep[0][2] = expand5(extractBits(block, 69, 5));
          ep[1][2] = expand5(extractBits(block, 74, 5));
          ep[2][2] = expand5(extractBits(block, 79, 5));
          ep[3][2] = expand5(extractBits(block, 84, 5));
          ep[4][2] = expand5(extractBits(block, 89, 5));
          ep[5][2] = expand5(extractBits(block, 94, 5));

          // 29 index bits starting at bit 99
          // 3 subsets, anchor pixel for each subset has 1 fewer bit
          // Subset 0 anchor = pixel 0 (always), subset 1 anchor = table lookup
          // Total bits: 16 pixels × 2 bits = 32, minus 3 anchor bits = 29 bits

          // Sequential index reader: 29 index bits starting at bit 99,
          // consumed in pixel order. Anchors store one fewer bit (1 vs 2).
          uint32_t bitPos = 99;

          // For each pixel, determine subset and extract index
          for (int i = 0; i < 16; i++) {
            uint32_t subset = g_bc7_partition3[partition * 16 + i];
            uint32_t epIdx  = subset * 2; // endpoint pair index

            // Anchor: pixel 0, plus table anchors for subsets 1 and 2.
            // (Pixel 0 is always subset 0 per the partition tables.)
            bool isAnchor = (i == 0)
              || (subset == 1 && i == g_bc7_anchor3_1[partition])
              || (subset == 2 && i == g_bc7_anchor3_2[partition]);
            uint32_t nbits = isAnchor ? 1u : 2u;
            uint32_t idx = extractBits(block, bitPos, nbits);
            bitPos += nbits;

            uint32_t w = g_bc7_weights2[idx];

            pixels[i * 4 + 0] = bc7Interp(ep[epIdx][0], ep[epIdx + 1][0], w);
            pixels[i * 4 + 1] = bc7Interp(ep[epIdx][1], ep[epIdx + 1][1], w);
            pixels[i * 4 + 2] = bc7Interp(ep[epIdx][2], ep[epIdx + 1][2], w);
            pixels[i * 4 + 3] = 255; // Mode 2 has no alpha
          }

        } else if (mode == 3) {
          // Mode 3: 2 subsets, RGBP 7.7.7.1 endpoints, 2-bit indices
          // Layout: [0:3]=mode(0001), [4:9]=partition(6 bits),
          //         [10:16]=R0, [17:23]=R1, [24:30]=R2, [31:37]=R3,
          //         [38:44]=G0, [45:51]=G1, [52:58]=G2, [59:65]=G3,
          //         [66:72]=B0, [73:79]=B1, [80:86]=B2, [87:93]=B3,
          //         [94]=EPB0, [95]=EPB1, [96]=EPB2, [97]=EPB3,
          //         [98:127]=indices(30 bits)
          //
          // 4 endpoints: (R0,G0,B0), (R1,G1,B1), (R2,G2,B2), (R3,G3,B3)
          // Subset 0 uses ep0,ep1; Subset 1 uses ep2,ep3

          uint32_t partition = extractBits(block, 4, 6);

          // Extract 7-bit channel values + P-bit → 8-bit.
          // 7+1 = 8 significant bits already: full = (v << 1) | p,
          // no replication needed (unlike 6+1/4+1-bit modes).
          auto expand7P = [](uint32_t v, uint32_t pbit) -> uint8_t {
            return static_cast<uint8_t>(((v << 1) | (pbit & 1u)) & 0xFFu);
          };
          uint8_t ep[4][3]; // [endpoint][channel: R=0,G=1,B=2]

          ep[0][0] = expand7P(extractBits(block, 10, 7), extractBits(block, 94, 1));
          ep[1][0] = expand7P(extractBits(block, 17, 7), extractBits(block, 95, 1));
          ep[2][0] = expand7P(extractBits(block, 24, 7), extractBits(block, 96, 1));
          ep[3][0] = expand7P(extractBits(block, 31, 7), extractBits(block, 97, 1));

          ep[0][1] = expand7P(extractBits(block, 38, 7), extractBits(block, 94, 1));
          ep[1][1] = expand7P(extractBits(block, 45, 7), extractBits(block, 95, 1));
          ep[2][1] = expand7P(extractBits(block, 52, 7), extractBits(block, 96, 1));
          ep[3][1] = expand7P(extractBits(block, 59, 7), extractBits(block, 97, 1));

          ep[0][2] = expand7P(extractBits(block, 66, 7), extractBits(block, 94, 1));
          ep[1][2] = expand7P(extractBits(block, 73, 7), extractBits(block, 95, 1));
          ep[2][2] = expand7P(extractBits(block, 80, 7), extractBits(block, 96, 1));
          ep[3][2] = expand7P(extractBits(block, 87, 7), extractBits(block, 97, 1));

          // Sequential index reader: 30 index bits starting at bit 98,
          // consumed in pixel order. Anchors store one fewer bit (1 vs 2).
          uint32_t bitPos = 98;

          for (int i = 0; i < 16; i++) {
            uint32_t subset = g_bc7_partition2[partition * 16 + i];
            uint32_t epIdx  = subset * 2;

            // Anchor: pixel 0, plus table anchor for subset 1.
            // (Pixel 0 is always subset 0 per the partition tables.)
            bool isAnchor = (i == 0)
              || (subset == 1 && i == g_bc7_anchor2[partition]);
            uint32_t nbits = isAnchor ? 1u : 2u;
            uint32_t idx = extractBits(block, bitPos, nbits);
            bitPos += nbits;

            uint32_t w = g_bc7_weights2[idx];

            pixels[i * 4 + 0] = bc7Interp(ep[epIdx][0], ep[epIdx + 1][0], w);
            pixels[i * 4 + 1] = bc7Interp(ep[epIdx][1], ep[epIdx + 1][1], w);
            pixels[i * 4 + 2] = bc7Interp(ep[epIdx][2], ep[epIdx + 1][2], w);
            pixels[i * 4 + 3] = 255; // Mode 3 has no alpha
          }

        } else if (mode == 0) {
          // Mode 0: 3 subsets, RGBP 4.4.4.1 endpoints, 3-bit indices, 16 partitions
          // Layout: [0]=mode(1), [1:4]=partition(4), [5:28]=channels(24),
          //         [29:52]=channels(24), [53:76]=channels(24),
          //         [77:82]=EPBs(6), [83:127]=indices(45)
          //
          // 6 endpoints × 3 channels × 4 bits + 6 P-bits = 78 endpoint bits
          // 16 partitions (4-bit partition select)
          // 3-bit indices, 45 index bits total

          uint32_t partition = extractBits(block, 1, 4);

          // Extract 6 endpoints: each has R,G,B (4 bits) + P-bit (1 bit).
          // P-bit extends precision: v5 = (v4 << 1) | p, then replicate 5->8.
          auto expand4P = [](uint32_t v4, uint32_t pbit) -> uint8_t {
            uint32_t v5 = (v4 << 1) | (pbit & 1u);
            return static_cast<uint8_t>((v5 << 3) | (v5 >> 2));
          };

          uint8_t ep[6][3]; // [endpoint][R=0,G=1,B=2]
          ep[0][0] = expand4P(extractBits(block,  5, 4), extractBits(block, 77, 1));
          ep[1][0] = expand4P(extractBits(block,  9, 4), extractBits(block, 78, 1));
          ep[2][0] = expand4P(extractBits(block, 13, 4), extractBits(block, 79, 1));
          ep[3][0] = expand4P(extractBits(block, 17, 4), extractBits(block, 80, 1));
          ep[4][0] = expand4P(extractBits(block, 21, 4), extractBits(block, 81, 1));
          ep[5][0] = expand4P(extractBits(block, 25, 4), extractBits(block, 82, 1));

          ep[0][1] = expand4P(extractBits(block, 29, 4), extractBits(block, 77, 1));
          ep[1][1] = expand4P(extractBits(block, 33, 4), extractBits(block, 78, 1));
          ep[2][1] = expand4P(extractBits(block, 37, 4), extractBits(block, 79, 1));
          ep[3][1] = expand4P(extractBits(block, 41, 4), extractBits(block, 80, 1));
          ep[4][1] = expand4P(extractBits(block, 45, 4), extractBits(block, 81, 1));
          ep[5][1] = expand4P(extractBits(block, 49, 4), extractBits(block, 82, 1));

          ep[0][2] = expand4P(extractBits(block, 53, 4), extractBits(block, 77, 1));
          ep[1][2] = expand4P(extractBits(block, 57, 4), extractBits(block, 78, 1));
          ep[2][2] = expand4P(extractBits(block, 61, 4), extractBits(block, 79, 1));
          ep[3][2] = expand4P(extractBits(block, 65, 4), extractBits(block, 80, 1));
          ep[4][2] = expand4P(extractBits(block, 69, 4), extractBits(block, 81, 1));
          ep[5][2] = expand4P(extractBits(block, 73, 4), extractBits(block, 82, 1));

          // Sequential index reader: 45 index bits starting at bit 83,
          // consumed in pixel order. Anchors store one fewer bit (2 vs 3).
          uint32_t bitPos = 83;

          for (int i = 0; i < 16; i++) {
            uint32_t subset = g_bc7_partition3[partition * 16 + i];
            uint32_t epIdx  = subset * 2;

            // Anchor: pixel 0, plus table anchors for subsets 1 and 2.
            // (Pixel 0 is always subset 0 per the partition tables.)
            bool isAnchor = (i == 0)
              || (subset == 1 && i == g_bc7_anchor3_1[partition])
              || (subset == 2 && i == g_bc7_anchor3_2[partition]);
            uint32_t nbits = isAnchor ? 2u : 3u;
            uint32_t idx = extractBits(block, bitPos, nbits);
            bitPos += nbits;

            uint32_t w = g_bc7_weights3[idx];

            pixels[i * 4 + 0] = bc7Interp(ep[epIdx][0], ep[epIdx + 1][0], w);
            pixels[i * 4 + 1] = bc7Interp(ep[epIdx][1], ep[epIdx + 1][1], w);
            pixels[i * 4 + 2] = bc7Interp(ep[epIdx][2], ep[epIdx + 1][2], w);
            pixels[i * 4 + 3] = 255; // Mode 0 has no alpha
          }

        } else if (mode == 1) {
          // Mode 1: 2 subsets, RGBP 6.6.6.1 endpoints, 3-bit indices, 64 partitions
          // Layout: [0:1]=mode(01), [2:7]=partition(6),
          //         [8:13]=R0, [14:19]=R1, [20:25]=R2, [26:31]=R3,
          //         [32:37]=G0, [38:43]=G1, [44:49]=G2, [50:55]=G3,
          //         [56:61]=B0, [62:67]=B1, [68:73]=B2, [74:79]=B3,
          //         [80]=SPB0, [81]=SPB1, [82:127]=indices(46 bits)
          //
          // Shared P-bit: both endpoints in a subset share the same P-bit
          // 8-bit_channel = (6-bit << 1) | shared_P-bit

          uint32_t partition = extractBits(block, 2, 6);

          // Extract shared P-bits
          uint32_t spb0 = extractBits(block, 80, 1);
          uint32_t spb1 = extractBits(block, 81, 1);

          // 4 endpoints × 3 channels × 6 bits = 72 endpoint bits
          // Shared P-bit extends precision: v7 = (v6 << 1) | p,
          // then replicate 7->8.
          auto expand6P = [](uint32_t v6, uint32_t pbit) -> uint8_t {
            uint32_t v7 = (v6 << 1) | (pbit & 1u);
            return static_cast<uint8_t>((v7 << 1) | (v7 >> 6));
          };

          uint8_t ep[4][3]; // [endpoint][R=0,G=1,B=2]
          // Subset 0 endpoints (share spb0)
          ep[0][0] = expand6P(extractBits(block,  8, 6), spb0);
          ep[1][0] = expand6P(extractBits(block, 14, 6), spb0);
          ep[0][1] = expand6P(extractBits(block, 32, 6), spb0);
          ep[1][1] = expand6P(extractBits(block, 38, 6), spb0);
          ep[0][2] = expand6P(extractBits(block, 56, 6), spb0);
          ep[1][2] = expand6P(extractBits(block, 62, 6), spb0);
          // Subset 1 endpoints (share spb1)
          ep[2][0] = expand6P(extractBits(block, 20, 6), spb1);
          ep[3][0] = expand6P(extractBits(block, 26, 6), spb1);
          ep[2][1] = expand6P(extractBits(block, 44, 6), spb1);
          ep[3][1] = expand6P(extractBits(block, 50, 6), spb1);
          ep[2][2] = expand6P(extractBits(block, 68, 6), spb1);
          ep[3][2] = expand6P(extractBits(block, 74, 6), spb1);

          // Sequential index reader: 46 index bits starting at bit 82,
          // consumed in pixel order. Anchors store one fewer bit (2 vs 3).
          uint32_t bitPos = 82;

          for (int i = 0; i < 16; i++) {
            uint32_t subset = g_bc7_partition2[partition * 16 + i];
            uint32_t epIdx  = subset * 2;

            // Anchor: pixel 0, plus table anchor for subset 1.
            // (Pixel 0 is always subset 0 per the partition tables.)
            bool isAnchor = (i == 0)
              || (subset == 1 && i == g_bc7_anchor2[partition]);
            uint32_t nbits = isAnchor ? 2u : 3u;
            uint32_t idx = extractBits(block, bitPos, nbits);
            bitPos += nbits;

            uint32_t w = g_bc7_weights3[idx];

            pixels[i * 4 + 0] = bc7Interp(ep[epIdx][0], ep[epIdx + 1][0], w);
            pixels[i * 4 + 1] = bc7Interp(ep[epIdx][1], ep[epIdx + 1][1], w);
            pixels[i * 4 + 2] = bc7Interp(ep[epIdx][2], ep[epIdx + 1][2], w);
            pixels[i * 4 + 3] = 255; // Mode 1 has no alpha
          }

        } else if (mode == 4) {
          // Mode 4: 1 subset, RGB 5.5.5 + A 6-bit endpoints, mixed 2/3-bit indices
          // Layout: [0:4]=mode(00001), [5:6]=rotation, [7]=ISB,
          //         [8:12]=R0(5), [13:17]=R1(5), [18:22]=G0(5), [23:27]=G1(5),
          //         [28:32]=B0(5), [33:37]=B1(5), [38:43]=A0(6), [44:49]=A1(6),
          //         [50:80]=primary_idx(31), [81:127]=secondary_idx(47)
          //
          // ISB=0: color=2-bit(primary), alpha=3-bit(secondary)
          // ISB=1: color=3-bit(secondary), alpha=2-bit(primary)

          uint32_t rotation = extractBits(block, 5, 2);
          uint32_t isb = extractBits(block, 7, 1);

          // 5-bit RGB endpoints → 8-bit via bit replication
          auto expand5 = [](uint32_t v5) -> uint8_t {
            return static_cast<uint8_t>((v5 << 3) | (v5 >> 2));
          };
          // 6-bit alpha → 8-bit via bit replication
          auto expand6 = [](uint32_t v6) -> uint8_t {
            return static_cast<uint8_t>((v6 << 2) | (v6 >> 4));
          };

          uint8_t r0 = expand5(extractBits(block, 8,  5));
          uint8_t r1 = expand5(extractBits(block, 13, 5));
          uint8_t g0 = expand5(extractBits(block, 18, 5));
          uint8_t g1 = expand5(extractBits(block, 23, 5));
          uint8_t b0 = expand5(extractBits(block, 28, 5));
          uint8_t b1 = expand5(extractBits(block, 33, 5));
          uint8_t a0 = expand6(extractBits(block, 38, 6));
          uint8_t a1 = expand6(extractBits(block, 44, 6));

          // Primary indices (2-bit) from bits [50:80], 31 bits
          // Anchor pixel 0: 1 bit (MSB implicitly 0), pixels 1-15: 2 bits each
          uint32_t primIdx[16];
          primIdx[0] = extractBits(block, 50, 1);
          for (int pi = 1; pi < 16; pi++)
            primIdx[pi] = extractBits(block, 51 + (pi - 1) * 2, 2);

          // Secondary indices (3-bit) from bits [81:127], 47 bits
          // Anchor pixel 0: 2 bits (MSB implicitly 0), pixels 1-15: 3 bits each
          uint32_t secIdx[16];
          secIdx[0] = extractBits(block, 81, 2);
          for (int pi = 1; pi < 16; pi++)
            secIdx[pi] = extractBits(block, 83 + (pi - 1) * 3, 3);

          for (int pi = 0; pi < 16; pi++) {
            uint32_t colorWeight, alphaWeight;
            if (isb == 0) {
              colorWeight = g_bc7_weights2[primIdx[pi]];
              alphaWeight = g_bc7_weights3[secIdx[pi]];
            } else {
              colorWeight = g_bc7_weights3[secIdx[pi]];
              alphaWeight = g_bc7_weights2[primIdx[pi]];
            }

            uint8_t r = bc7Interp(r0, r1, colorWeight);
            uint8_t g = bc7Interp(g0, g1, colorWeight);
            uint8_t b = bc7Interp(b0, b1, colorWeight);
            uint8_t a = bc7Interp(a0, a1, alphaWeight);

            // Apply component rotation
            switch (rotation) {
              case 0: break;                          // RGB|A
              case 1: std::swap(r, a); break;         // A|RGB (swap R↔A)
              case 2: std::swap(g, a); break;         // RAGB (swap G↔A)
              case 3: std::swap(b, a); break;         // RGBA→swap B↔A
            }

            pixels[pi * 4 + 0] = r;
            pixels[pi * 4 + 1] = g;
            pixels[pi * 4 + 2] = b;
            pixels[pi * 4 + 3] = a;
          }

        } else if (mode == 5) {
          // Mode 5: 1 subset, RGB 7.7.7 + A 8-bit endpoints, 2/2-bit indices
          // Layout: [0:5]=mode(000001), [6:7]=rotation,
          //         [8:14]=R0(7), [15:21]=R1(7), [22:28]=G0(7), [29:35]=G1(7),
          //         [36:42]=B0(7), [43:49]=B1(7), [50:57]=A0(8), [58:65]=A1(8),
          //         [66:96]=primary_idx(31), [97:127]=secondary_idx(31)

          uint32_t rotation = extractBits(block, 6, 2);

          // 7-bit → 8-bit via bit replication
          auto expand7 = [](uint32_t v7) -> uint8_t {
            return static_cast<uint8_t>((v7 << 1) | (v7 >> 6));
          };

          uint8_t r0 = expand7(extractBits(block, 8,  7));
          uint8_t r1 = expand7(extractBits(block, 15, 7));
          uint8_t g0 = expand7(extractBits(block, 22, 7));
          uint8_t g1 = expand7(extractBits(block, 29, 7));
          uint8_t b0 = expand7(extractBits(block, 36, 7));
          uint8_t b1 = expand7(extractBits(block, 43, 7));
          uint8_t a0 = static_cast<uint8_t>(extractBits(block, 50, 8));
          uint8_t a1 = static_cast<uint8_t>(extractBits(block, 58, 8));

          // Primary indices (2-bit) from bits [66:96], 31 bits
          uint32_t primIdx[16];
          primIdx[0] = extractBits(block, 66, 1);
          for (int pi = 1; pi < 16; pi++)
            primIdx[pi] = extractBits(block, 67 + (pi - 1) * 2, 2);

          // Secondary indices (2-bit) from bits [97:127], 31 bits
          uint32_t secIdx[16];
          secIdx[0] = extractBits(block, 97, 1);
          for (int pi = 1; pi < 16; pi++)
            secIdx[pi] = extractBits(block, 98 + (pi - 1) * 2, 2);

          for (int pi = 0; pi < 16; pi++) {
            uint32_t colorWeight = g_bc7_weights2[primIdx[pi]];
            uint32_t alphaWeight = g_bc7_weights2[secIdx[pi]];

            uint8_t r = bc7Interp(r0, r1, colorWeight);
            uint8_t g = bc7Interp(g0, g1, colorWeight);
            uint8_t b = bc7Interp(b0, b1, colorWeight);
            uint8_t a = bc7Interp(a0, a1, alphaWeight);

            switch (rotation) {
              case 0: break;
              case 1: std::swap(r, a); break;
              case 2: std::swap(g, a); break;
              case 3: std::swap(b, a); break;
            }

            pixels[pi * 4 + 0] = r;
            pixels[pi * 4 + 1] = g;
            pixels[pi * 4 + 2] = b;
            pixels[pi * 4 + 3] = a;
          }

        } else if (mode == 7) {
          // Mode 7: 2 subsets, RGBAP 5.5.5.5.1 endpoints, 2-bit indices, 64 partitions
          // Layout: [0:7]=mode(00000001), [8:13]=partition(6),
          //         [14:18]=R0(5), [19:23]=R1(5), [24:28]=R2(5), [29:33]=R3(5),
          //         [34:38]=G0(5), [39:43]=G1(5), [44:48]=G2(5), [49:53]=G3(5),
          //         [54:58]=B0(5), [59:63]=B1(5), [64:68]=B2(5), [69:73]=B3(5),
          //         [74:78]=A0(5), [79:83]=A1(5), [84:88]=A2(5), [89:93]=A3(5),
          //         [94]=EPB0, [95]=EPB1, [96]=EPB2, [97]=EPB3,
          //         [98:127]=indices(30 bits)

          uint32_t partition = extractBits(block, 8, 6);

          // Extract P-bits (unique per endpoint)
          uint32_t epb0 = extractBits(block, 94, 1);
          uint32_t epb1 = extractBits(block, 95, 1);
          uint32_t epb2 = extractBits(block, 96, 1);
          uint32_t epb3 = extractBits(block, 97, 1);

          // P-bit extends precision: v6 = (v5 << 1) | p, then replicate 6->8.
          auto expand5P = [](uint32_t v5, uint32_t pbit) -> uint8_t {
            uint32_t v6 = (v5 << 1) | (pbit & 1u);
            return static_cast<uint8_t>((v6 << 2) | (v6 >> 4));
          };

          // 4 endpoints × RGBA channels
          uint8_t ep[4][4]; // [endpoint][R=0,G=1,B=2,A=3]
          ep[0][0] = expand5P(extractBits(block, 14, 5), epb0);
          ep[1][0] = expand5P(extractBits(block, 19, 5), epb1);
          ep[2][0] = expand5P(extractBits(block, 24, 5), epb2);
          ep[3][0] = expand5P(extractBits(block, 29, 5), epb3);
          ep[0][1] = expand5P(extractBits(block, 34, 5), epb0);
          ep[1][1] = expand5P(extractBits(block, 39, 5), epb1);
          ep[2][1] = expand5P(extractBits(block, 44, 5), epb2);
          ep[3][1] = expand5P(extractBits(block, 49, 5), epb3);
          ep[0][2] = expand5P(extractBits(block, 54, 5), epb0);
          ep[1][2] = expand5P(extractBits(block, 59, 5), epb1);
          ep[2][2] = expand5P(extractBits(block, 64, 5), epb2);
          ep[3][2] = expand5P(extractBits(block, 69, 5), epb3);
          ep[0][3] = expand5P(extractBits(block, 74, 5), epb0);
          ep[1][3] = expand5P(extractBits(block, 79, 5), epb1);
          ep[2][3] = expand5P(extractBits(block, 84, 5), epb2);
          ep[3][3] = expand5P(extractBits(block, 89, 5), epb3);

          // Sequential index reader: 30 index bits starting at bit 98,
          // consumed in pixel order. Anchors store one fewer bit (1 vs 2).
          uint32_t bitPos = 98;

          for (int pi = 0; pi < 16; pi++) {
            uint32_t subset = g_bc7_partition2[partition * 16 + pi];
            uint32_t epIdx  = subset * 2;

            // Anchor: pixel 0, plus table anchor for subset 1.
            // (Pixel 0 is always subset 0 per the partition tables.)
            bool isAnchor = (pi == 0)
              || (subset == 1 && pi == g_bc7_anchor2[partition]);
            uint32_t nbits = isAnchor ? 1u : 2u;
            uint32_t idx = extractBits(block, bitPos, nbits);
            bitPos += nbits;

            uint32_t w = g_bc7_weights2[idx];

            pixels[pi * 4 + 0] = bc7Interp(ep[epIdx][0], ep[epIdx + 1][0], w);
            pixels[pi * 4 + 1] = bc7Interp(ep[epIdx][1], ep[epIdx + 1][1], w);
            pixels[pi * 4 + 2] = bc7Interp(ep[epIdx][2], ep[epIdx + 1][2], w);
            pixels[pi * 4 + 3] = bc7Interp(ep[epIdx][3], ep[epIdx + 1][3], w);
          }

        } else {
          // Unknown mode (should not happen with valid BC7 data)
#ifndef NDEBUG
          bc7Stats().unhandledBlocks++;
#endif
          for (int i = 0; i < 16; i++) {
            pixels[i * 4 + 0] = 255;
            pixels[i * 4 + 1] = 0;
            pixels[i * 4 + 2] = 255;
            pixels[i * 4 + 3] = 255;
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
          VkDeviceSize   dstRowPitch,
          bool           bc6hSigned = false) {
    uint32_t blockWidth  = (width + 3) / 4;
    uint32_t blockHeight = (height + 3) / 4;

    // Hoisted out of the per-block loop (loop-invariant).
    const uint32_t blockSizeBytes =
      (bcFormat == BcFormat::BC1 || bcFormat == BcFormat::BC4) ? 8 : 16;

    uint32_t srcBlockPitch;
    if (srcRowPitch > 0) {
      srcBlockPitch = static_cast<uint32_t>(srcRowPitch);
    } else {
      srcBlockPitch = blockWidth * blockSizeBytes;
    }

    if (dstRowPitch == 0)
      dstRowPitch = width * 4;

    uint8_t blockPixels[64]; // 4x4 x 4 bytes

    for (uint32_t by = 0; by < blockHeight; by++) {
      for (uint32_t bx = 0; bx < blockWidth; bx++) {
        const uint8_t* block = srcData + static_cast<VkDeviceSize>(by) * srcBlockPitch + bx * blockSizeBytes;

        decodeBcBlock(bcFormat, block, blockPixels, bc6hSigned);

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
