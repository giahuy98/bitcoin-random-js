'use strict';

const fs = require('node:fs');
const path = require('node:path');

function loadNativeModule() {
  const localBuild = path.join(__dirname, 'build', 'Release', 'bitcoin_random_js.node');
  if (fs.existsSync(localBuild)) {
    return require(localBuild);
  }

  const prebuilt = path.join(
    __dirname,
    'prebuilds',
    `${process.platform}-${process.arch}`,
    'bitcoin-random-js.node',
  );
  if (fs.existsSync(prebuilt)) {
    return require(prebuilt);
  }

  throw new Error(
    `No native binary found for ${process.platform}-${process.arch}. ` +
      'This package ships prebuilt binaries for linux-x64, win32-x64, darwin-x64, and darwin-arm64.',
  );
}

const native = loadNativeModule();

function assertLength(length) {
  if (!Number.isSafeInteger(length) || length < 0) {
    throw new RangeError('length must be a non-negative safe integer');
  }
}

function assertUint32(value) {
  if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) {
    throw new RangeError('value must be a uint32');
  }
}

function getRandBytes(length = 32) {
  assertLength(length);
  return native.getRandBytes(length);
}

function getStrongRandBytes(length = 32) {
  assertLength(length);
  return native.getStrongRandBytes(length);
}

function getRandHash() {
  return native.getRandHash();
}

function randomInit() {
  return native.randomInit();
}

function randAddPeriodic() {
  return native.randAddPeriodic();
}

function randAddEvent(eventInfo) {
  assertUint32(eventInfo);
  return native.randAddEvent(eventInfo);
}

module.exports = {
  getRandBytes,
  getStrongRandBytes,
  getRandHash,
  randomInit,
  randAddPeriodic,
  randAddEvent,
};
