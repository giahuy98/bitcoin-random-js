/// <reference types="node" />

export function getRandBytes(length?: number): Buffer;
export function getStrongRandBytes(length?: number): Buffer;
export function getRandHash(): Buffer;
export function randomInit(): void;
export function randAddPeriodic(): void;
export function randAddEvent(eventInfo: number): void;
