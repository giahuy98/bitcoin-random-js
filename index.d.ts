/// <reference types="node" />

/** Returns `length` bytes of fast random data. */
export function getRandBytes(length?: number): Buffer;

/** Returns `length` bytes of strong random data. Any non-negative length is allowed. */
export function getStrongRandBytes(length?: number): Buffer;

/** Returns a fixed-size 32-byte random hash buffer. */
export function getRandHash(): Buffer;

/** Optional eager seeding hook. Normal use does not require calling this first. */
export function randomInit(): void;

/** Optional maintenance hook for long-lived processes. Not required for basic use. */
export function randAddPeriodic(): void;

/** Optional advanced hook for mixing in external event data. */
export function randAddEvent(eventInfo: number): void;
