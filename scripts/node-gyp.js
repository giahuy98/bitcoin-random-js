'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

function resolveNodeGyp() {
  if (process.env.NODE_GYP_BIN && fs.existsSync(process.env.NODE_GYP_BIN)) {
    return process.env.NODE_GYP_BIN;
  }

  const candidates = [];
  if (process.env.npm_execpath) {
    const npmRoot = path.resolve(path.dirname(process.env.npm_execpath), '..');
    candidates.push(path.join(npmRoot, 'node_modules', 'node-gyp', 'bin', 'node-gyp.js'));
  }

  const execRoot = path.resolve(path.dirname(process.execPath), '..');
  candidates.push(path.join(execRoot, 'lib', 'node_modules', 'npm', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js'));

  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }

  throw new Error('Unable to locate node-gyp from npm. Run this command via `npm run`, or set NODE_GYP_BIN.');
}

function runNodeGyp(args) {
  const result = spawnSync(process.execPath, [resolveNodeGyp(), ...args], {
    stdio: 'inherit',
  });

  if (result.error) {
    throw result.error;
  }
  if (typeof result.status === 'number' && result.status !== 0) {
    process.exit(result.status);
  }
}

module.exports = runNodeGyp;

if (require.main === module) {
  runNodeGyp(process.argv.slice(2));
}
