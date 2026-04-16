#pragma once
#include <cstdint>
#include "KeyboardData.h"   // PotFilterStore

// ── PotFilter — Core 1 only, not thread-safe ──
// MCP3208 SPI ADC for 5 pots. Deadband + edge snap + sleep/wake. No EMA, no float.
namespace PotFilter {

// --- Lifecycle ---
void begin();                              // SPI init, initial reads, start ACTIVE
void updateAll();                          // Read + filter all 5 pots (call once per loop)

// --- Configuration ---
void setConfig(const PotFilterStore& cfg); // Live update from loadAll or setup
const PotFilterStore& getConfig();

// --- Output (consumers: PotRouter, SetupPotInput, ToolPotMapping) ---
uint16_t getStable(uint8_t pot);           // Post-deadband output 0-4095
bool     hasMoved(uint8_t pot);            // Crossed deadband this cycle
uint16_t getRaw(uint8_t pot);              // Last raw SPI read

}  // namespace PotFilter
