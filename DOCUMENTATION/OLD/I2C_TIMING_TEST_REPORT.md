# I2C Bus Timing Test Report — ILLPAD MPR121

**Date:** 2026-02-12  
**Test Environment:** `I2S_TIMING_TEST`  
**Hardware:** ESP32-S3-N8R16, 4× MPR121 (0x5A, 0x5B, 0x5C, 0x5D)

---

## Executive Summary

**Test Result:** ✅ **All tests passed with zero I2C errors**

- **100 kHz:** ~65.7 polls/s, ~15.2 ms per scan
- **400 kHz:** 250 polls/s, ~4.0 ms per scan
- **Recommendation:** Use **400 kHz** for production (already configured)

---

## Test Configuration

- **I2C Pins:** SDA=GPIO8, SCL=GPIO9
- **MPR121 Addresses:** 0x5A, 0x5B, 0x5C, 0x5D (all 4 sensors present)
- **Init Time:** 31 ms (all sensors initialized successfully)
- **Test Duration:** ~86 seconds total

---

## Detailed Results

### 100 kHz Tests

| Duration | Polls | Errors | Polls/s | Scan Time (µs) |
|----------|-------|--------|---------|----------------|
| 3 s      | 198   | 0      | 65.72   | min: 15202, avg: 15215, max: 15257 |
| 10 s     | 658   | 0      | 65.71   | min: 15201, avg: 15215, max: 15224 |
| 30 s     | 1972  | 0      | 65.71   | min: 15201, avg: 15215, max: 15224 |

**Analysis:**
- Consistent ~65.7 polls/s across all durations
- Scan time ~15.2 ms (very stable, low jitter)
- Zero errors — bus is reliable at this speed

### 400 kHz Tests

| Duration | Polls | Errors | Polls/s | Scan Time (µs) |
|----------|-------|--------|---------|----------------|
| 3 s      | 750   | 0      | 250.00  | min: 3998, avg: 3998, max: 4017 |
| 10 s     | 2500  | 0      | 250.00  | min: 3998, avg: 3998, max: 4017 |
| 30 s     | 7500  | 0      | 250.00  | min: 3998, avg: 3998, max: 4017 |

**Analysis:**
- Exactly **250 polls/s** (perfectly consistent)
- Scan time ~4.0 ms (very stable, minimal jitter)
- Zero errors — bus is reliable at this speed
- **3.8× faster** than 100 kHz

---

## Key Metrics

### Maximum Reliable Poll Rate
- **100 kHz:** 65.7 polls/s (with margin: use ≤60 polls/s)
- **400 kHz:** 250 polls/s (with margin: use ≤200 polls/s)

### Scan Time (Latency)
- **100 kHz:** ~15.2 ms per full scan (all 4 sensors)
- **400 kHz:** ~4.0 ms per full scan (all 4 sensors)

### Reliability
- **Both speeds:** Zero I2C errors across all test durations
- **Conclusion:** Bus is stable and reliable at both speeds

---

## Implications for Main Sketch

### Current Configuration ✅

**`HardwareConfig.h`:**
```cpp
#define I2C_CLOCK_HZ 400000   // ✅ Already optimal
```

**Status:** Current 400 kHz setting is **correct** and provides the best performance.

### Main Loop Poll Rate

**Test shows:** Maximum reliable poll rate = **250 polls/s** at 400 kHz

**Current main sketch:**
- Calls `keyboard.update()` once per `loop()` iteration
- Loop rate depends on other work (LED updates, BLE housekeeping, pot reading)
- No explicit rate limiting on `keyboard.update()`

**Recommendation:**
- **No changes needed** — main loop naturally runs slower than 250 Hz
- If you add explicit rate limiting later, cap at **≤200 Hz** (80% of max) for safety margin

### Aftertouch Rate Limit

**Current config:**
```cpp
const uint16_t AFTERTOUCH_UPDATE_INTERVAL_MS = 25;  // ~40 Hz per note
```

**Analysis:**
- 40 Hz per pad × 48 pads = theoretical max **1920 aftertouch events/s**
- But: only pressed pads send aftertouch, and rate limiting prevents bursts
- **I2C capacity:** 250 polls/s × 48 pads = theoretical max **12,000 pad reads/s**
- **Conclusion:** ✅ Current 25 ms interval is fine — I2C bus has plenty of headroom

**Recommendation:** No changes needed. Current aftertouch rate is well within I2C capacity.

### Latency Considerations

**Worst-case scan time:**
- **400 kHz:** ~4.0 ms per full scan (all 4 sensors)
- This is the **I2C contribution** to overall latency

**Total latency chain:**
1. I2C scan: ~4 ms (from test)
2. Main loop processing: ~1–2 ms (estimated)
3. MIDI event queue: <1 ms (in-memory)
4. BLE MIDI transmission: ~10–20 ms (BLE connection interval)
5. **Total:** ~15–27 ms typical

**Recommendation:** ✅ 4 ms I2C latency is acceptable for a MIDI controller. No changes needed.

---

## Recommendations Summary

| Item | Current | Recommended | Status |
|------|---------|-------------|--------|
| I2C Speed | 400 kHz | 400 kHz | ✅ Already optimal |
| Max Poll Rate | No limit | ≤200 Hz (if limiting) | ✅ No limit needed |
| Aftertouch Interval | 25 ms | 25 ms | ✅ Already optimal |
| Scan Time | ~4 ms | ~4 ms | ✅ Acceptable |

**Overall:** ✅ **No changes required** — current configuration is optimal for BLE MIDI performance.

---

## Technical Notes

### Why 400 kHz is Better

- **3.8× faster** polling (250 vs 65.7 polls/s)
- **3.8× lower latency** (4 ms vs 15 ms per scan)
- **Same reliability** (zero errors at both speeds)
- **Better responsiveness** for MIDI aftertouch

### Safety Margin

The test shows **250 polls/s** is reliable. For production use, we recommend:
- **Main loop:** No explicit limit needed (naturally slower)
- **If adding rate limiting:** Cap at **≤200 Hz** (80% of max) for safety margin

### Bus Utilization

At 400 kHz:
- **Scan time:** ~4 ms
- **Polls/s:** 250
- **Bus utilization:** ~100% during scan (expected — we're reading all 4 sensors)
- **Idle time:** ~0 ms between scans (tight loop in test)

In main sketch:
- Bus utilization will be **lower** (main loop does other work between scans)
- This provides additional safety margin

---

## Conclusion

The I2C bus test confirms that:
1. ✅ **400 kHz is optimal** — already configured correctly
2. ✅ **Zero errors** — bus is reliable
3. ✅ **250 polls/s capacity** — more than enough for MIDI aftertouch
4. ✅ **~4 ms latency** — acceptable for MIDI controller

**No changes needed to main sketch.** Current configuration is production-ready.
