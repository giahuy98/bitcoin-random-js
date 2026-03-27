'use strict';

const { getStrongRandBytes, randAddPeriodic } = require('./index');

const bytes = getStrongRandBytes(64);
console.log(`64 strong random bytes: ${bytes.toString('hex')}`);

// Optional for long-lived processes that want to periodically mix in fresh entropy.
randAddPeriodic();

const moreBytes = getStrongRandBytes(16);
console.log(`16 more strong random bytes after randAddPeriodic(): ${moreBytes.toString('hex')}`);
