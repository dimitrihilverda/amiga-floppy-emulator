#ifndef PINS_H
#define PINS_H

// =============================================================================
// Amiga Floppy Emulator - Pin Definitions
// Board: ESP32-S3 Super Mini (ESP32-S3FH4R2, 4MB Flash, 2MB PSRAM)
//
// Front side GPIOs: GP1-GP7 (left), GP8-GP13 (right), TX, RX
// Back side GPIOs:  GP14-GP18, GP21, GP33-GP48
//
// GPIO48 = onboard WS2812 RGB LED — NOT used for floppy signals
// All 11 floppy signals use front-side pins (GP2-GP12) for easy wiring
// =============================================================================

// --- OUTPUT PINS (ESP32 → 74HCT125 U2 @ 5V → Amiga) ---

#define PIN_INDEX         2   // → Amiga Pin 8  (Index pulse, 1x per revolution)
#define PIN_TRACK0        3   // → Amiga Pin 26 (LOW = head at cylinder 0)
#define PIN_WRPROT        4   // → Amiga Pin 28 (LOW = write protected)
#define PIN_RDDATA        5   // → Amiga Pin 30 (MFM bitstream via RMT)
#define PIN_DISKCHANGE    6   // → Amiga Pin 34 (Disk Change / Ready)

// --- INPUT PINS (Amiga → 74HCT125 U3 @ 3.3V → ESP32) ---

#define PIN_DRVSEL0       7   // ← Amiga Pin 10 (Drive Select 0)
#define PIN_MOTOR         8   // ← Amiga Pin 16 (Motor On)
#define PIN_DIRECTION     9   // ← Amiga Pin 18 (LOW = inward/hogere tracks)
#define PIN_STEP         10   // ← Amiga Pin 20 (Step pulse, falling edge)
#define PIN_WRDATA       11   // ← Amiga Pin 22 (Write Data MFM)
#define PIN_SIDE         12   // ← Amiga Pin 32 (LOW = side 0, HIGH = side 1)

// --- STATUS LED ---
#define PIN_LED          48   // Onboard WS2812 RGB LED

// =============================================================================
// 74HCT125 Gate Assignments
// =============================================================================
//
// U2: 74HCT125 @ VCC=5V (output level shifter 3.3V → 5V)
//   Gate 1 (pin 1=OE→GND, 2=IN, 3=OUT): GPIO2 → Amiga pin 8  (INDEX)
//   Gate 2 (pin 4=OE→GND, 5=IN, 6=OUT): GPIO3 → Amiga pin 26 (TRACK0)
//   Gate 3 (pin 10=OE→GND, 9=IN, 8=OUT): GPIO4 → Amiga pin 28 (WRPROT)
//   Gate 4 (pin 13=OE→GND, 12=IN, 11=OUT): GPIO5 → Amiga pin 30 (RDDATA)
//
// U3: 74HCT125 @ VCC=3.3V (input level shifter 5V → 3.3V)
//   Gate 1: Amiga pin 10 → GPIO7  (DRVSEL0)
//   Gate 2: Amiga pin 16 → GPIO8  (MOTOR)
//   Gate 3: Amiga pin 18 → GPIO9  (DIRECTION)
//   Gate 4: Amiga pin 20 → GPIO10 (STEP)
//
// Voltage dividers (1kΩ series + 2kΩ to GND):
//   Amiga pin 22 (5V) → divider → GPIO11 (WRDATA)  → 3.33V
//   Amiga pin 32 (5V) → divider → GPIO12 (SIDE)    → 3.33V
//
// DISKCHANGE (GPIO6 → Amiga pin 34):
//   Option A: NPN transistor (2N2222) + 4.7kΩ pull-up to 5V
//   Option B: 3rd 74HCT125 (1 gate used)

#endif // PINS_H
