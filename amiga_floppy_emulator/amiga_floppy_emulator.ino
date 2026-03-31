// =============================================================================
// AMIGA FLOPPY EMULATOR - ESP32-S3
//
// Standalone DD floppy drive emulator for Commodore Amiga.
// Upload ADF files via WiFi (connect to "AmigaFloppy" AP, open 192.168.4.1)
//
// Hardware: ESP32-S3 Mini N8R8 + 2x 74HCT125 + 34-pin IDC
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <driver/rmt.h>

#include "config.h"
#include "pins.h"
#include "mfm.h"
#include "floppy.h"
#include "webui.h"
#include "mfm_test.h"

// Global floppy drive instance
FloppyDrive floppy;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  AMIGA FLOPPY EMULATOR - ESP32-S3");
    Serial.println("  Rev 1.0");
    Serial.println("========================================");
    Serial.println();

    // Check PSRAM
    if (psramFound()) {
        Serial.printf("PSRAM: %d bytes total, %d bytes free\n",
                      ESP.getPsramSize(), ESP.getFreePsram());
    } else {
        Serial.println("WARNING: No PSRAM detected!");
    }
    Serial.printf("Flash: %d bytes\n", ESP.getFlashChipSize());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println();

    // Run MFM self-test
    if (!mfm_selftest()) {
        Serial.println("!!! MFM SELF-TEST FAILED !!!");
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
    floppy.update();
    webui_update();
}
