#include <Arduino.h>
#include "config.h"
#include "pins.h"
#include "floppy.h"
#include "webui.h"
#include "mfm_test.h"

// =============================================================================
// Amiga Floppy Emulator - Main Entry Point
//
// ESP32-S3 Mini (N8R8) als standalone floppy drive emulator
// ADF upload via WiFi webinterface (telefoon/browser)
//
// Gebruik:
// 1. Verbind de ESP32-S3 via level shifters met de Amiga floppy poort
// 2. Power on via USB-C
// 3. Verbind met WiFi AP "AmigaFloppy" (wachtwoord: amiga1200)
// 4. Open http://192.168.4.1 in je browser
// 5. Upload een .ADF bestand
// 6. De Amiga ziet een floppy disk en kan booten!
// =============================================================================

// Global floppy drive instance
FloppyDrive floppy;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  AMIGA FLOPPY EMULATOR - ESP32-S3");
    Serial.println("  Rev 1.0 - 2026");
    Serial.println("========================================");
    Serial.println();

    // Check PSRAM
    if (psramFound()) {
        Serial.printf("PSRAM: %d bytes total, %d bytes free\n",
                      ESP.getPsramSize(), ESP.getFreePsram());
    } else {
        Serial.println("WARNING: No PSRAM detected! ADF buffering will be limited.");
    }

    Serial.printf("Flash: %d bytes\n", ESP.getFlashChipSize());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println();

    // Run MFM self-test before anything else
    if (!mfm_selftest()) {
        Serial.println("!!! MFM SELF-TEST FAILED — check encoder !!!");
        Serial.println("Continuing anyway, but disk reads may fail on Amiga.");
    }
    Serial.println();

    // Initialize floppy drive emulation
    floppy.begin();

    // Initialize WiFi web interface
    webui_begin();

    Serial.println();
    Serial.println("=== READY ===");
    Serial.println("Connect to WiFi: " WIFI_SSID);
    Serial.println("Open: http://192.168.4.1");
    Serial.println();
}

void loop() {
    // Floppy drive state machine (timing-critical)
    floppy.update();

    // Web server (non-critical, handled in gaps)
    webui_update();
}
