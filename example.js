'use strict';

const { getStrongRandBytes } = require('./index');

const bytes = getStrongRandBytes(32);
console.log(`32 strong random bytes: ${bytes.toString('hex')}`);
