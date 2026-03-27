'use strict';

const fs = require('node:fs');
const path = require('node:path');

const runNodeGyp = require('./node-gyp');

const rootDir = path.resolve(__dirname, '..');
const outputDir = path.join(rootDir, 'prebuilds', `${process.platform}-${process.arch}`);
const builtModule = path.join(rootDir, 'build', 'Release', 'bitcoin_random_js.node');
const packagedModule = path.join(outputDir, 'bitcoin-random-js.node');

runNodeGyp(['rebuild']);

if (!fs.existsSync(builtModule)) {
  throw new Error(`Expected built addon at ${builtModule}`);
}

fs.mkdirSync(outputDir, { recursive: true });
fs.copyFileSync(builtModule, packagedModule);
