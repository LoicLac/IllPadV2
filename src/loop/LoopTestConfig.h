#ifndef LOOP_TEST_CONFIG_H
#define LOOP_TEST_CONFIG_H

// =================================================================
// LoopTestConfig — temporary hardcoded test values (Phase 2 only)
// =================================================================
// This file exists because Phase 2 must run on the device before Tool 4
// (bank type selection) and Tool 3 (pad roles) gain LOOP support in Phase 3.
// When set, it forces bank 8 as LOOP and maps 3 adjacent pads as REC /
// PLAY-STOP / CLEAR controls so the engine can be tested end-to-end.
//
// REMOVE IN PHASE 3. Once Tool 4 persists the LOOP bank type and Tool 3
// persists the control pads via LoopPadStore, this file becomes dead.

#define LOOP_TEST_ENABLED     1

// Bank index (0-7) to force as LOOP at boot. Bank 8 = index 7 = MIDI ch 8.
#define LOOP_TEST_BANK        7

// 3 adjacent physical pads for REC / PLAY-STOP / CLEAR. Adjust to whatever
// is convenient on the prototype layout. These pads MUST NOT collide with
// any existing bank/scale/arp role (Tool 3). They act as control pads only
// when the foreground bank is LOOP_TEST_BANK — on other banks they remain
// regular music pads.
#define LOOP_TEST_REC_PAD      30
#define LOOP_TEST_PLAYSTOP_PAD 31
#define LOOP_TEST_CLEAR_PAD    32

#endif // LOOP_TEST_CONFIG_H
