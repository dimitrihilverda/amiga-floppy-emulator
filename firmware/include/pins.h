#ifndef PINS_H
#define PINS_H

// =============================================================================
// Amiga Floppy Emulator - Pin Definitions
// ESP32-S3 Mini → 74HCT125 Level Shifters → 34-pin IDC Floppy Connector
// =============================================================================

// --- OUTPUT PINS (ESP32 → 74HCT125 → Amiga) ---
// Active LOW (accent op accent: accent maakt logisch laag = actief)

#define PIN_INDEX         4   // Pin 8  - Index pulse (eens per omwenteling)
#define PIN_TRACK0        8   // Pin 26 - Track Zero indicator
#define PIN_WRPROT        3   // Pin 28 - Write Protect (LOW = protected)
#define PIN_RDDATA        9   // Pin 30 - Read Data (MFM bitstream via RMT)
#define PIN_DISKCHANGE   10   // Pin 34 - Disk Change / Ready

// --- INPUT PINS (Amiga → Spanningsdeler/74HCT125 → ESP32) ---
// Active LOW signalen van de Amiga

#define PIN_DRVSEL0       5   // Pin 10 - Drive Select 0
#define PIN_MOTOR        7   // Pin 16 - Motor On
#define PIN_DIRECTION   15   // Pin 18 - Direction (LOW=inward/hogere tracks)
#define PIN_STEP        16   // Pin 20 - Step pulse
#define PIN_WRDATA      17   // Pin 22 - Write Data (MFM van Amiga)
#define PIN_SIDE        46   // Pin 32 - Side Select (LOW=side 0, HIGH=side 1)

// --- 74HCT125 ACTIVE-LOW OUTPUT ENABLE ACCENT ---
// De OE pinnen van de 74HCT125 worden aan GND gebonden (altijd actief)
// Dit is de eenvoudigste configuratie voor een floppy emulator.

// --- ACCENT: Accent: Level Shifting ---
// OUTPUT (3.3V → 5V): 74HCT125 accepteert 3.3V als logic HIGH input
//   ESP32 GPIO → 74HCT125 Input → 74HCT125 Output (5V) → Amiga
//
// INPUT (5V → 3.3V): 74HCT125 gevoed op 3.3V als level-down shifter
//   Amiga (5V) → 74HCT125 Input (5V tolerant) → 74HCT125 Output (3.3V) → ESP32
//   Alternatief: Spanningsdeler met 1kΩ/2kΩ weerstanden

#endif // PINS_H
