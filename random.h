// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2026 The bitcoin-random contributors
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://opensource.org/license/mit.

#ifndef BITCOIN_RANDOM_STANDALONE_RANDOM_H
#define BITCOIN_RANDOM_STANDALONE_RANDOM_H

#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace bitcoin_random {

using Hash256 = std::array<std::byte, 32>;

void RandomInit();
void RandAddPeriodic() noexcept;
void RandAddEvent(uint32_t event_info) noexcept;

void GetRandBytes(std::span<std::byte> bytes) noexcept;
void GetRandBytes(std::span<unsigned char> bytes) noexcept;
void GetStrongRandBytes(std::span<std::byte> bytes) noexcept;
void GetStrongRandBytes(std::span<unsigned char> bytes) noexcept;

double MakeExponentiallyDistributed(uint64_t uniform) noexcept;

template <typename T>
class RandomMixin;

template <typename T>
concept RandomNumberGenerator = requires(T& rng) {
    { rng.rand64() } noexcept -> std::same_as<uint64_t>;
    requires std::derived_from<std::remove_reference_t<T>, RandomMixin<std::remove_reference_t<T>>>;
};

template <typename T>
concept StdChronoDuration = requires {
    []<class Rep, class Period>(std::type_identity<std::chrono::duration<Rep, Period>>) {}(
        std::type_identity<T>());
};

template <typename T>
concept BasicByte = std::same_as<std::remove_cv_t<T>, std::byte> ||
                    std::same_as<std::remove_cv_t<T>, unsigned char>;

template <typename T>
class RandomMixin
{
private:
    uint64_t m_bitbuf{0};
    int m_bitbuf_size{0};

    RandomNumberGenerator auto& Impl() noexcept { return static_cast<T&>(*this); }

protected:
    constexpr void FlushCache() noexcept
    {
        m_bitbuf = 0;
        m_bitbuf_size = 0;
    }

public:
    constexpr RandomMixin() noexcept = default;

    RandomMixin(const RandomMixin&) = delete;
    RandomMixin& operator=(const RandomMixin&) = delete;
    RandomMixin(RandomMixin&&) = delete;
    RandomMixin& operator=(RandomMixin&&) = delete;

    uint64_t randbits(int bits) noexcept
    {
        assert(bits >= 0 && bits <= 64);
        if (bits == 64) return Impl().rand64();

        uint64_t ret;
        if (bits <= m_bitbuf_size) {
            ret = m_bitbuf;
            m_bitbuf >>= bits;
            m_bitbuf_size -= bits;
        } else {
            const uint64_t gen = Impl().rand64();
            ret = (gen << m_bitbuf_size) | m_bitbuf;
            m_bitbuf = gen >> (bits - m_bitbuf_size);
            m_bitbuf_size = 64 + m_bitbuf_size - bits;
        }

        return ret & ((uint64_t{1} << bits) - 1);
    }

    template <int Bits>
    uint64_t randbits() noexcept
    {
        static_assert(Bits >= 0 && Bits <= 64);
        if constexpr (Bits == 64) {
            return Impl().rand64();
        } else {
            uint64_t ret;
            if (Bits <= m_bitbuf_size) {
                ret = m_bitbuf;
                m_bitbuf >>= Bits;
                m_bitbuf_size -= Bits;
            } else {
                const uint64_t gen = Impl().rand64();
                ret = (gen << m_bitbuf_size) | m_bitbuf;
                m_bitbuf = gen >> (Bits - m_bitbuf_size);
                m_bitbuf_size = 64 + m_bitbuf_size - Bits;
            }
            constexpr uint64_t mask = (uint64_t{1} << Bits) - 1;
            return ret & mask;
        }
    }

    template <std::integral I>
    I randrange(I range) noexcept
    {
        static_assert(std::numeric_limits<I>::max() <= std::numeric_limits<uint64_t>::max());
        assert(range > 0);
        const uint64_t maxval = static_cast<uint64_t>(range - 1);
        const int bits = std::bit_width(maxval);
        while (true) {
            const uint64_t ret = Impl().randbits(bits);
            if (ret <= maxval) return static_cast<I>(ret);
        }
    }

    void fillrand(std::span<std::byte> span) noexcept
    {
        while (span.size() >= 8) {
            const uint64_t gen = Impl().rand64();
            for (int i = 0; i < 8; ++i) {
                span[static_cast<size_t>(i)] = static_cast<std::byte>((gen >> (i * 8)) & 0xffU);
            }
            span = span.subspan(8);
        }
        if (span.size() >= 4) {
            const uint32_t gen = Impl().rand32();
            for (int i = 0; i < 4; ++i) {
                span[static_cast<size_t>(i)] = static_cast<std::byte>((gen >> (i * 8)) & 0xffU);
            }
            span = span.subspan(4);
        }
        while (!span.empty()) {
            span[0] = static_cast<std::byte>(Impl().template randbits<8>());
            span = span.subspan(1);
        }
    }

    template <BasicByte B = unsigned char>
    std::vector<B> randbytes(size_t len) noexcept
    {
        std::vector<B> ret(len);
        Impl().fillrand(std::as_writable_bytes(std::span<B>(ret)));
        return ret;
    }

    uint32_t rand32() noexcept { return static_cast<uint32_t>(Impl().template randbits<32>()); }

    Hash256 rand256() noexcept
    {
        Hash256 ret{};
        Impl().fillrand(std::span<std::byte>(ret.data(), ret.size()));
        return ret;
    }

    bool randbool() noexcept { return Impl().template randbits<1>() != 0; }

    template <typename Tp>
    Tp rand_uniform_delay(const Tp& time, typename Tp::duration range) noexcept
    {
        return time + Impl().template rand_uniform_duration<Tp>(range);
    }

    template <typename Chrono>
    requires StdChronoDuration<typename Chrono::duration>
    typename Chrono::duration rand_uniform_duration(typename Chrono::duration range) noexcept
    {
        using Dur = typename Chrono::duration;
        return range.count() > 0 ? Dur{Impl().randrange(range.count())} :
               range.count() < 0 ? -Dur{Impl().randrange(-range.count())} :
                                   Dur{0};
    }

    template <StdChronoDuration Dur>
    Dur randrange(typename std::common_type_t<Dur> range) noexcept
    {
        return Dur{Impl().randrange(range.count())};
    }

    std::chrono::microseconds rand_exp_duration(std::chrono::microseconds mean) noexcept
    {
        using namespace std::chrono_literals;
        const auto unscaled = MakeExponentiallyDistributed(Impl().rand64());
        return std::chrono::duration_cast<std::chrono::microseconds>(unscaled * mean + 0.5us);
    }

    using result_type = uint64_t;
    static constexpr uint64_t min() noexcept { return 0; }
    static constexpr uint64_t max() noexcept { return std::numeric_limits<uint64_t>::max(); }
    uint64_t operator()() noexcept { return Impl().rand64(); }
};

class FastRandomContext : public RandomMixin<FastRandomContext>
{
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void RandomSeed() noexcept;

public:
    explicit FastRandomContext(bool deterministic = false) noexcept;
    explicit FastRandomContext(const Hash256& seed) noexcept;
    ~FastRandomContext();

    FastRandomContext(const FastRandomContext&) = delete;
    FastRandomContext& operator=(const FastRandomContext&) = delete;
    FastRandomContext(FastRandomContext&&) = delete;
    FastRandomContext& operator=(FastRandomContext&&) = delete;

    void Reseed(const Hash256& seed) noexcept;
    uint64_t rand64() noexcept;
    void fillrand(std::span<std::byte> output) noexcept;
    void fillrand(std::span<unsigned char> output) noexcept
    {
        fillrand(std::as_writable_bytes(output));
    }
};

class InsecureRandomContext : public RandomMixin<InsecureRandomContext>
{
private:
    uint64_t m_s0;
    uint64_t m_s1;

    [[nodiscard]] constexpr static uint64_t SplitMix64(uint64_t& seedval) noexcept
    {
        uint64_t z = (seedval += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

public:
    constexpr explicit InsecureRandomContext(uint64_t seedval) noexcept
        : m_s0(SplitMix64(seedval)), m_s1(SplitMix64(seedval))
    {
    }

    constexpr void Reseed(uint64_t seedval) noexcept
    {
        FlushCache();
        m_s0 = SplitMix64(seedval);
        m_s1 = SplitMix64(seedval);
    }

    constexpr uint64_t rand64() noexcept
    {
        const uint64_t s0 = m_s0;
        uint64_t s1 = m_s1;
        const uint64_t result = std::rotl(s0 + s1, 17) + s0;
        s1 ^= s0;
        m_s0 = std::rotl(s0, 49) ^ s1 ^ (s1 << 21);
        m_s1 = std::rotl(s1, 28);
        return result;
    }
};

inline Hash256 GetRandHash() noexcept
{
    Hash256 hash{};
    GetRandBytes(std::span<std::byte>(hash.data(), hash.size()));
    return hash;
}

bool Random_SanityCheck();

} // namespace bitcoin_random

#endif // BITCOIN_RANDOM_STANDALONE_RANDOM_H
