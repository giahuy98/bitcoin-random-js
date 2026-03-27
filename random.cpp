// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2026 The bitcoin-random contributors
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://opensource.org/license/mit.

#include "random.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#ifdef WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if defined(__linux__)
#include <sys/random.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <sys/sysctl.h>
#endif

#if (defined(__x86_64__) || defined(__amd64__) || defined(__i386__)) && !defined(_MSC_VER)
#include <cpuid.h>
#endif

namespace bitcoin_random {
namespace {

using SteadyClock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr size_t NUM_OS_RANDOM_BYTES = 32;

template <typename T>
constexpr T ByteSwap(T value) noexcept
{
    static_assert(std::is_unsigned_v<T>);
    if constexpr (sizeof(T) == 2) {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(value)));
#else
        return static_cast<T>((value << 8) | (value >> 8));
#endif
    } else if constexpr (sizeof(T) == 4) {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(value)));
#else
        return static_cast<T>(((value & 0x000000ffU) << 24) |
                              ((value & 0x0000ff00U) << 8) |
                              ((value & 0x00ff0000U) >> 8) |
                              ((value & 0xff000000U) >> 24));
#endif
    } else {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<T>(__builtin_bswap64(static_cast<uint64_t>(value)));
#else
        return static_cast<T>(((value & 0x00000000000000ffULL) << 56) |
                              ((value & 0x000000000000ff00ULL) << 40) |
                              ((value & 0x0000000000ff0000ULL) << 24) |
                              ((value & 0x00000000ff000000ULL) << 8) |
                              ((value & 0x000000ff00000000ULL) >> 8) |
                              ((value & 0x0000ff0000000000ULL) >> 24) |
                              ((value & 0x00ff000000000000ULL) >> 40) |
                              ((value & 0xff00000000000000ULL) >> 56));
#endif
    }
}

template <typename T>
constexpr T FromLittleEndian(T value) noexcept
{
    if constexpr (std::endian::native == std::endian::little) {
        return value;
    } else {
        return ByteSwap(value);
    }
}

template <typename T>
constexpr T FromBigEndian(T value) noexcept
{
    if constexpr (std::endian::native == std::endian::big) {
        return value;
    } else {
        return ByteSwap(value);
    }
}

template <typename T>
constexpr T ToLittleEndian(T value) noexcept
{
    return FromLittleEndian(value);
}

template <typename T>
constexpr T ToBigEndian(T value) noexcept
{
    return FromBigEndian(value);
}

uint32_t ReadLE32(const unsigned char* ptr) noexcept
{
    uint32_t value;
    std::memcpy(&value, ptr, sizeof(value));
    return FromLittleEndian(value);
}

uint64_t ReadLE64(const unsigned char* ptr) noexcept
{
    uint64_t value;
    std::memcpy(&value, ptr, sizeof(value));
    return FromLittleEndian(value);
}

uint64_t ReadBE64(const unsigned char* ptr) noexcept
{
    uint64_t value;
    std::memcpy(&value, ptr, sizeof(value));
    return FromBigEndian(value);
}

void WriteLE32(unsigned char* ptr, uint32_t value) noexcept
{
    const uint32_t encoded = ToLittleEndian(value);
    std::memcpy(ptr, &encoded, sizeof(encoded));
}

[[maybe_unused]] void WriteLE64(unsigned char* ptr, uint64_t value) noexcept
{
    const uint64_t encoded = ToLittleEndian(value);
    std::memcpy(ptr, &encoded, sizeof(encoded));
}

void WriteBE64(unsigned char* ptr, uint64_t value) noexcept
{
    const uint64_t encoded = ToBigEndian(value);
    std::memcpy(ptr, &encoded, sizeof(encoded));
}

void memory_cleanse(void* ptr, size_t len) noexcept
{
#if defined(WIN32)
    SecureZeroMemory(ptr, len);
#else
    std::memset(ptr, 0, len);
    __asm__ __volatile__("" : : "r"(ptr) : "memory");
#endif
}

[[noreturn]] void RandFailure()
{
    std::fputs("bitcoin-random: failed to read randomness\n", stderr);
    std::abort();
}

inline int64_t GetPerformanceCounter() noexcept
{
#if !defined(_MSC_VER) && defined(__i386__)
    uint64_t r = 0;
    __asm__ volatile("rdtsc" : "=A"(r));
    return static_cast<int64_t>(r);
#elif !defined(_MSC_VER) && (defined(__x86_64__) || defined(__amd64__))
    uint64_t r1 = 0;
    uint64_t r2 = 0;
    __asm__ volatile("rdtsc" : "=a"(r1), "=d"(r2));
    return static_cast<int64_t>((r2 << 32) | r1);
#else
    return std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
}

class CSHA512
{
private:
    uint64_t m_state[8];
    unsigned char m_buffer[128];
    uint64_t m_bytes{0};

public:
    static constexpr size_t OUTPUT_SIZE = 64;

    CSHA512();
    CSHA512& Write(const unsigned char* data, size_t len);
    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    CSHA512& Reset();
    uint64_t Size() const noexcept { return m_bytes; }
};

namespace sha512 {

uint64_t inline Ch(uint64_t x, uint64_t y, uint64_t z) { return z ^ (x & (y ^ z)); }
uint64_t inline Maj(uint64_t x, uint64_t y, uint64_t z) { return (x & y) | (z & (x | y)); }
uint64_t inline Sigma0(uint64_t x) { return (x >> 28 | x << 36) ^ (x >> 34 | x << 30) ^ (x >> 39 | x << 25); }
uint64_t inline Sigma1(uint64_t x) { return (x >> 14 | x << 50) ^ (x >> 18 | x << 46) ^ (x >> 41 | x << 23); }
uint64_t inline sigma0(uint64_t x) { return (x >> 1 | x << 63) ^ (x >> 8 | x << 56) ^ (x >> 7); }
uint64_t inline sigma1(uint64_t x) { return (x >> 19 | x << 45) ^ (x >> 61 | x << 3) ^ (x >> 6); }

void inline Round(uint64_t a, uint64_t b, uint64_t c, uint64_t& d, uint64_t e, uint64_t f,
                  uint64_t g, uint64_t& h, uint64_t k, uint64_t w)
{
    const uint64_t t1 = h + Sigma1(e) + Ch(e, f, g) + k + w;
    const uint64_t t2 = Sigma0(a) + Maj(a, b, c);
    d += t1;
    h = t1 + t2;
}

void inline Initialize(uint64_t* state)
{
    state[0] = 0x6a09e667f3bcc908ULL;
    state[1] = 0xbb67ae8584caa73bULL;
    state[2] = 0x3c6ef372fe94f82bULL;
    state[3] = 0xa54ff53a5f1d36f1ULL;
    state[4] = 0x510e527fade682d1ULL;
    state[5] = 0x9b05688c2b3e6c1fULL;
    state[6] = 0x1f83d9abfb41bd6bULL;
    state[7] = 0x5be0cd19137e2179ULL;
}

void Transform(uint64_t* state, const unsigned char* chunk)
{
    uint64_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint64_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint64_t w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;

    Round(a, b, c, d, e, f, g, h, 0x428a2f98d728ae22ULL, w0 = ReadBE64(chunk + 0));
    Round(h, a, b, c, d, e, f, g, 0x7137449123ef65cdULL, w1 = ReadBE64(chunk + 8));
    Round(g, h, a, b, c, d, e, f, 0xb5c0fbcfec4d3b2fULL, w2 = ReadBE64(chunk + 16));
    Round(f, g, h, a, b, c, d, e, 0xe9b5dba58189dbbcULL, w3 = ReadBE64(chunk + 24));
    Round(e, f, g, h, a, b, c, d, 0x3956c25bf348b538ULL, w4 = ReadBE64(chunk + 32));
    Round(d, e, f, g, h, a, b, c, 0x59f111f1b605d019ULL, w5 = ReadBE64(chunk + 40));
    Round(c, d, e, f, g, h, a, b, 0x923f82a4af194f9bULL, w6 = ReadBE64(chunk + 48));
    Round(b, c, d, e, f, g, h, a, 0xab1c5ed5da6d8118ULL, w7 = ReadBE64(chunk + 56));
    Round(a, b, c, d, e, f, g, h, 0xd807aa98a3030242ULL, w8 = ReadBE64(chunk + 64));
    Round(h, a, b, c, d, e, f, g, 0x12835b0145706fbeULL, w9 = ReadBE64(chunk + 72));
    Round(g, h, a, b, c, d, e, f, 0x243185be4ee4b28cULL, w10 = ReadBE64(chunk + 80));
    Round(f, g, h, a, b, c, d, e, 0x550c7dc3d5ffb4e2ULL, w11 = ReadBE64(chunk + 88));
    Round(e, f, g, h, a, b, c, d, 0x72be5d74f27b896fULL, w12 = ReadBE64(chunk + 96));
    Round(d, e, f, g, h, a, b, c, 0x80deb1fe3b1696b1ULL, w13 = ReadBE64(chunk + 104));
    Round(c, d, e, f, g, h, a, b, 0x9bdc06a725c71235ULL, w14 = ReadBE64(chunk + 112));
    Round(b, c, d, e, f, g, h, a, 0xc19bf174cf692694ULL, w15 = ReadBE64(chunk + 120));

    Round(a, b, c, d, e, f, g, h, 0xe49b69c19ef14ad2ULL, w0 += sigma1(w14) + w9 + sigma0(w1));
    Round(h, a, b, c, d, e, f, g, 0xefbe4786384f25e3ULL, w1 += sigma1(w15) + w10 + sigma0(w2));
    Round(g, h, a, b, c, d, e, f, 0x0fc19dc68b8cd5b5ULL, w2 += sigma1(w0) + w11 + sigma0(w3));
    Round(f, g, h, a, b, c, d, e, 0x240ca1cc77ac9c65ULL, w3 += sigma1(w1) + w12 + sigma0(w4));
    Round(e, f, g, h, a, b, c, d, 0x2de92c6f592b0275ULL, w4 += sigma1(w2) + w13 + sigma0(w5));
    Round(d, e, f, g, h, a, b, c, 0x4a7484aa6ea6e483ULL, w5 += sigma1(w3) + w14 + sigma0(w6));
    Round(c, d, e, f, g, h, a, b, 0x5cb0a9dcbd41fbd4ULL, w6 += sigma1(w4) + w15 + sigma0(w7));
    Round(b, c, d, e, f, g, h, a, 0x76f988da831153b5ULL, w7 += sigma1(w5) + w0 + sigma0(w8));
    Round(a, b, c, d, e, f, g, h, 0x983e5152ee66dfabULL, w8 += sigma1(w6) + w1 + sigma0(w9));
    Round(h, a, b, c, d, e, f, g, 0xa831c66d2db43210ULL, w9 += sigma1(w7) + w2 + sigma0(w10));
    Round(g, h, a, b, c, d, e, f, 0xb00327c898fb213fULL, w10 += sigma1(w8) + w3 + sigma0(w11));
    Round(f, g, h, a, b, c, d, e, 0xbf597fc7beef0ee4ULL, w11 += sigma1(w9) + w4 + sigma0(w12));
    Round(e, f, g, h, a, b, c, d, 0xc6e00bf33da88fc2ULL, w12 += sigma1(w10) + w5 + sigma0(w13));
    Round(d, e, f, g, h, a, b, c, 0xd5a79147930aa725ULL, w13 += sigma1(w11) + w6 + sigma0(w14));
    Round(c, d, e, f, g, h, a, b, 0x06ca6351e003826fULL, w14 += sigma1(w12) + w7 + sigma0(w15));
    Round(b, c, d, e, f, g, h, a, 0x142929670a0e6e70ULL, w15 += sigma1(w13) + w8 + sigma0(w0));

    Round(a, b, c, d, e, f, g, h, 0x27b70a8546d22ffcULL, w0 += sigma1(w14) + w9 + sigma0(w1));
    Round(h, a, b, c, d, e, f, g, 0x2e1b21385c26c926ULL, w1 += sigma1(w15) + w10 + sigma0(w2));
    Round(g, h, a, b, c, d, e, f, 0x4d2c6dfc5ac42aedULL, w2 += sigma1(w0) + w11 + sigma0(w3));
    Round(f, g, h, a, b, c, d, e, 0x53380d139d95b3dfULL, w3 += sigma1(w1) + w12 + sigma0(w4));
    Round(e, f, g, h, a, b, c, d, 0x650a73548baf63deULL, w4 += sigma1(w2) + w13 + sigma0(w5));
    Round(d, e, f, g, h, a, b, c, 0x766a0abb3c77b2a8ULL, w5 += sigma1(w3) + w14 + sigma0(w6));
    Round(c, d, e, f, g, h, a, b, 0x81c2c92e47edaee6ULL, w6 += sigma1(w4) + w15 + sigma0(w7));
    Round(b, c, d, e, f, g, h, a, 0x92722c851482353bULL, w7 += sigma1(w5) + w0 + sigma0(w8));
    Round(a, b, c, d, e, f, g, h, 0xa2bfe8a14cf10364ULL, w8 += sigma1(w6) + w1 + sigma0(w9));
    Round(h, a, b, c, d, e, f, g, 0xa81a664bbc423001ULL, w9 += sigma1(w7) + w2 + sigma0(w10));
    Round(g, h, a, b, c, d, e, f, 0xc24b8b70d0f89791ULL, w10 += sigma1(w8) + w3 + sigma0(w11));
    Round(f, g, h, a, b, c, d, e, 0xc76c51a30654be30ULL, w11 += sigma1(w9) + w4 + sigma0(w12));
    Round(e, f, g, h, a, b, c, d, 0xd192e819d6ef5218ULL, w12 += sigma1(w10) + w5 + sigma0(w13));
    Round(d, e, f, g, h, a, b, c, 0xd69906245565a910ULL, w13 += sigma1(w11) + w6 + sigma0(w14));
    Round(c, d, e, f, g, h, a, b, 0xf40e35855771202aULL, w14 += sigma1(w12) + w7 + sigma0(w15));
    Round(b, c, d, e, f, g, h, a, 0x106aa07032bbd1b8ULL, w15 += sigma1(w13) + w8 + sigma0(w0));

    Round(a, b, c, d, e, f, g, h, 0x19a4c116b8d2d0c8ULL, w0 += sigma1(w14) + w9 + sigma0(w1));
    Round(h, a, b, c, d, e, f, g, 0x1e376c085141ab53ULL, w1 += sigma1(w15) + w10 + sigma0(w2));
    Round(g, h, a, b, c, d, e, f, 0x2748774cdf8eeb99ULL, w2 += sigma1(w0) + w11 + sigma0(w3));
    Round(f, g, h, a, b, c, d, e, 0x34b0bcb5e19b48a8ULL, w3 += sigma1(w1) + w12 + sigma0(w4));
    Round(e, f, g, h, a, b, c, d, 0x391c0cb3c5c95a63ULL, w4 += sigma1(w2) + w13 + sigma0(w5));
    Round(d, e, f, g, h, a, b, c, 0x4ed8aa4ae3418acbULL, w5 += sigma1(w3) + w14 + sigma0(w6));
    Round(c, d, e, f, g, h, a, b, 0x5b9cca4f7763e373ULL, w6 += sigma1(w4) + w15 + sigma0(w7));
    Round(b, c, d, e, f, g, h, a, 0x682e6ff3d6b2b8a3ULL, w7 += sigma1(w5) + w0 + sigma0(w8));
    Round(a, b, c, d, e, f, g, h, 0x748f82ee5defb2fcULL, w8 += sigma1(w6) + w1 + sigma0(w9));
    Round(h, a, b, c, d, e, f, g, 0x78a5636f43172f60ULL, w9 += sigma1(w7) + w2 + sigma0(w10));
    Round(g, h, a, b, c, d, e, f, 0x84c87814a1f0ab72ULL, w10 += sigma1(w8) + w3 + sigma0(w11));
    Round(f, g, h, a, b, c, d, e, 0x8cc702081a6439ecULL, w11 += sigma1(w9) + w4 + sigma0(w12));
    Round(e, f, g, h, a, b, c, d, 0x90befffa23631e28ULL, w12 += sigma1(w10) + w5 + sigma0(w13));
    Round(d, e, f, g, h, a, b, c, 0xa4506cebde82bde9ULL, w13 += sigma1(w11) + w6 + sigma0(w14));
    Round(c, d, e, f, g, h, a, b, 0xbef9a3f7b2c67915ULL, w14 += sigma1(w12) + w7 + sigma0(w15));
    Round(b, c, d, e, f, g, h, a, 0xc67178f2e372532bULL, w15 += sigma1(w13) + w8 + sigma0(w0));

    Round(a, b, c, d, e, f, g, h, 0xca273eceea26619cULL, w0 += sigma1(w14) + w9 + sigma0(w1));
    Round(h, a, b, c, d, e, f, g, 0xd186b8c721c0c207ULL, w1 += sigma1(w15) + w10 + sigma0(w2));
    Round(g, h, a, b, c, d, e, f, 0xeada7dd6cde0eb1eULL, w2 += sigma1(w0) + w11 + sigma0(w3));
    Round(f, g, h, a, b, c, d, e, 0xf57d4f7fee6ed178ULL, w3 += sigma1(w1) + w12 + sigma0(w4));
    Round(e, f, g, h, a, b, c, d, 0x06f067aa72176fbaULL, w4 += sigma1(w2) + w13 + sigma0(w5));
    Round(d, e, f, g, h, a, b, c, 0x0a637dc5a2c898a6ULL, w5 += sigma1(w3) + w14 + sigma0(w6));
    Round(c, d, e, f, g, h, a, b, 0x113f9804bef90daeULL, w6 += sigma1(w4) + w15 + sigma0(w7));
    Round(b, c, d, e, f, g, h, a, 0x1b710b35131c471bULL, w7 += sigma1(w5) + w0 + sigma0(w8));
    Round(a, b, c, d, e, f, g, h, 0x28db77f523047d84ULL, w8 += sigma1(w6) + w1 + sigma0(w9));
    Round(h, a, b, c, d, e, f, g, 0x32caab7b40c72493ULL, w9 += sigma1(w7) + w2 + sigma0(w10));
    Round(g, h, a, b, c, d, e, f, 0x3c9ebe0a15c9bebcULL, w10 += sigma1(w8) + w3 + sigma0(w11));
    Round(f, g, h, a, b, c, d, e, 0x431d67c49c100d4cULL, w11 += sigma1(w9) + w4 + sigma0(w12));
    Round(e, f, g, h, a, b, c, d, 0x4cc5d4becb3e42b6ULL, w12 += sigma1(w10) + w5 + sigma0(w13));
    Round(d, e, f, g, h, a, b, c, 0x597f299cfc657e2aULL, w13 += sigma1(w11) + w6 + sigma0(w14));
    Round(c, d, e, f, g, h, a, b, 0x5fcb6fab3ad6faecULL, w14 += sigma1(w12) + w7 + sigma0(w15));
    Round(b, c, d, e, f, g, h, a, 0x6c44198c4a475817ULL, w15 += sigma1(w13) + w8 + sigma0(w0));

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace sha512

CSHA512::CSHA512()
{
    sha512::Initialize(m_state);
}

CSHA512& CSHA512::Write(const unsigned char* data, size_t len)
{
    const unsigned char* end = data + len;
    size_t buffer_size = m_bytes % 128;
    if (buffer_size && buffer_size + len >= 128) {
        std::memcpy(m_buffer + buffer_size, data, 128 - buffer_size);
        m_bytes += 128 - buffer_size;
        data += 128 - buffer_size;
        sha512::Transform(m_state, m_buffer);
        buffer_size = 0;
    }
    while (end - data >= 128) {
        sha512::Transform(m_state, data);
        data += 128;
        m_bytes += 128;
    }
    if (end > data) {
        std::memcpy(m_buffer + buffer_size, data, static_cast<size_t>(end - data));
        m_bytes += static_cast<uint64_t>(end - data);
    }
    return *this;
}

void CSHA512::Finalize(unsigned char hash[OUTPUT_SIZE])
{
    static const unsigned char pad[128] = {0x80};
    unsigned char size_desc[16] = {0};
    WriteBE64(size_desc + 8, m_bytes << 3);
    Write(pad, 1 + ((239 - (m_bytes % 128)) % 128));
    Write(size_desc, 16);
    WriteBE64(hash + 0, m_state[0]);
    WriteBE64(hash + 8, m_state[1]);
    WriteBE64(hash + 16, m_state[2]);
    WriteBE64(hash + 24, m_state[3]);
    WriteBE64(hash + 32, m_state[4]);
    WriteBE64(hash + 40, m_state[5]);
    WriteBE64(hash + 48, m_state[6]);
    WriteBE64(hash + 56, m_state[7]);
}

CSHA512& CSHA512::Reset()
{
    m_bytes = 0;
    sha512::Initialize(m_state);
    return *this;
}

class ChaCha20Aligned
{
private:
    uint32_t m_input[12];

public:
    static constexpr unsigned KEYLEN = 32;
    static constexpr unsigned BLOCKLEN = 64;
    using Nonce96 = std::pair<uint32_t, uint64_t>;

    explicit ChaCha20Aligned(std::span<const std::byte> key) noexcept;
    ~ChaCha20Aligned();

    void SetKey(std::span<const std::byte> key) noexcept;
    void Seek(Nonce96 nonce, uint32_t block_counter) noexcept;
    void Keystream(std::span<std::byte> output) noexcept;
};

class ChaCha20
{
private:
    ChaCha20Aligned m_aligned;
    std::array<std::byte, ChaCha20Aligned::BLOCKLEN> m_buffer{};
    unsigned m_bufleft{0};

public:
    static constexpr unsigned KEYLEN = ChaCha20Aligned::KEYLEN;
    using Nonce96 = ChaCha20Aligned::Nonce96;

    explicit ChaCha20(std::span<const std::byte> key) noexcept : m_aligned(key) {}
    ~ChaCha20();

    void SetKey(std::span<const std::byte> key) noexcept;
    void Seek(Nonce96 nonce, uint32_t block_counter) noexcept
    {
        m_aligned.Seek(nonce, block_counter);
        m_bufleft = 0;
    }
    void Keystream(std::span<std::byte> output) noexcept;
};

#define QUARTERROUND(a, b, c, d) \
    a += b; d = std::rotl(d ^ a, 16); \
    c += d; b = std::rotl(b ^ c, 12); \
    a += b; d = std::rotl(d ^ a, 8); \
    c += d; b = std::rotl(b ^ c, 7);

#define REPEAT10(a) do { {a}; {a}; {a}; {a}; {a}; {a}; {a}; {a}; {a}; {a}; } while (0)

ChaCha20Aligned::ChaCha20Aligned(std::span<const std::byte> key) noexcept
{
    SetKey(key);
}

ChaCha20Aligned::~ChaCha20Aligned()
{
    memory_cleanse(m_input, sizeof(m_input));
}

void ChaCha20Aligned::SetKey(std::span<const std::byte> key) noexcept
{
    assert(key.size() == KEYLEN);
    const auto* bytes = reinterpret_cast<const unsigned char*>(key.data());
    m_input[0] = ReadLE32(bytes + 0);
    m_input[1] = ReadLE32(bytes + 4);
    m_input[2] = ReadLE32(bytes + 8);
    m_input[3] = ReadLE32(bytes + 12);
    m_input[4] = ReadLE32(bytes + 16);
    m_input[5] = ReadLE32(bytes + 20);
    m_input[6] = ReadLE32(bytes + 24);
    m_input[7] = ReadLE32(bytes + 28);
    m_input[8] = 0;
    m_input[9] = 0;
    m_input[10] = 0;
    m_input[11] = 0;
}

void ChaCha20Aligned::Seek(Nonce96 nonce, uint32_t block_counter) noexcept
{
    m_input[8] = block_counter;
    m_input[9] = nonce.first;
    m_input[10] = static_cast<uint32_t>(nonce.second);
    m_input[11] = static_cast<uint32_t>(nonce.second >> 32);
}

void ChaCha20Aligned::Keystream(std::span<std::byte> output) noexcept
{
    unsigned char* out = reinterpret_cast<unsigned char*>(output.data());
    size_t blocks = output.size() / BLOCKLEN;
    assert(blocks * BLOCKLEN == output.size());
    if (blocks == 0) return;

    uint32_t x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15;
    uint32_t j4 = m_input[0], j5 = m_input[1], j6 = m_input[2], j7 = m_input[3];
    uint32_t j8 = m_input[4], j9 = m_input[5], j10 = m_input[6], j11 = m_input[7];
    uint32_t j12 = m_input[8], j13 = m_input[9], j14 = m_input[10], j15 = m_input[11];

    for (;;) {
        x0 = 0x61707865;
        x1 = 0x3320646e;
        x2 = 0x79622d32;
        x3 = 0x6b206574;
        x4 = j4;
        x5 = j5;
        x6 = j6;
        x7 = j7;
        x8 = j8;
        x9 = j9;
        x10 = j10;
        x11 = j11;
        x12 = j12;
        x13 = j13;
        x14 = j14;
        x15 = j15;

        REPEAT10(
            QUARTERROUND(x0, x4, x8, x12);
            QUARTERROUND(x1, x5, x9, x13);
            QUARTERROUND(x2, x6, x10, x14);
            QUARTERROUND(x3, x7, x11, x15);
            QUARTERROUND(x0, x5, x10, x15);
            QUARTERROUND(x1, x6, x11, x12);
            QUARTERROUND(x2, x7, x8, x13);
            QUARTERROUND(x3, x4, x9, x14);
        );

        x0 += 0x61707865;
        x1 += 0x3320646e;
        x2 += 0x79622d32;
        x3 += 0x6b206574;
        x4 += j4;
        x5 += j5;
        x6 += j6;
        x7 += j7;
        x8 += j8;
        x9 += j9;
        x10 += j10;
        x11 += j11;
        x12 += j12;
        x13 += j13;
        x14 += j14;
        x15 += j15;

        ++j12;
        if (!j12) ++j13;

        WriteLE32(out + 0, x0);
        WriteLE32(out + 4, x1);
        WriteLE32(out + 8, x2);
        WriteLE32(out + 12, x3);
        WriteLE32(out + 16, x4);
        WriteLE32(out + 20, x5);
        WriteLE32(out + 24, x6);
        WriteLE32(out + 28, x7);
        WriteLE32(out + 32, x8);
        WriteLE32(out + 36, x9);
        WriteLE32(out + 40, x10);
        WriteLE32(out + 44, x11);
        WriteLE32(out + 48, x12);
        WriteLE32(out + 52, x13);
        WriteLE32(out + 56, x14);
        WriteLE32(out + 60, x15);

        if (blocks == 1) {
            m_input[8] = j12;
            m_input[9] = j13;
            return;
        }
        --blocks;
        out += BLOCKLEN;
    }
}

ChaCha20::~ChaCha20()
{
    memory_cleanse(m_buffer.data(), m_buffer.size());
}

void ChaCha20::SetKey(std::span<const std::byte> key) noexcept
{
    m_aligned.SetKey(key);
    m_bufleft = 0;
    memory_cleanse(m_buffer.data(), m_buffer.size());
}

void ChaCha20::Keystream(std::span<std::byte> output) noexcept
{
    if (output.empty()) return;

    if (m_bufleft) {
        const unsigned reuse = std::min<size_t>(m_bufleft, output.size());
        std::copy(m_buffer.end() - m_bufleft, m_buffer.end() - m_bufleft + reuse, output.begin());
        m_bufleft -= reuse;
        output = output.subspan(reuse);
    }

    if (output.size() >= m_aligned.BLOCKLEN) {
        const size_t blocks = output.size() / m_aligned.BLOCKLEN;
        m_aligned.Keystream(output.first(blocks * m_aligned.BLOCKLEN));
        output = output.subspan(blocks * m_aligned.BLOCKLEN);
    }

    if (!output.empty()) {
        m_aligned.Keystream(m_buffer);
        std::copy(m_buffer.begin(), m_buffer.begin() + output.size(), output.begin());
        m_bufleft = m_aligned.BLOCKLEN - output.size();
    }
}

#undef REPEAT10
#undef QUARTERROUND

#if (defined(__x86_64__) || defined(__amd64__) || defined(__i386__)) && !defined(_MSC_VER)
bool g_rdrand_supported = false;
bool g_rdseed_supported = false;
constexpr uint32_t CPUID_F1_ECX_RDRAND = 0x40000000U;
constexpr uint32_t CPUID_F7_EBX_RDSEED = 0x00040000U;

void GetCPUID(uint32_t leaf, uint32_t subleaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d)
{
    __cpuid_count(leaf, subleaf, a, b, c, d);
}

void InitHardwareRand()
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    GetCPUID(1, 0, eax, ebx, ecx, edx);
    g_rdrand_supported = (ecx & CPUID_F1_ECX_RDRAND) != 0;
    GetCPUID(7, 0, eax, ebx, ecx, edx);
    g_rdseed_supported = (ebx & CPUID_F7_EBX_RDSEED) != 0;
}

uint64_t GetRdRand() noexcept
{
#if defined(__i386__)
    uint8_t ok = 0;
    uint32_t r1 = 0;
    uint32_t r2 = 0;
    for (int i = 0; i < 10; ++i) {
        __asm__ volatile(".byte 0x0f, 0xc7, 0xf0; setc %1" : "=a"(r1), "=q"(ok) :: "cc");
        if (ok) break;
    }
    for (int i = 0; i < 10; ++i) {
        __asm__ volatile(".byte 0x0f, 0xc7, 0xf0; setc %1" : "=a"(r2), "=q"(ok) :: "cc");
        if (ok) break;
    }
    return (static_cast<uint64_t>(r2) << 32) | r1;
#else
    uint8_t ok = 0;
    uint64_t r1 = 0;
    for (int i = 0; i < 10; ++i) {
        __asm__ volatile(".byte 0x48, 0x0f, 0xc7, 0xf0; setc %1" : "=a"(r1), "=q"(ok) :: "cc");
        if (ok) break;
    }
    return r1;
#endif
}

uint64_t GetRdSeed() noexcept
{
#if defined(__i386__)
    uint8_t ok = 0;
    uint32_t r1 = 0;
    uint32_t r2 = 0;
    do {
        __asm__ volatile(".byte 0x0f, 0xc7, 0xf8; setc %1" : "=a"(r1), "=q"(ok) :: "cc");
        if (ok) break;
        __asm__ volatile("pause");
    } while (true);
    do {
        __asm__ volatile(".byte 0x0f, 0xc7, 0xf8; setc %1" : "=a"(r2), "=q"(ok) :: "cc");
        if (ok) break;
        __asm__ volatile("pause");
    } while (true);
    return (static_cast<uint64_t>(r2) << 32) | r1;
#else
    uint8_t ok = 0;
    uint64_t r1 = 0;
    do {
        __asm__ volatile(".byte 0x48, 0x0f, 0xc7, 0xf8; setc %1" : "=a"(r1), "=q"(ok) :: "cc");
        if (ok) break;
        __asm__ volatile("pause");
    } while (true);
    return r1;
#endif
}

void SeedHardwareFast(CSHA512& hasher) noexcept
{
    if (g_rdrand_supported) {
        const uint64_t out = GetRdRand();
        hasher.Write(reinterpret_cast<const unsigned char*>(&out), sizeof(out));
    }
}

void SeedHardwareSlow(CSHA512& hasher) noexcept
{
    if (g_rdseed_supported) {
        for (int i = 0; i < 4; ++i) {
            const uint64_t out = GetRdSeed();
            hasher.Write(reinterpret_cast<const unsigned char*>(&out), sizeof(out));
        }
        return;
    }
    if (g_rdrand_supported) {
        for (int i = 0; i < 4; ++i) {
            uint64_t out = 0;
            for (int j = 0; j < 1024; ++j) out ^= GetRdRand();
            hasher.Write(reinterpret_cast<const unsigned char*>(&out), sizeof(out));
        }
    }
}
#else
void InitHardwareRand() {}
void SeedHardwareFast(CSHA512&) noexcept {}
void SeedHardwareSlow(CSHA512&) noexcept {}
#endif

template <typename T>
void AddToHasher(CSHA512& hasher, const T& data)
{
    hasher.Write(reinterpret_cast<const unsigned char*>(&data), sizeof(data));
}

void RandAddDynamicEnv(CSHA512& hasher)
{
    AddToHasher(hasher, std::chrono::system_clock::now().time_since_epoch().count());
    AddToHasher(hasher, std::chrono::steady_clock::now().time_since_epoch().count());
    AddToHasher(hasher, std::chrono::high_resolution_clock::now().time_since_epoch().count());
    AddToHasher(hasher, std::hash<std::thread::id>{}(std::this_thread::get_id()));

    int local_marker = 0;
    AddToHasher(hasher, reinterpret_cast<std::uintptr_t>(&local_marker));

#ifdef WIN32
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    AddToHasher(hasher, time);
#else
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    AddToHasher(hasher, tv);

#ifdef CLOCK_MONOTONIC
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    AddToHasher(hasher, ts);
#endif
#ifdef CLOCK_REALTIME
    struct timespec realtime_ts = {};
    clock_gettime(CLOCK_REALTIME, &realtime_ts);
    AddToHasher(hasher, realtime_ts);
#endif

    const pid_t pid = getpid();
    const pid_t ppid = getppid();
    AddToHasher(hasher, pid);
    AddToHasher(hasher, ppid);

    struct rusage usage = {};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        AddToHasher(hasher, usage);
    }
#endif
}

void RandAddStaticEnv(CSHA512& hasher)
{
    const auto concurrency = std::thread::hardware_concurrency();
    AddToHasher(hasher, concurrency);
    AddToHasher(hasher, static_cast<uint64_t>(__cplusplus));
    AddToHasher(hasher, sizeof(void*));

#ifdef __VERSION__
    hasher.Write(reinterpret_cast<const unsigned char*>(__VERSION__), std::strlen(__VERSION__));
#endif

#ifdef WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    AddToHasher(hasher, info);
#else
    struct utsname info = {};
    if (uname(&info) == 0) {
        AddToHasher(hasher, info);
    }

    const long page_size = sysconf(_SC_PAGESIZE);
    const long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (page_size != -1) AddToHasher(hasher, page_size);
    if (cpu_count != -1) AddToHasher(hasher, cpu_count);
#endif

#if (defined(__x86_64__) || defined(__amd64__) || defined(__i386__)) && !defined(_MSC_VER)
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    GetCPUID(0, 0, eax, ebx, ecx, edx);
    AddToHasher(hasher, eax);
    AddToHasher(hasher, ebx);
    AddToHasher(hasher, ecx);
    AddToHasher(hasher, edx);
    GetCPUID(1, 0, eax, ebx, ecx, edx);
    AddToHasher(hasher, eax);
    AddToHasher(hasher, ebx);
    AddToHasher(hasher, ecx);
    AddToHasher(hasher, edx);
    GetCPUID(7, 0, eax, ebx, ecx, edx);
    AddToHasher(hasher, eax);
    AddToHasher(hasher, ebx);
    AddToHasher(hasher, ecx);
    AddToHasher(hasher, edx);
#endif
}

void Strengthen(const unsigned char (&seed)[32], SteadyClock::duration dur, CSHA512& hasher) noexcept
{
    CSHA512 inner_hasher;
    inner_hasher.Write(seed, sizeof(seed));

    unsigned char buffer[64];
    const auto stop = SteadyClock::now() + dur;
    do {
        for (int i = 0; i < 1000; ++i) {
            inner_hasher.Finalize(buffer);
            inner_hasher.Reset();
            inner_hasher.Write(buffer, sizeof(buffer));
        }
        const int64_t perf = GetPerformanceCounter();
        hasher.Write(reinterpret_cast<const unsigned char*>(&perf), sizeof(perf));
    } while (SteadyClock::now() < stop);

    inner_hasher.Finalize(buffer);
    hasher.Write(buffer, sizeof(buffer));
    inner_hasher.Reset();
    memory_cleanse(buffer, sizeof(buffer));
}

#ifndef WIN32
[[maybe_unused]] void GetDevURandom(unsigned char* output)
{
    const int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) RandFailure();

    int have = 0;
    while (have < static_cast<int>(NUM_OS_RANDOM_BYTES)) {
        const ssize_t n = read(fd, output + have, NUM_OS_RANDOM_BYTES - have);
        if (n <= 0 || have + n > static_cast<ssize_t>(NUM_OS_RANDOM_BYTES)) {
            close(fd);
            RandFailure();
        }
        have += static_cast<int>(n);
    }
    close(fd);
}
#endif

void GetOSRand(unsigned char* output)
{
#ifdef WIN32
    HCRYPTPROV provider;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        RandFailure();
    }
    if (!CryptGenRandom(provider, NUM_OS_RANDOM_BYTES, output)) {
        CryptReleaseContext(provider, 0);
        RandFailure();
    }
    CryptReleaseContext(provider, 0);
#elif defined(__linux__)
    int have = 0;
    while (have < static_cast<int>(NUM_OS_RANDOM_BYTES)) {
        const ssize_t n = getrandom(output + have, NUM_OS_RANDOM_BYTES - have, 0);
        if (n <= 0 || have + n > static_cast<ssize_t>(NUM_OS_RANDOM_BYTES)) {
            RandFailure();
        }
        have += static_cast<int>(n);
    }
#elif defined(__OpenBSD__)
    arc4random_buf(output, NUM_OS_RANDOM_BYTES);
#elif defined(__APPLE__)
    if (getentropy(output, NUM_OS_RANDOM_BYTES) != 0) RandFailure();
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    int name[2] = {CTL_KERN, KERN_ARND};
    int have = 0;
    do {
        size_t len = NUM_OS_RANDOM_BYTES - have;
        if (sysctl(name, 2, output + have, &len, nullptr, 0) != 0) RandFailure();
        have += static_cast<int>(len);
    } while (have < static_cast<int>(NUM_OS_RANDOM_BYTES));
#else
    GetDevURandom(output);
#endif
}

class RNGState
{
private:
    std::mutex m_mutex;
    unsigned char m_state[32]{};
    uint64_t m_counter{0};
    bool m_strongly_seeded{false};
    std::optional<ChaCha20> m_deterministic_prng;

    std::mutex m_events_mutex;
    CSHA512 m_events_hasher;

public:
    RNGState() noexcept
    {
        InitHardwareRand();
    }

    void AddEvent(uint32_t event_info) noexcept
    {
        std::lock_guard lock(m_events_mutex);
        m_events_hasher.Write(reinterpret_cast<const unsigned char*>(&event_info), sizeof(event_info));
        const uint32_t perfcounter = static_cast<uint32_t>(GetPerformanceCounter());
        m_events_hasher.Write(reinterpret_cast<const unsigned char*>(&perfcounter), sizeof(perfcounter));
    }

    void SeedEvents(CSHA512& hasher) noexcept
    {
        std::lock_guard lock(m_events_mutex);
        unsigned char events_hash[CSHA512::OUTPUT_SIZE];
        m_events_hasher.Finalize(events_hash);
        hasher.Write(events_hash, sizeof(events_hash));
        m_events_hasher.Reset();
        m_events_hasher.Write(events_hash, sizeof(events_hash));
        memory_cleanse(events_hash, sizeof(events_hash));
    }

    void MakeDeterministic(const Hash256& seed) noexcept
    {
        std::lock_guard lock(m_mutex);
        m_deterministic_prng.emplace(std::span<const std::byte>(seed.data(), seed.size()));
    }

    bool MixExtract(unsigned char* out, size_t num, CSHA512&& hasher, bool strong_seed,
                    bool always_use_real_rng) noexcept
    {
        assert(num <= 32);
        unsigned char buffer[64];
        static_assert(sizeof(buffer) == CSHA512::OUTPUT_SIZE);
        bool ret;

        {
            std::lock_guard lock(m_mutex);
            ret = (m_strongly_seeded |= strong_seed);
            hasher.Write(m_state, sizeof(m_state));
            hasher.Write(reinterpret_cast<const unsigned char*>(&m_counter), sizeof(m_counter));
            ++m_counter;
            hasher.Finalize(buffer);
            std::memcpy(m_state, buffer + 32, 32);

            if (!always_use_real_rng && m_deterministic_prng.has_value()) {
                m_deterministic_prng->Keystream(
                    std::span<std::byte>(reinterpret_cast<std::byte*>(buffer), num));
                ret = true;
            }
        }

        if (num != 0 && out != nullptr) {
            std::memcpy(out, buffer, num);
        }

        hasher.Reset();
        memory_cleanse(buffer, sizeof(buffer));
        return ret;
    }
};

RNGState& GetRNGState() noexcept
{
    static RNGState rng;
    return rng;
}

void SeedTimestamp(CSHA512& hasher) noexcept
{
    const int64_t perfcounter = GetPerformanceCounter();
    hasher.Write(reinterpret_cast<const unsigned char*>(&perfcounter), sizeof(perfcounter));
}

void SeedFast(CSHA512& hasher) noexcept
{
    unsigned char stack_buffer[32];
    const unsigned char* ptr = stack_buffer;
    hasher.Write(reinterpret_cast<const unsigned char*>(&ptr), sizeof(ptr));
    SeedHardwareFast(hasher);
    SeedTimestamp(hasher);
}

void SeedSlow(CSHA512& hasher, RNGState& rng) noexcept
{
    unsigned char buffer[32];
    SeedFast(hasher);
    GetOSRand(buffer);
    hasher.Write(buffer, sizeof(buffer));
    rng.SeedEvents(hasher);
    SeedTimestamp(hasher);
}

void SeedStrengthen(CSHA512& hasher, RNGState& rng, SteadyClock::duration dur) noexcept
{
    unsigned char strengthen_seed[32];
    rng.MixExtract(strengthen_seed, sizeof(strengthen_seed), CSHA512(hasher), false, true);
    Strengthen(strengthen_seed, dur, hasher);
    memory_cleanse(strengthen_seed, sizeof(strengthen_seed));
}

void SeedPeriodic(CSHA512& hasher, RNGState& rng) noexcept
{
    SeedFast(hasher);
    SeedTimestamp(hasher);
    rng.SeedEvents(hasher);
    RandAddDynamicEnv(hasher);
    SeedStrengthen(hasher, rng, 10ms);
}

void SeedStartup(CSHA512& hasher, RNGState& rng) noexcept
{
    SeedHardwareSlow(hasher);
    SeedSlow(hasher, rng);
    RandAddDynamicEnv(hasher);
    RandAddStaticEnv(hasher);
    SeedStrengthen(hasher, rng, 100ms);
}

enum class RNGLevel {
    FAST,
    SLOW,
    PERIODIC,
};

void ProcRand(unsigned char* out, size_t num, RNGLevel level, bool always_use_real_rng) noexcept
{
    assert(num <= 32);
    RNGState& rng = GetRNGState();
    CSHA512 hasher;

    switch (level) {
    case RNGLevel::FAST:
        SeedFast(hasher);
        break;
    case RNGLevel::SLOW:
        SeedSlow(hasher, rng);
        break;
    case RNGLevel::PERIODIC:
        SeedPeriodic(hasher, rng);
        break;
    }

    if (!rng.MixExtract(out, num, std::move(hasher), false, always_use_real_rng)) {
        CSHA512 startup_hasher;
        SeedStartup(startup_hasher, rng);
        rng.MixExtract(out, num, std::move(startup_hasher), true, always_use_real_rng);
    }
}

void GetRandBytesImpl(std::span<std::byte> bytes, RNGLevel level, bool always_use_real_rng) noexcept
{
    while (!bytes.empty()) {
        const size_t chunk = std::min<size_t>(bytes.size(), 32);
        ProcRand(reinterpret_cast<unsigned char*>(bytes.data()), chunk, level, always_use_real_rng);
        bytes = bytes.subspan(chunk);
    }
}

constexpr std::array<std::byte, ChaCha20::KEYLEN> ZERO_KEY{};

} // namespace

struct FastRandomContext::Impl
{
    bool requires_seed;
    ChaCha20 rng;

    explicit Impl(bool deterministic) noexcept
        : requires_seed(!deterministic), rng(std::span<const std::byte>(ZERO_KEY.data(), ZERO_KEY.size()))
    {
    }

    explicit Impl(const Hash256& seed) noexcept
        : requires_seed(false), rng(std::span<const std::byte>(seed.data(), seed.size()))
    {
    }
};

void RandomInit()
{
    ProcRand(nullptr, 0, RNGLevel::FAST, true);
}

void RandAddPeriodic() noexcept
{
    ProcRand(nullptr, 0, RNGLevel::PERIODIC, false);
}

void RandAddEvent(uint32_t event_info) noexcept
{
    GetRNGState().AddEvent(event_info);
}

void GetRandBytes(std::span<std::byte> bytes) noexcept
{
    GetRandBytesImpl(bytes, RNGLevel::FAST, false);
}

void GetRandBytes(std::span<unsigned char> bytes) noexcept
{
    GetRandBytes(std::as_writable_bytes(bytes));
}

void GetStrongRandBytes(std::span<std::byte> bytes) noexcept
{
    GetRandBytesImpl(bytes, RNGLevel::SLOW, true);
}

void GetStrongRandBytes(std::span<unsigned char> bytes) noexcept
{
    GetStrongRandBytes(std::as_writable_bytes(bytes));
}

FastRandomContext::FastRandomContext(bool deterministic) noexcept
    : m_impl(std::make_unique<Impl>(deterministic))
{
}

FastRandomContext::FastRandomContext(const Hash256& seed) noexcept
    : m_impl(std::make_unique<Impl>(seed))
{
}

FastRandomContext::~FastRandomContext() = default;

void FastRandomContext::RandomSeed() noexcept
{
    const Hash256 seed = GetRandHash();
    m_impl->rng.SetKey(std::span<const std::byte>(seed.data(), seed.size()));
    m_impl->requires_seed = false;
}

void FastRandomContext::Reseed(const Hash256& seed) noexcept
{
    FlushCache();
    m_impl->requires_seed = false;
    m_impl->rng.SetKey(std::span<const std::byte>(seed.data(), seed.size()));
}

uint64_t FastRandomContext::rand64() noexcept
{
    if (m_impl->requires_seed) RandomSeed();
    std::array<std::byte, 8> buffer{};
    m_impl->rng.Keystream(std::span<std::byte>(buffer.data(), buffer.size()));
    return ReadLE64(reinterpret_cast<const unsigned char*>(buffer.data()));
}

void FastRandomContext::fillrand(std::span<std::byte> output) noexcept
{
    if (m_impl->requires_seed) RandomSeed();
    m_impl->rng.Keystream(output);
}

bool Random_SanityCheck()
{
    const uint64_t start = static_cast<uint64_t>(GetPerformanceCounter());
    static constexpr int MAX_TRIES = 1024;

    uint8_t data[NUM_OS_RANDOM_BYTES];
    bool overwritten[NUM_OS_RANDOM_BYTES] = {};
    int num_overwritten = 0;
    int tries = 0;

    do {
        std::memset(data, 0, sizeof(data));
        GetOSRand(data);
        for (size_t i = 0; i < NUM_OS_RANDOM_BYTES; ++i) {
            overwritten[i] |= (data[i] != 0);
        }
        num_overwritten = 0;
        for (bool byte_written : overwritten) {
            num_overwritten += byte_written ? 1 : 0;
        }
        ++tries;
    } while (num_overwritten < static_cast<int>(NUM_OS_RANDOM_BYTES) && tries < MAX_TRIES);

    if (num_overwritten != static_cast<int>(NUM_OS_RANDOM_BYTES)) return false;

    std::this_thread::sleep_for(1ms);
    const uint64_t stop = static_cast<uint64_t>(GetPerformanceCounter());
    if (stop == start) return false;

    CSHA512 to_add;
    to_add.Write(reinterpret_cast<const unsigned char*>(&start), sizeof(start));
    to_add.Write(reinterpret_cast<const unsigned char*>(&stop), sizeof(stop));
    GetRNGState().MixExtract(nullptr, 0, std::move(to_add), false, true);
    return true;
}

double MakeExponentiallyDistributed(uint64_t uniform) noexcept
{
    return -std::log1p((uniform >> 11) * -0x1.0p-53);
}

} // namespace bitcoin_random
