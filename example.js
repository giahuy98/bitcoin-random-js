'use strict';

const { getStrongRandBytes } = require('./index');

const bytes = getStrongRandBytes(64);
console.log(`64 strong random bytes: ${bytes.toString('hex')}`);
