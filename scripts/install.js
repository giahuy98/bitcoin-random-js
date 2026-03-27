'use strict';

const fs = require('node:fs');
const path = require('node:path');

const rootDir = path.resolve(__dirname, '..');
const nativeBuild = path.join(rootDir, 'build', 'Release', 'bitcoin_random_js.node');
const prebuilt = path.join(
  rootDir,
  'prebuilds',
  `${process.platform}-${process.arch}`,
  'bitcoin-random-js.node',
);

if (fs.existsSync(nativeBuild) || fs.existsSync(prebuilt)) {
  process.exit(0);
}

console.warn(
  `[bitcoin-random-js] No bundled binary found for ${process.platform}-${process.arch}. ` +
    'This package ships prebuilt binaries only on supported platforms.',
);
