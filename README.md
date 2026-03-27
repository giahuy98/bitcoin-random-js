# bitcoin-random-js

Standalone extraction of Bitcoin Core's random subsystem packaged for Node.js, with the native C++ sources kept alongside the addon.

This folder keeps the public shape of `random.h` familiar, but trims away Bitcoin-specific plumbing such as logging, custom allocators, `uint256`, and the larger build system. The main API remains:

- `bitcoin_random::GetRandBytes`
- `bitcoin_random::GetStrongRandBytes`
- `bitcoin_random::FastRandomContext`
- `bitcoin_random::InsecureRandomContext`
- `bitcoin_random::RandAddEvent`
- `bitcoin_random::RandAddPeriodic`

The main intentional differences from upstream Bitcoin Core are:

- `GetRandHash()` returns `std::array<std::byte, 32>` instead of `uint256`
- environment seeding is reduced to a smaller standalone implementation
- everything is packaged as a self-contained Node addon plus an optional CMake build

## Node.js build

```bash
cd bitcoin-random-js
npm install
```

## Node.js example

```js
const { getStrongRandBytes } = require('./');

const bytes = getStrongRandBytes(32);
console.log(bytes.toString('hex'));
```

Run the bundled example with:

```bash
cd bitcoin-random-js
node example.js
```

## Native C++ build

```bash
cmake -S bitcoin-random-js -B /tmp/bitcoin-random-js-build
cmake --build /tmp/bitcoin-random-js-build
```

## Native C++ example

```cpp
#include "random.h"

#include <array>

int main() {
    std::array<unsigned char, 32> seed{};
    bitcoin_random::GetStrongRandBytes(seed);

    bitcoin_random::FastRandomContext rng;
    auto value = rng.rand64();
    (void)value;
}
```
