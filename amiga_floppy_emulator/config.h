#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// Amiga Floppy Emulator - Configuration
// =============================================================================

// --- Disk Geometry ---
#define TRACKS_PER_DISK     80      // Cylinders (0-79)
#define SIDES_PER_DISK       2      // Side 0 and Side 1
#define SECTORS_PER_TRACK   11      // AmigaDOS DD format
#define BYTES_PER_SECTOR   512
#define TOTAL_TRACKS       (TRACKS_PER_DISK * SIDES_PER_DISK)  // 160

// ADF file size: 80 * 2 * 11 * 512 = 901120 bytes
#define ADF_FILE_SIZE      901120

// Raw track data size (decoded, before MFM encoding)
#define TRACK_DATA_SIZE    (SECTORS_PER_TRACK * BYTES_PER_SECTOR)  // 5632 bytes

// --- MFM Timing ---
#define RPM                300      // Disk rotation speed
#define REV_TIME_US    200000       // 200ms per revolution (1/300 * 60 * 1e6)

// DD (Double Density) timing
#define BIT_CELL_US          2      // 2 microseconds per MFM bit cell
#define DATA_RATE_BPS   250000      // 250 kbps
#define BITS_PER_TRACK  100000      // 200ms / 2us = 100000 bits per track
#define BYTES_PER_RAW_TRACK (BITS_PER_TRACK / 8)  // 12500 bytes raw MFM

// --- MFM Track Layout ---
// AmigaDOS sector: 2 sync words + 56 bytes header + 1024 bytes data (MFM encoded)
// Totaal per sector: ~1088 bytes MFM = 8704 bits
// 11 sectoren = ~95744 bits, rest is gap (~4256 bits = ~532 bytes)
#define MFM_SECTOR_SIZE    1088     // Approximate MFM bytes per sector
#define MFM_GAP_BYTE       0x00    // Gap filler (encodes to 0xAAAA in MFM)
#define MFM_SYNC_WORD      0x4489  // Amiga sync marker

// --- RMT Configuration ---
#define RMT_CHANNEL         0       // RMT channel for read data output
#define RMT_CLK_DIV        80       // 80MHz / 80 = 1MHz (1us resolution)
#define RMT_TICKS_PER_BIT   2       // 2 ticks = 2us = 1 DD bit cell

// --- Index Pulse ---
#define INDEX_PULSE_US     500      // Index pulse width in microseconds
#define MOTOR_SPINUP_MS    500      // Motor spin-up delay

// --- Step Timing ---
#define STEP_SETTLE_US    3000      // 3ms settle time after step
#define STEP_DEBOUNCE_US   100      // Debounce time for step signal

// --- WiFi AP Configuration ---
#define WIFI_SSID       "AmigaFloppy"
#define WIFI_PASSWORD   "amiga1200"
#define WIFI_CHANNEL     1
#define WEB_PORT        80

// --- Buffer Configuration ---
// With 8MB PSRAM we can buffer the entire ADF in memory
#define USE_PSRAM        true

#endif // CONFIG_H
