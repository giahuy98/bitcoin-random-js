'use strict';

const { spawnSync } = require('node:child_process');

const args = [
  require.resolve('prebuildify/bin.js'),
  '--napi',
  '--strip',
  '--target',
  process.versions.node,
];

const result = spawnSync(process.execPath, args, {
  stdio: 'inherit',
});

if (result.error) {
  throw result.error;
}

process.exit(result.status ?? 1);
