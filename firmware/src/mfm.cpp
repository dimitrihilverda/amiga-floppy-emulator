#include "mfm.h"
#include "config.h"
#include <string.h>
#include <Arduino.h>

// =============================================================================
// MFM Encoder - Corrected AmigaDOS sector layout
//
// AmigaDOS sector layout (in MFM domain):
//   [2 bytes gap 0xAA] [2×0x4489 sync]
//   [info odd 4B] [info even 4B]           ← "header block" (8 bytes raw = 4 longs MFM)
//   [label odd 16B] [label even 16B]       ← OS recovery (32 bytes raw = 8 longs MFM)
//   [header checksum odd 4B] [hdr ck even 4B]
//   [data checksum odd 4B] [data ck even 4B]  ← NOTE: data ck BEFORE data!
//   [data odd 512B] [data even 512B]
//
// All odd/even sections are MFM encoded with clock bits.
// Checksum = XOR of relevant MFM longwords, masked with 0x55555555
//
// Reference: AmigaOS Developer Docs, phloppy_0, adflib
// =============================================================================

MFMEncoder::MFMEncoder() : _lastBit(0) {}

void MFMEncoder::writeBE16(uint8_t* buf, uint16_t val) {
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

void MFMEncoder::writeBE32(uint8_t* buf, uint32_t val) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

// AmigaDOS checksum: XOR all longwords, mask with 0x55555555
uint32_t MFMEncoder::amigaChecksum(const uint8_t* mfmData, size_t len) {
    uint32_t ck = 0;
    for (size_t i = 0; i + 3 < len; i += 4) {
        uint32_t lw = ((uint32_t)mfmData[i] << 24) |
                      ((uint32_t)mfmData[i+1] << 16) |
                      ((uint32_t)mfmData[i+2] << 8) |
                      (uint32_t)mfmData[i+3];
        ck ^= lw;
    }
    return ck & 0x55555555;
}

// Split a 32-bit longword into odd and even bit halves
// Returns: bits 31,29,27,...,1 packed into upper 16 bits (odd)
//          bits 30,28,26,...,0 packed into lower 16 bits (even)
static uint32_t splitOddEven(uint32_t val) {
    uint32_t odd = 0, even = 0;
    for (int i = 0; i < 16; i++) {
        odd  |= ((val >> (31 - i * 2)) & 1) << (15 - i);
        even |= ((val >> (30 - i * 2)) & 1) << (15 - i);
    }
    return (odd << 16) | even;
}

// MFM encode raw data bits with clock bit insertion
// Input:  data stream (each bit is a data bit)
// Output: MFM stream (clock+data interleaved)
// Each input byte produces 2 output bytes (8 data bits → 16 MFM bits)
// prevBit tracks the last data bit for clock calculation across calls
static size_t mfmEncodeWithClocks(const uint8_t* data, size_t dataLen,
                                   uint8_t* output, uint8_t& prevBit) {
    size_t outPos = 0;
    uint8_t outByte = 0;
    int outBitCount = 0;

    for (size_t i = 0; i < dataLen; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            uint8_t dataBit = (data[i] >> bit) & 1;

            // MFM clock rule: clock=1 only if both previous data bit AND current data bit are 0
            uint8_t clockBit = (!dataBit && !prevBit) ? 1 : 0;

            // Write clock bit first, then data bit
            outByte = (outByte << 1) | clockBit;
            outBitCount++;
            if (outBitCount == 8) {
                output[outPos++] = outByte;
                outByte = 0;
                outBitCount = 0;
            }

            outByte = (outByte << 1) | dataBit;
            outBitCount++;
            if (outBitCount == 8) {
                output[outPos++] = outByte;
                outByte = 0;
                outBitCount = 0;
            }

            prevBit = dataBit;
        }
    }

    // Flush any remaining bits (shouldn't happen with byte-aligned input)
    if (outBitCount > 0) {
        outByte <<= (8 - outBitCount);
        output[outPos++] = outByte;
    }

    return outPos;
}

// Helper: split N bytes of logical data into odd/even bit streams
// Input:  src (N bytes, treated as N/4 longwords, big-endian)
// Output: oddDst (N/2 bytes), evenDst (N/2 bytes)
static void splitDataOddEven(const uint8_t* src, size_t srcLen,
                              uint8_t* oddDst, uint8_t* evenDst) {
    size_t numLongs = srcLen / 4;
    for (size_t i = 0; i < numLongs; i++) {
        uint32_t lw = ((uint32_t)src[i*4] << 24) |
                      ((uint32_t)src[i*4+1] << 16) |
                      ((uint32_t)src[i*4+2] << 8) |
                      (uint32_t)src[i*4+3];
        uint32_t sp = splitOddEven(lw);
        oddDst[i*2]     = (sp >> 24) & 0xFF;
        oddDst[i*2 + 1] = (sp >> 16) & 0xFF;
        evenDst[i*2]     = (sp >> 8) & 0xFF;
        evenDst[i*2 + 1] = sp & 0xFF;
    }
}

// =============================================================================
// Encode a single AmigaDOS sector — CORRECTED LAYOUT
// =============================================================================
size_t MFMEncoder::encodeSector(const uint8_t* sectorData, uint8_t track,
                                 uint8_t sector, uint8_t sectorsToGap,
                                 uint8_t* output) {
    size_t pos = 0;

    // --- GAP: 2 words of 0x00 → MFM = 0xAAAAAAAA ---
    output[pos++] = 0xAA;
    output[pos++] = 0xAA;
    output[pos++] = 0xAA;
    output[pos++] = 0xAA;

    // --- SYNC: 2 × 0x4489 (special pattern, NOT normal MFM) ---
    output[pos++] = 0x44;
    output[pos++] = 0x89;
    output[pos++] = 0x44;
    output[pos++] = 0x89;

    // After sync 0x4489, the last data bit is 1 (bit 0 of 0x89 = 1...0x89=10001001)
    _lastBit = 1;

    // =========================================================================
    // HEADER BLOCK: info (4 bytes) + label (16 bytes) = 20 bytes logical
    // MFM: odd half (20 bytes) + even half (20 bytes) = 40 raw bytes → 80 MFM bytes
    // =========================================================================

    // Build info longword: [format=0xFF] [track] [sector] [sectorsToGap]
    uint8_t headerBlock[20];
    headerBlock[0] = 0xFF;
    headerBlock[1] = track;
    headerBlock[2] = sector;
    headerBlock[3] = sectorsToGap;
    // Label area: 16 bytes zeros (OS recovery, unused in standard AmigaDOS)
    memset(&headerBlock[4], 0, 16);

    // Split into odd/even
    uint8_t hdrOdd[10], hdrEven[10];
    // Process as 5 longwords (20 bytes / 4)
    splitDataOddEven(headerBlock, 20, hdrOdd, hdrEven);

    // Record start for checksum calculation
    size_t hdrMfmStart = pos;

    // Write MFM-encoded odd bits
    pos += mfmEncodeWithClocks(hdrOdd, 10, &output[pos], _lastBit);
    // Write MFM-encoded even bits
    pos += mfmEncodeWithClocks(hdrEven, 10, &output[pos], _lastBit);

    size_t hdrMfmEnd = pos;

    // =========================================================================
    // HEADER CHECKSUM
    // =========================================================================
    uint32_t hdrCk = amigaChecksum(&output[hdrMfmStart], hdrMfmEnd - hdrMfmStart);

    // Split checksum into odd/even and MFM encode
    uint32_t hdrCkSplit = splitOddEven(hdrCk);
    uint8_t hdrCkOdd[2]  = { (uint8_t)(hdrCkSplit >> 24), (uint8_t)(hdrCkSplit >> 16) };
    uint8_t hdrCkEven[2] = { (uint8_t)(hdrCkSplit >> 8),  (uint8_t)(hdrCkSplit & 0xFF) };
    pos += mfmEncodeWithClocks(hdrCkOdd, 2, &output[pos], _lastBit);
    pos += mfmEncodeWithClocks(hdrCkEven, 2, &output[pos], _lastBit);

    // =========================================================================
    // DATA CHECKSUM (comes BEFORE data in Amiga format!)
    // We need to pre-compute it, so we encode data to a temp buffer first
    // =========================================================================

    // Split 512 bytes of sector data into odd/even (each 256 bytes)
    uint8_t* dataOdd  = (uint8_t*)malloc(256);
    uint8_t* dataEven = (uint8_t*)malloc(256);
    // Temp buffer for MFM-encoded data (256 bytes × 2 × 2 = 1024 MFM bytes)
    uint8_t* dataMfmTemp = (uint8_t*)malloc(1100);

    if (!dataOdd || !dataEven || !dataMfmTemp) {
        if (dataOdd) free(dataOdd);
        if (dataEven) free(dataEven);
        if (dataMfmTemp) free(dataMfmTemp);
        Serial.println("[MFM] ERROR: malloc failed in encodeSector!");
        return pos;
    }

    splitDataOddEven(sectorData, 512, dataOdd, dataEven);

    // MFM-encode the data to temp buffer so we can calculate the checksum
    uint8_t tempLastBit = _lastBit;  // Save state, we'll re-encode later
    size_t dataMfmLen = 0;

    // We need a dummy prevBit for the checksum calc - but the checksum itself
    // sits between header and data, so prevBit after checksum is what matters.
    // Solution: encode data with a temporary prevBit, calc checksum,
    // then write checksum and re-encode data with correct prevBit.

    // Approach: pre-encode data to get checksum, then write checksum + data
    uint8_t dummyPrev = 0;  // After checksum, prevBit depends on checksum value
    dataMfmLen += mfmEncodeWithClocks(dataOdd, 256, &dataMfmTemp[dataMfmLen], dummyPrev);
    dataMfmLen += mfmEncodeWithClocks(dataEven, 256, &dataMfmTemp[dataMfmLen], dummyPrev);

    uint32_t dataCk = amigaChecksum(dataMfmTemp, dataMfmLen);

    // Now write the data checksum
    uint32_t dataCkSplit = splitOddEven(dataCk);
    uint8_t dataCkOdd[2]  = { (uint8_t)(dataCkSplit >> 24), (uint8_t)(dataCkSplit >> 16) };
    uint8_t dataCkEven[2] = { (uint8_t)(dataCkSplit >> 8),  (uint8_t)(dataCkSplit & 0xFF) };
    pos += mfmEncodeWithClocks(dataCkOdd, 2, &output[pos], _lastBit);
    pos += mfmEncodeWithClocks(dataCkEven, 2, &output[pos], _lastBit);

    // =========================================================================
    // DATA (512 bytes → odd 256 + even 256 → MFM ~1024 bytes)
    // Now encode with the REAL _lastBit (after checksum was written)
    // =========================================================================
    pos += mfmEncodeWithClocks(dataOdd, 256, &output[pos], _lastBit);
    pos += mfmEncodeWithClocks(dataEven, 256, &output[pos], _lastBit);

    free(dataOdd);
    free(dataEven);
    free(dataMfmTemp);

    return pos;
}

// =============================================================================
// Encode a complete track from ADF sector data
// =============================================================================
size_t MFMEncoder::encodeTrack(const uint8_t* trackData, uint8_t cylinder,
                                uint8_t side, uint8_t* mfmBuffer,
                                size_t bufferSize) {
    size_t pos = 0;
    _lastBit = 0;

    uint8_t trackNum = cylinder * 2 + side;

    // Pre-gap: ~80 bytes of 0xAA (MFM-encoded zeros, ~640 bit cells = 1.28ms)
    int preGapBytes = 80;
    for (int i = 0; i < preGapBytes && pos < bufferSize; i++) {
        mfmBuffer[pos++] = 0xAA;
    }

    // Encode all 11 sectors
    for (int sec = 0; sec < SECTORS_PER_TRACK; sec++) {
        if (pos >= bufferSize - 1200) {
            Serial.printf("[MFM] Buffer near full at sector %d (pos=%d)\n", sec, pos);
            break;
        }

        const uint8_t* sectorData = &trackData[sec * BYTES_PER_SECTOR];
        size_t written = encodeSector(sectorData, trackNum, sec,
                                       SECTORS_PER_TRACK - sec,
                                       &mfmBuffer[pos]);
        pos += written;
    }

    // Post-gap: fill remaining track time with 0xAA
    // Target: BYTES_PER_RAW_TRACK (12500 bytes = 100000 bits = 200ms)
    while (pos < BYTES_PER_RAW_TRACK && pos < bufferSize) {
        mfmBuffer[pos++] = 0xAA;
    }

    Serial.printf("[MFM] Track %d (cyl=%d side=%d): %d bytes (%d bits, %.1fms)\n",
                  trackNum, cylinder, side, pos, pos * 8,
                  (float)(pos * 8) * BIT_CELL_US / 1000.0f);

    return pos;
}

// =============================================================================
// MFM Decoder (for write support / verification)
// =============================================================================

MFMDecoder::MFMDecoder() {}

int MFMDecoder::findSync(const uint8_t* data, size_t len, size_t startPos) {
    for (size_t i = startPos; i + 1 < len; i++) {
        if (data[i] == 0x44 && data[i + 1] == 0x89) {
            return (int)i;
        }
    }
    return -1;
}

bool MFMDecoder::verifyChecksum(const uint8_t* mfmData, size_t len, uint32_t expected) {
    uint32_t ck = 0;
    for (size_t i = 0; i + 3 < len; i += 4) {
        uint32_t lw = ((uint32_t)mfmData[i] << 24) |
                      ((uint32_t)mfmData[i+1] << 16) |
                      ((uint32_t)mfmData[i+2] << 8) |
                      (uint32_t)mfmData[i+3];
        ck ^= lw;
    }
    return (ck & 0x55555555) == expected;
}

void MFMDecoder::mfmDecodeBlock(const uint8_t* mfmData, size_t mfmLen, uint8_t* output) {
    // Extract data bits (every other bit, starting from bit position 1)
    size_t outPos = 0;
    uint8_t outByte = 0;
    int outBitCount = 0;

    for (size_t i = 0; i < mfmLen; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            // Even bit positions (0,2,4,6) are clock bits — skip
            // Odd bit positions (1,3,5,7) are data bits — extract
            if (bit % 2 == 0) continue;  // Skip clock bits

            uint8_t dataBit = (mfmData[i] >> bit) & 1;
            outByte = (outByte << 1) | dataBit;
            outBitCount++;
            if (outBitCount == 8) {
                output[outPos++] = outByte;
                outByte = 0;
                outBitCount = 0;
            }
        }
    }
}

int MFMDecoder::decodeTrack(const uint8_t* mfmBuffer, size_t mfmLength,
                             uint8_t* trackData, uint8_t* cylinder,
                             uint8_t* side) {
    int sectorsDecoded = 0;
    size_t pos = 0;

    while (sectorsDecoded < SECTORS_PER_TRACK && pos < mfmLength) {
        // Find sync word 0x4489
        int syncPos = findSync(mfmBuffer, mfmLength, pos);
        if (syncPos < 0) break;

        // Check for double sync
        if (syncPos + 3 < (int)mfmLength &&
            mfmBuffer[syncPos + 2] == 0x44 && mfmBuffer[syncPos + 3] == 0x89) {
            pos = syncPos + 4;  // Skip past both sync words
        } else {
            pos = syncPos + 2;
            continue;
        }

        // Decode sector info from MFM
        // Header MFM block = 40 bytes (info odd+even + label odd+even)
        if (pos + 40 + 8 + 8 + 1024 > mfmLength) break;

        // For now, extract track/sector info from the raw MFM header
        // Full decode would reconstitute the odd/even halves

        sectorsDecoded++;
        pos += 40 + 8 + 8 + 1024;  // header + hdr_ck + data_ck + data
    }

    return sectorsDecoded;
}
