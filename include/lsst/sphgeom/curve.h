/*
 * LSST Data Management System
 * Copyright 2016 AURA/LSST.
 *
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the LSST License Statement and
 * the GNU General Public License along with this program.  If not,
 * see <https://www.lsstcorp.org/LegalNotices/>.
 */

#ifndef LSST_SPHGEOM_CURVE_H_
#define LSST_SPHGEOM_CURVE_H_

/// \file
/// \brief This file contains functions for space-filling curves.
///
/// Mappings between 2-D points with non-negative integer coordinates
/// and their corresponding Morton or Hilbert indexes are provided.
///
/// The Morton order implementation, mortonIndex, is straightforward. The
/// Hilbert order implementation is derived from Algorithm 2 in:
///
///     C. Hamilton. Compact Hilbert indices. Technical Report CS-2006-07,
///     Dalhousie University, Faculty of Computer Science, Jul 2006.
///     https://www.cs.dal.ca/research/techreports/cs-2006-07
///
/// Using the variable names from that paper, n is fixed at 2. As a first
/// step, the arithmetic in the loop over the bits of the input coordinates
/// is replaced by a table lookup. In particular, the lookup maps the values
/// of (e, d, l) at the beginning of a loop iteration to the values (e, d, w)
/// at the end. Since e and d can both be represented by a single bit, and l
/// and w are 2 bits wide, the lookup table has 16 4 bit entries and fits in
/// a single 64 bit integer constant (HILBERT_LUT_1). The implementation
/// then looks like:
///
///     inline uint64_t hilbertIndex(uint32_t x, uint32_t y, uint32_t m) {
///         uint64_t const z = mortonIndex(x, y);
///         uint64_t h = 0;
///         uint64_t i = 0;
///         for (m = 2 * m; m != 0;) {
///             m -= 2;
///             i = (i & 0xc) | ((z >> m) & 3);
///             i = HILBERT_LUT_1 >> (i * 4);
///             h = (h << 2) | (i & 3);
///         }
///         return h;
///     }
///
/// Note that interleaving x and y with mortonIndex beforehand allows the loop
/// to extract 2 bits at a time from z, rather than extracting bits from x and
/// y and then pasting them together. This lowers the total operation count.
///
/// Performance is further increased by executing j loop iterations at a time.
/// This requires using a larger lookup table that maps the values of e and d
/// at the beginning of a loop iteration, along with 2j input bits, to the
/// values of e and d after j iterations, along with 2j output bits.
/// In this implementation, j = 3, which corresponds to a 256 byte LUT. On
/// recent Intel CPUs the LUT fits in 4 cache lines, and, because of adjacent
/// cache line prefetch, should become cache resident after just 2 misses.
///
/// For a helpful presentation of the technical report, as well as a
/// reference implementation of its algorithms in Python, see
/// [Pierre de Buyl's notebook](https://github.com/pdebuyl/compute/blob/master/hilbert_curve/hilbert_curve.ipynb).
/// The Hilbert curve lookup tables below were generated using a trivial
/// modification of that code (available in makeHilbertLuts.py).

#if !defined(NO_SIMD) && defined(__x86_64__)
    #include <x86intrin.h>
#endif
#include <cstdint>
#include <tuple>


namespace lsst {
namespace sphgeom {

///@{
/// `log2` returns the index of the most significant 1 bit in x.
/// If x is 0, the return value is 0.
///
/// A beautiful algorithm to find this index is presented in:
///
/// Using de Bruijn Sequences to Index a 1 in a Computer Word
/// C. E. Leiserson, H. Prokop, and K. H. Randall.
/// http://supertech.csail.mit.edu/papers/debruijn.pdf
inline uint8_t log2(uint64_t x) {
    alignas(64) static uint8_t const PERFECT_HASH_TABLE[64] = {
         0,  1,  2,  7,  3, 13,  8, 19,  4, 25, 14, 28,  9, 34, 20, 40,
         5, 17, 26, 38, 15, 46, 29, 48, 10, 31, 35, 54, 21, 50, 41, 57,
        63,  6, 12, 18, 24, 27, 33, 39, 16, 37, 45, 47, 30, 53, 49, 56,
        62, 11, 23, 32, 36, 44, 52, 55, 61, 22, 43, 51, 60, 42, 59, 58
    };
    uint64_t const DE_BRUIJN_SEQUENCE = UINT64_C(0x0218a392cd3d5dbf);
    // First ensure that all bits below the MSB are set.
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    x |= (x >> 32);
    // Then, subtract them away.
    x = x - (x >> 1);
    // Multiplication by x is now a shift by the index i of the MSB.
    //
    // The value of the upper 6 bits of a 64-bit De Bruijn sequence left
    // shifted by i is different for every value of i in 0..63. It can
    // therefore be used as an an index into a lookup table that recovers i.
    // In other words, (DE_BRUIJN_SEQUENCE * x) >> 58 is a minimal perfect
    // hash function for 64 bit powers of 2.
    return PERFECT_HASH_TABLE[(DE_BRUIJN_SEQUENCE * x) >> 58];
}

inline uint8_t log2(uint32_t x) {
    // See https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
    alignas(32) static uint8_t const PERFECT_HASH_TABLE[32] = {
        0,  9,  1, 10, 13, 21,  2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
        8, 12, 20, 28, 15, 17, 24,  7, 19, 27, 23,  6, 26,  5, 4, 31
    };
    uint32_t const DE_BRUIJN_SEQUENCE = UINT32_C(0x07c4acdd);
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return PERFECT_HASH_TABLE[(DE_BRUIJN_SEQUENCE * x) >> 27];
}
///@}

/// `mortonIndex` interleaves the bits of x and y.
///
/// The 32 even bits of the return value will be the bits of x, and the 32
/// odd bits those of y. This is the z-value of (x,y) defined by the Morton
/// order function. See:
///
/// - https://en.wikipedia.org/wiki/Z-order_curve
///
/// for more information.
#if defined(NO_SIMD) || !defined(__x86_64__)
    inline uint64_t mortonIndex(uint32_t x, uint32_t y) {
        // This is just a 64-bit extension of:
        // http://graphics.stanford.edu/~seander/bithacks.html#InterleaveBMN
        uint64_t b = y;
        uint64_t a = x;
        b = (b | (b << 16)) & UINT64_C(0x0000ffff0000ffff);
        a = (a | (a << 16)) & UINT64_C(0x0000ffff0000ffff);
        b = (b | (b << 8)) & UINT64_C(0x00ff00ff00ff00ff);
        a = (a | (a << 8)) & UINT64_C(0x00ff00ff00ff00ff);
        b = (b | (b << 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
        a = (a | (a << 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
        b = (b | (b << 2)) & UINT64_C(0x3333333333333333);
        a = (a | (a << 2)) & UINT64_C(0x3333333333333333);
        b = (b | (b << 1)) & UINT64_C(0x5555555555555555);
        a = (a | (a << 1)) & UINT64_C(0x5555555555555555);
        return a | (b << 1);
    }
#else
    #ifndef SWIG
        inline uint64_t mortonIndex(__m128i xy) {
            xy = _mm_and_si128(_mm_or_si128(xy, _mm_slli_epi64(xy, 16)),
                               _mm_set1_epi32(0x0000ffff));
            xy = _mm_and_si128(_mm_or_si128(xy, _mm_slli_epi64(xy, 8)),
                               _mm_set1_epi32(0x00ff00ff));
            xy = _mm_and_si128(_mm_or_si128(xy, _mm_slli_epi64(xy, 4)),
                               _mm_set1_epi32(0x0f0f0f0f));
            xy = _mm_and_si128(_mm_or_si128(xy, _mm_slli_epi64(xy, 2)),
                               _mm_set1_epi32(0x33333333));
            xy = _mm_and_si128(_mm_or_si128(xy, _mm_slli_epi64(xy, 1)),
                               _mm_set1_epi32(0x55555555));
            __m128i y = _mm_unpackhi_epi64(xy, _mm_setzero_si128());
            __m128i r = _mm_or_si128(xy, _mm_slli_epi64(y, 1));
            return static_cast<uint64_t>(_mm_cvtsi128_si64(r));
        }
    #endif

    inline uint64_t mortonIndex(uint32_t x, uint32_t y) {
        __m128i xy = _mm_set_epi64x(static_cast<int64_t>(y),
                                    static_cast<int64_t>(x));
        return mortonIndex(xy);
    }
#endif

/// `mortonIndexInverse` separates the even and odd bits of z.
///
/// The 32 even bits of z are returned in the first element of the result
/// tuple, and the 32 odd bits in the second. This is the inverse of
/// mortonIndex().
#if defined(NO_SIMD) || !defined(__x86_64__)
    inline std::tuple<uint32_t, uint32_t> mortonIndexInverse(uint64_t z) {
        uint64_t x = z & UINT64_C(0x5555555555555555);
        uint64_t y = (z >> 1) & UINT64_C(0x5555555555555555);
        x = (x | (x >> 1)) & UINT64_C(0x3333333333333333);
        y = (y | (y >> 1)) & UINT64_C(0x3333333333333333);
        x = (x | (x >> 2)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
        y = (y | (y >> 2)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
        x = (x | (x >> 4)) & UINT64_C(0x00ff00ff00ff00ff);
        y = (y | (y >> 4)) & UINT64_C(0x00ff00ff00ff00ff);
        x = (x | (x >> 8)) & UINT64_C(0x0000ffff0000ffff);
        y = (y | (y >> 8)) & UINT64_C(0x0000ffff0000ffff);
        return std::make_tuple(static_cast<uint32_t>(x | (x >> 16)),
                               static_cast<uint32_t>(y | (y >> 16)));
    }
#else
    #ifndef SWIG
        inline __m128i mortonIndexInverseSimd(uint64_t z) {
            __m128i xy = _mm_set_epi64x(static_cast<int64_t>(z >> 1),
                                        static_cast<int64_t>(z));
            xy = _mm_and_si128(xy, _mm_set1_epi32(0x55555555));
            xy = _mm_and_si128(_mm_or_si128(xy, _mm_srli_epi64(xy, 1)),
                               _mm_set1_epi32(0x33333333));
            xy = _mm_and_si128(_mm_or_si128(xy, _mm_srli_epi64(xy, 2)),
                               _mm_set1_epi32(0x0f0f0f0f));
            xy = _mm_and_si128(_mm_or_si128(xy, _mm_srli_epi64(xy, 4)),
                               _mm_set1_epi32(0x00ff00ff));
            xy = _mm_and_si128(_mm_or_si128(xy, _mm_srli_epi64(xy, 8)),
                               _mm_set1_epi32(0x0000ffff));
            xy = _mm_or_si128(xy, _mm_srli_epi64(xy, 16));
            return xy;
        }
    #endif

    inline std::tuple<uint32_t, uint32_t> mortonIndexInverse(uint64_t z) {
        __m128i xy = mortonIndexInverseSimd(z);
        uint64_t r = _mm_cvtsi128_si64(_mm_shuffle_epi32(xy, 8));
        return std::make_tuple(static_cast<uint32_t>(r & 0xffffffff),
                               static_cast<uint32_t>(r >> 32));
    }
#endif

static constexpr uint64_t HILBERT_LUT_1 = UINT64_C(0x8d3ec79a6b5021f4);

static constexpr uint64_t HILBERT_INVERSE_LUT_1 = UINT64_C(0x1ceb689fa750d324);

/// `mortonToHilbert` converts the 2m-bit Morton index z to the
/// corresponding Hilbert index.
inline uint64_t mortonToHilbert(uint64_t z, int m) {
    alignas(64) static uint8_t const HILBERT_LUT_3[256] = {
        0x40, 0xc3, 0x01, 0x02, 0x04, 0x45, 0x87, 0x46,
        0x8e, 0x8d, 0x4f, 0xcc, 0x08, 0x49, 0x8b, 0x4a,
        0xfa, 0x3b, 0xf9, 0xb8, 0x7c, 0xff, 0x3d, 0x3e,
        0xf6, 0x37, 0xf5, 0xb4, 0xb2, 0xb1, 0x73, 0xf0,
        0x10, 0x51, 0x93, 0x52, 0xde, 0x1f, 0xdd, 0x9c,
        0x54, 0xd7, 0x15, 0x16, 0x58, 0xdb, 0x19, 0x1a,
        0x20, 0x61, 0xa3, 0x62, 0xee, 0x2f, 0xed, 0xac,
        0x64, 0xe7, 0x25, 0x26, 0x68, 0xeb, 0x29, 0x2a,
        0x00, 0x41, 0x83, 0x42, 0xce, 0x0f, 0xcd, 0x8c,
        0x44, 0xc7, 0x05, 0x06, 0x48, 0xcb, 0x09, 0x0a,
        0x50, 0xd3, 0x11, 0x12, 0x14, 0x55, 0x97, 0x56,
        0x9e, 0x9d, 0x5f, 0xdc, 0x18, 0x59, 0x9b, 0x5a,
        0xba, 0xb9, 0x7b, 0xf8, 0xb6, 0xb5, 0x77, 0xf4,
        0x3c, 0x7d, 0xbf, 0x7e, 0xf2, 0x33, 0xf1, 0xb0,
        0x60, 0xe3, 0x21, 0x22, 0x24, 0x65, 0xa7, 0x66,
        0xae, 0xad, 0x6f, 0xec, 0x28, 0x69, 0xab, 0x6a,
        0xaa, 0xa9, 0x6b, 0xe8, 0xa6, 0xa5, 0x67, 0xe4,
        0x2c, 0x6d, 0xaf, 0x6e, 0xe2, 0x23, 0xe1, 0xa0,
        0x9a, 0x99, 0x5b, 0xd8, 0x96, 0x95, 0x57, 0xd4,
        0x1c, 0x5d, 0x9f, 0x5e, 0xd2, 0x13, 0xd1, 0x90,
        0x70, 0xf3, 0x31, 0x32, 0x34, 0x75, 0xb7, 0x76,
        0xbe, 0xbd, 0x7f, 0xfc, 0x38, 0x79, 0xbb, 0x7a,
        0xca, 0x0b, 0xc9, 0x88, 0x4c, 0xcf, 0x0d, 0x0e,
        0xc6, 0x07, 0xc5, 0x84, 0x82, 0x81, 0x43, 0xc0,
        0xea, 0x2b, 0xe9, 0xa8, 0x6c, 0xef, 0x2d, 0x2e,
        0xe6, 0x27, 0xe5, 0xa4, 0xa2, 0xa1, 0x63, 0xe0,
        0x30, 0x71, 0xb3, 0x72, 0xfe, 0x3f, 0xfd, 0xbc,
        0x74, 0xf7, 0x35, 0x36, 0x78, 0xfb, 0x39, 0x3a,
        0xda, 0x1b, 0xd9, 0x98, 0x5c, 0xdf, 0x1d, 0x1e,
        0xd6, 0x17, 0xd5, 0x94, 0x92, 0x91, 0x53, 0xd0,
        0x8a, 0x89, 0x4b, 0xc8, 0x86, 0x85, 0x47, 0xc4,
        0x0c, 0x4d, 0x8f, 0x4e, 0xc2, 0x03, 0xc1, 0x80
    };
    uint64_t h = 0;
    uint64_t i = 0;
    for (m = 2 * m; m >= 6;) {
        m -= 6;
        uint8_t j = HILBERT_LUT_3[i | ((z >> m) & 0x3f)];
        h = (h << 6) | (j & 0x3f);
        i = j & 0xc0;
    }
    if (m != 0) {
        // m = 2 or 4
        int r = 6 - m;
        uint8_t j = HILBERT_LUT_3[i | ((z << r) & 0x3f)];
        h = (h << m) | ((j & 0x3f) >> r);
    }
    return h;
}

/// `hilbertToMorton` converts the 2m-bit Hilbert index h to the
/// corresponding Morton index.
inline uint64_t hilbertToMorton(uint64_t h, int m) {
    alignas(64) static uint8_t const HILBERT_INVERSE_LUT_3[256] = {
        0x40, 0x02, 0x03, 0xc1, 0x04, 0x45, 0x47, 0x86,
        0x0c, 0x4d, 0x4f, 0x8e, 0xcb, 0x89, 0x88, 0x4a,
        0x20, 0x61, 0x63, 0xa2, 0x68, 0x2a, 0x2b, 0xe9,
        0x6c, 0x2e, 0x2f, 0xed, 0xa7, 0xe6, 0xe4, 0x25,
        0x30, 0x71, 0x73, 0xb2, 0x78, 0x3a, 0x3b, 0xf9,
        0x7c, 0x3e, 0x3f, 0xfd, 0xb7, 0xf6, 0xf4, 0x35,
        0xdf, 0x9d, 0x9c, 0x5e, 0x9b, 0xda, 0xd8, 0x19,
        0x93, 0xd2, 0xd0, 0x11, 0x54, 0x16, 0x17, 0xd5,
        0x00, 0x41, 0x43, 0x82, 0x48, 0x0a, 0x0b, 0xc9,
        0x4c, 0x0e, 0x0f, 0xcd, 0x87, 0xc6, 0xc4, 0x05,
        0x50, 0x12, 0x13, 0xd1, 0x14, 0x55, 0x57, 0x96,
        0x1c, 0x5d, 0x5f, 0x9e, 0xdb, 0x99, 0x98, 0x5a,
        0x70, 0x32, 0x33, 0xf1, 0x34, 0x75, 0x77, 0xb6,
        0x3c, 0x7d, 0x7f, 0xbe, 0xfb, 0xb9, 0xb8, 0x7a,
        0xaf, 0xee, 0xec, 0x2d, 0xe7, 0xa5, 0xa4, 0x66,
        0xe3, 0xa1, 0xa0, 0x62, 0x28, 0x69, 0x6b, 0xaa,
        0xff, 0xbd, 0xbc, 0x7e, 0xbb, 0xfa, 0xf8, 0x39,
        0xb3, 0xf2, 0xf0, 0x31, 0x74, 0x36, 0x37, 0xf5,
        0x9f, 0xde, 0xdc, 0x1d, 0xd7, 0x95, 0x94, 0x56,
        0xd3, 0x91, 0x90, 0x52, 0x18, 0x59, 0x5b, 0x9a,
        0x8f, 0xce, 0xcc, 0x0d, 0xc7, 0x85, 0x84, 0x46,
        0xc3, 0x81, 0x80, 0x42, 0x08, 0x49, 0x4b, 0x8a,
        0x60, 0x22, 0x23, 0xe1, 0x24, 0x65, 0x67, 0xa6,
        0x2c, 0x6d, 0x6f, 0xae, 0xeb, 0xa9, 0xa8, 0x6a,
        0xbf, 0xfe, 0xfc, 0x3d, 0xf7, 0xb5, 0xb4, 0x76,
        0xf3, 0xb1, 0xb0, 0x72, 0x38, 0x79, 0x7b, 0xba,
        0xef, 0xad, 0xac, 0x6e, 0xab, 0xea, 0xe8, 0x29,
        0xa3, 0xe2, 0xe0, 0x21, 0x64, 0x26, 0x27, 0xe5,
        0xcf, 0x8d, 0x8c, 0x4e, 0x8b, 0xca, 0xc8, 0x09,
        0x83, 0xc2, 0xc0, 0x01, 0x44, 0x06, 0x07, 0xc5,
        0x10, 0x51, 0x53, 0x92, 0x58, 0x1a, 0x1b, 0xd9,
        0x5c, 0x1e, 0x1f, 0xdd, 0x97, 0xd6, 0xd4, 0x15
    };
    uint64_t z = 0;
    uint64_t i = 0;
    for (m = 2 * m; m >= 6;) {
        m -= 6;
        uint8_t j = HILBERT_INVERSE_LUT_3[i | ((h >> m) & 0x3f)];
        z = (z << 6) | (j & 0x3f);
        i = j & 0xc0;
    }
    if (m != 0) {
        // m = 2 or 4
        int r = 6 - m;
        uint8_t j = HILBERT_INVERSE_LUT_3[i | ((h << r) & 0x3f)];
        z = (z << m) | ((j & 0x3f) >> r);
    }
    return z;
}

/// `hilbertIndex` returns the index of (x, y) in a 2-D Hilbert curve.
///
/// Only the m least significant bits of x and y are used. Computing
/// the Hilbert index of a point has been measured to take 4 to 15
/// times as long as computing its Morton index on an Intel Core i7-3820QM
/// CPU. With Xcode 7.3 and -O3, latency is ~19ns per call at a CPU
/// frequency of 3.5 GHz.
inline uint64_t hilbertIndex(uint32_t x, uint32_t y, int m) {
    return mortonToHilbert(mortonIndex(x, y), m);
}

/// `hilbertIndexInverse` returns the point (x, y) with Hilbert index h,
/// where x and y are m bit integers.
inline std::tuple<uint32_t, uint32_t> hilbertIndexInverse(uint64_t h, int m) {
    return mortonIndexInverse(hilbertToMorton(h, m));
}

}} // namespace lsst::sphgeom

#endif // LSST_SPHGEOM_CURVE_H_
