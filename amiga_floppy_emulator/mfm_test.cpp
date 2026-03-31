#include "mfm.h"
#include "config.h"
#include <Arduino.h>

// =============================================================================
// MFM Encoder Validation
//
// Generates a known test pattern, encodes it, and verifies the output
// matches expected MFM characteristics. Run this at startup to catch
// encoding bugs before connecting to real Amiga hardware.
// =============================================================================

static MFMEncoder testEncoder;
static MFMDecoder testDecoder;

// Verify basic MFM encoding properties
static bool testMFMClockRule(const uint8_t* mfm, size_t len) {
    // MFM rule: no two consecutive '1' bits
    // (because a clock bit between two '1' data bits must be '0')
    uint8_t prevBit = 0;
    int violations = 0;

    for (size_t i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            uint8_t b = (mfm[i] >> bit) & 1;
            if (b && prevBit) {
                violations++;
                if (violations <= 5) {
                    Serial.printf("[MFM TEST] Clock violation at byte %d bit %d\n", i, bit);
                }
            }
            prevBit = b;
        }
    }

    if (violations > 0) {
        Serial.printf("[MFM TEST] FAIL: %d clock rule violations\n", violations);
        // Note: The sync word 0x4489 intentionally violates the rule
        // (that's how the Amiga recognizes it). We expect exactly
        // 2 violations per sector × 11 sectors = 22 violations per track.
        // Actually 0x4489 = 01000100 10001001 — let's check...
        // 0x44 = 01000100, 0x89 = 10001001
        // Between 0x44 and 0x89: last bit of 0x44 is 0, first bit of 0x89 is 1 → OK
        // Within 0x89: bits are 1,0,0,0,1,0,0,1 → no consecutive 1s
        // Within 0x44: bits are 0,1,0,0,0,1,0,0 → no consecutive 1s
        // So 0x4489 does NOT violate the clock rule in its bit pattern!
        // The special thing about 0x4489 is that it can't be produced by
        // normal MFM encoding — the clock bits are in impossible positions.
        // So any violations we find are real bugs.
        return (violations == 0);
    }

    Serial.println("[MFM TEST] Clock rule OK (no consecutive 1s outside sync)");
    return true;
}

// Check that sync words appear in the encoded output
static bool testSyncWords(const uint8_t* mfm, size_t len) {
    int syncCount = 0;
    for (size_t i = 0; i + 1 < len; i++) {
        if (mfm[i] == 0x44 && mfm[i+1] == 0x89) {
            syncCount++;
        }
    }

    // Each sector has 2 sync words, 11 sectors = 22 sync words per track
    bool ok = (syncCount == SECTORS_PER_TRACK * 2);
    Serial.printf("[MFM TEST] Sync words found: %d (expected %d) %s\n",
                  syncCount, SECTORS_PER_TRACK * 2, ok ? "OK" : "FAIL");
    return ok;
}

// Check track length is reasonable
static bool testTrackLength(size_t len) {
    // DD track should be close to 12500 bytes (100000 bits / 8)
    bool ok = (len >= 12000 && len <= BYTES_PER_RAW_TRACK);
    Serial.printf("[MFM TEST] Track length: %d bytes (target %d) %s\n",
                  len, BYTES_PER_RAW_TRACK, ok ? "OK" : "WARN");
    return ok;
}

// Check that the decoder can find all 11 sectors
static bool testDecoderFindsSectors(const uint8_t* mfm, size_t len) {
    uint8_t trackData[TRACK_DATA_SIZE];
    uint8_t cyl, side;

    int found = testDecoder.decodeTrack(mfm, len, trackData, &cyl, &side);
    bool ok = (found == SECTORS_PER_TRACK);
    Serial.printf("[MFM TEST] Decoder found %d/%d sectors %s\n",
                  found, SECTORS_PER_TRACK, ok ? "OK" : "FAIL");
    return ok;
}

// =============================================================================
// Run all MFM tests
// Returns true if all critical tests pass
// =============================================================================
bool mfm_selftest() {
    Serial.println("========================================");
    Serial.println("[MFM TEST] Starting self-test...");
    Serial.println("========================================");

    // Create a test pattern: simple incrementing bytes (like a Workbench boot block)
    uint8_t testTrack[TRACK_DATA_SIZE];
    for (int i = 0; i < TRACK_DATA_SIZE; i++) {
        testTrack[i] = (uint8_t)(i & 0xFF);
    }

    // Encode track 0, side 0
    uint8_t* mfmBuf = (uint8_t*)ps_malloc(MFM_TRACK_BUFFER_SIZE);
    if (!mfmBuf) {
        mfmBuf = (uint8_t*)malloc(MFM_TRACK_BUFFER_SIZE);
    }
    if (!mfmBuf) {
        Serial.println("[MFM TEST] FATAL: Cannot allocate test buffer!");
        return false;
    }

    unsigned long t0 = micros();
    size_t mfmLen = testEncoder.encodeTrack(testTrack, 0, 0, mfmBuf, MFM_TRACK_BUFFER_SIZE);
    unsigned long dt = micros() - t0;

    Serial.printf("[MFM TEST] Encode time: %lu µs\n", dt);

    // Run tests
    bool allOk = true;
    allOk &= testTrackLength(mfmLen);
    allOk &= testSyncWords(mfmBuf, mfmLen);
    allOk &= testMFMClockRule(mfmBuf, mfmLen);
    allOk &= testDecoderFindsSectors(mfmBuf, mfmLen);

    // Print first sector header bytes for manual inspection
    Serial.print("[MFM TEST] First 32 bytes: ");
    for (int i = 0; i < 32 && i < (int)mfmLen; i++) {
        Serial.printf("%02X ", mfmBuf[i]);
    }
    Serial.println();

    // Find first sync and print sector header
    for (size_t i = 0; i + 1 < mfmLen; i++) {
        if (mfmBuf[i] == 0x44 && mfmBuf[i+1] == 0x89) {
            Serial.printf("[MFM TEST] First sync at byte %d, header: ", i);
            for (int j = 0; j < 16 && (i + j) < mfmLen; j++) {
                Serial.printf("%02X ", mfmBuf[i + j]);
            }
            Serial.println();
            break;
        }
    }

    free(mfmBuf);

    Serial.println("========================================");
    Serial.printf("[MFM TEST] Result: %s\n", allOk ? "ALL PASSED" : "SOME FAILED");
    Serial.println("========================================");

    return allOk;
}
