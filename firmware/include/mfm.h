#ifndef MFM_H
#define MFM_H

#include <stdint.h>
#include <stddef.h>

// =============================================================================
// MFM Encoder for AmigaDOS format
//
// Amiga MFM encoding:
// - Data bits and clock bits are interleaved
// - Clock bit = 1 only if BOTH adjacent data bits are 0
// - Special sync word 0x4489 cannot occur in normal MFM data
//
// AmigaDOS encodes odd bits first, then even bits (not byte-by-byte!)
// =============================================================================

// MFM encoded track buffer (raw bitstream ready for RMT)
// Max ~12500 bytes per DD track
#define MFM_TRACK_BUFFER_SIZE  12800

class MFMEncoder {
public:
    MFMEncoder();

    // Encode a complete AmigaDOS track from ADF sector data
    // Input:  trackData   - 5632 bytes (11 sectors × 512 bytes) from ADF
    //         cylinder    - cylinder number (0-79)
    //         side        - head side (0 or 1)
    // Output: mfmBuffer   - raw MFM bitstream
    // Returns: number of bytes written to mfmBuffer
    size_t encodeTrack(const uint8_t* trackData, uint8_t cylinder, uint8_t side,
                       uint8_t* mfmBuffer, size_t bufferSize);

private:
    // Encode a single AmigaDOS sector
    // Returns bytes written to output
    size_t encodeSector(const uint8_t* sectorData, uint8_t track, uint8_t sector,
                        uint8_t sectorsToGap, uint8_t* output);

    // MFM encode a block of bytes (Amiga style: odd bits first, then even bits)
    // Writes 2× input length to output (each byte becomes 2 MFM bytes)
    void mfmEncodeBlock(const uint8_t* data, size_t len, uint8_t* output);

    // MFM encode a single 32-bit long word
    // Returns 32-bit MFM encoded value (with clock bits)
    uint32_t mfmEncodeLong(uint32_t data);

    // Calculate AmigaDOS checksum (XOR of MFM longs, masked with 0x55555555)
    uint32_t amigaChecksum(const uint8_t* mfmData, size_t len);

    // Write a 16-bit word to buffer (big-endian)
    void writeBE16(uint8_t* buf, uint16_t val);

    // Write a 32-bit word to buffer (big-endian)
    void writeBE32(uint8_t* buf, uint32_t val);

    // Track last bit for clock bit calculation between sectors
    uint8_t _lastBit;
};

// =============================================================================
// MFM Decoder (for write support - future use)
// =============================================================================

class MFMDecoder {
public:
    MFMDecoder();

    // Decode a raw MFM track back to AmigaDOS sector data
    // Returns number of sectors successfully decoded (0-11)
    int decodeTrack(const uint8_t* mfmBuffer, size_t mfmLength,
                    uint8_t* trackData, uint8_t* cylinder, uint8_t* side);

private:
    // Find sync word (0x4489) in MFM bitstream
    int findSync(const uint8_t* data, size_t len, size_t startPos);

    // MFM decode a block (reverse of encode)
    void mfmDecodeBlock(const uint8_t* mfmData, size_t mfmLen, uint8_t* output);

    // Verify AmigaDOS checksum
    bool verifyChecksum(const uint8_t* mfmData, size_t len, uint32_t expected);
};

#endif // MFM_H
