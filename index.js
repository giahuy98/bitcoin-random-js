'use strict';

const native = require('./build/Release/bitcoin_random_js.node');

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
