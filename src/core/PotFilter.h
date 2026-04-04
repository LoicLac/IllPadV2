#pragma once
#include <cstdint>
#include "KeyboardData.h"   // PotFilterStore

// ── PotFilter — Core 1 only, not thread-safe ──
// Adaptive filter for 5 potentiometers with oversampling, sleep, and edge snap.
// Single source of truth for all ADC pot reads (runtime + setup).
namespace PotFilter {

// --- Lifecycle ---
void begin();                              // GPIO init, initial reads, start ACTIVE
void updateAll();                          // Read + filter all 5 pots (call once per loop)

// --- Configuration ---
void setConfig(const PotFilterStore& cfg); // Live update from Monitor or loadAll
const PotFilterStore& getConfig();

// --- Output (consumers: PotRouter, SetupPotInput, ToolPotMapping) ---
uint16_t getStable(uint8_t pot);           // Post-deadband output 0-4095
bool     hasMoved(uint8_t pot);            // Crossed deadband this cycle

// --- Monitor / debug ---
uint16_t getRaw(uint8_t pot);              // Last raw ADC (for monitor)
float    getSmoothed(uint8_t pot);         // Post-EMA (for monitor)
float    getActivity(uint8_t pot);         // Current activity (for monitor)
bool     isSleeping(uint8_t pot);          // State == SLEEPING

}  // namespace PotFilter
