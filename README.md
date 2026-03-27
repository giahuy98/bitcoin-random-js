# bitcoin-random-js

Standalone extraction of Bitcoin Core's random subsystem packaged for Node.js, with prebuilt binaries for supported platforms and the native C++ sources kept alongside the addon.

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
- the npm package is intended to ship prebuilt binaries so consumers do not need a compiler toolchain

## npm consumers

Once the package is published with its prebuilds, consumers should be able to install it normally:

```bash
npm install bitcoin-random-js
```

No local native compilation should be needed on supported platforms because `index.js` loads the packaged binary directly from `prebuilds/<platform>-<arch>/`.

The published npm package has no npm dependencies.

## Usage notes

- `getStrongRandBytes(length)` accepts any non-negative length. It is not capped at 32 bytes; requests larger than 32 bytes are filled internally in 32-byte chunks.
- `getRandHash()` is the fixed-size 32-byte helper.
- `randomInit()` is optional. The RNG seeds lazily on first use, so normal clients do not need to call it before `getRandBytes()` or `getStrongRandBytes()`.
- `randAddPeriodic()` is optional. It is a maintenance hook for long-lived host processes that want to periodically mix in fresh environmental entropy, not a required step for ordinary consumers.
- `randAddEvent()` is also an advanced host hook. Most Node clients should not need it.

## Node.js build

```bash
cd bitcoin-random-js
npm run build:native
```

## Node.js example

```js
const { getStrongRandBytes, randAddPeriodic } = require('./');

const bytes = getStrongRandBytes(64);
console.log(bytes.toString('hex'));

// Optional for long-lived processes.
randAddPeriodic();
```

Run the bundled example with:

```bash
cd bitcoin-random-js
npm run build:native
node example.js
```

## Building prebuilt binaries

To generate a prebuilt binary for the current platform:

```bash
cd bitcoin-random-js
npm run build:prebuild
```

This creates a `prebuilds/` directory that is included in the npm package.

## npm publishing

The repository includes a GitHub Actions publish workflow that:

- builds prebuilt binaries on Linux, Windows, macOS x64, and macOS arm64
- gathers them into one package
- publishes the package with `npm publish --provenance`

To use it, set `NPM_TOKEN` in the repository secrets and push a `v*` tag.

## Native C++ build

```bash
cmake -S . -B build
cmake --build build
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
