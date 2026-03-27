// Copyright (c) 2026 The bitcoin-random contributors
// Distributed under the MIT software license.

#include "random.h"

#include <array>
#include <iomanip>
#include <iostream>

namespace {

void PrintHex(std::span<const std::byte> bytes)
{
    std::ios old_state{nullptr};
    old_state.copyfmt(std::cout);

    for (const std::byte byte : bytes) {
        std::cout << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<int>(std::to_integer<unsigned char>(byte));
    }
    std::cout << '\n';

    std::cout.copyfmt(old_state);
}

} // namespace

int main()
{
    using namespace bitcoin_random;

    std::array<unsigned char, 64> bytes{};
    GetStrongRandBytes(bytes);

    std::cout << "64 strong random bytes: ";
    PrintHex(std::as_bytes(std::span<unsigned char>(bytes)));

    FastRandomContext fast_rng;
    const auto fast_value = fast_rng.rand64();
    std::cout << "FastRandomContext rand64(): " << fast_value << '\n';

    const Hash256 hash = GetRandHash();
    std::cout << "Random hash: ";
    PrintHex(std::span<const std::byte>(hash.data(), hash.size()));

    return 0;
}
