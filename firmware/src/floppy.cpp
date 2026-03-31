#include "floppy.h"
#include "pins.h"
#include "config.h"
#include <Arduino.h>
#include <driver/rmt.h>

// =============================================================================
// Amiga Floppy Drive Emulator — ESP32-S3
//
// Signal convention: ALL Amiga floppy signals are active LOW
// Output to Amiga: ESP32 LOW = signal active, HIGH = inactive
// Input from Amiga: pin reads LOW = signal active, HIGH = inactive
//
// MFM read data (RDDATA): The Amiga expects pulses where a '1' bit in the
// MFM stream creates a ~2µs LOW pulse, and '0' bits have no pulse.
// The signal idles HIGH (no data / between pulses).
// =============================================================================

FloppyDrive* FloppyDrive::_instance = nullptr;

// =============================================================================
// Constructor
// =============================================================================
FloppyDrive::FloppyDrive()
    : _state(DRIVE_IDLE)
    , _diskLoaded(false)
    , _diskChanged(true)
    , _writeProtected(true)
    , _motorOn(false)
    , _selected(false)
    , _cylinder(0)
    , _side(0)
    , _trackDirty(true)
    , _motorStartTime(0)
    , _lastStepTime(0)
    , _lastIndexTime(0)
    , _revolutionStartTime(0)
    , _indexTimer(nullptr)
    , _indexPulseActive(false)
    , _indexPulseStart(0)
    , _adfBuffer(nullptr)
    , _mfmTrackBuffer(nullptr)
    , _mfmTrackLength(0)
    , _rmtActive(false)
    , _rmtBitPosition(0)
{
    _instance = this;
}

// =============================================================================
// Initialization
// =============================================================================
void FloppyDrive::begin() {
    Serial.println("[FDD] Initializing floppy drive emulator...");

    // Allocate MFM track buffer (prefer PSRAM)
    _mfmTrackBuffer = (uint8_t*)ps_malloc(MFM_TRACK_BUFFER_SIZE);
    if (!_mfmTrackBuffer) {
        Serial.println("[FDD] PSRAM alloc failed, trying heap...");
        _mfmTrackBuffer = (uint8_t*)malloc(MFM_TRACK_BUFFER_SIZE);
    }
    if (!_mfmTrackBuffer) {
        Serial.println("[FDD] FATAL: Cannot allocate MFM buffer!");
        return;
    }

    setupPins();
    setupRMT();

    // Hardware timer for index pulse generation (200ms = 300 RPM)
    // Timer 0, prescaler 80 → 1 tick = 1µs
    _indexTimer = timerBegin(0, 80, true);
    timerAttachInterrupt(_indexTimer, &onIndexTimer, true);
    // Timer is armed when motor starts

    Serial.printf("[FDD] Ready. MFM buf=%d bytes, PSRAM free=%d\n",
                  MFM_TRACK_BUFFER_SIZE, ESP.getFreePsram());
}

// =============================================================================
// GPIO Setup
// =============================================================================
void FloppyDrive::setupPins() {
    // === Outputs to Amiga (active LOW) ===
    pinMode(PIN_INDEX, OUTPUT);
    pinMode(PIN_TRACK0, OUTPUT);
    pinMode(PIN_WRPROT, OUTPUT);
    pinMode(PIN_DISKCHANGE, OUTPUT);
    // PIN_RDDATA is managed by RMT, don't manually configure

    // Default: all inactive (HIGH), except:
    // - WRPROT LOW = disk is write protected (safe default)
    // - DISKCHANGE LOW = disk has changed (no disk yet)
    // - TRACK0: we start at cylinder 0, so active (LOW)
    digitalWrite(PIN_INDEX, HIGH);
    digitalWrite(PIN_TRACK0, LOW);      // We start at track 0
    digitalWrite(PIN_WRPROT, LOW);      // Write protected
    digitalWrite(PIN_DISKCHANGE, LOW);  // Changed (no disk loaded)

    // === Inputs from Amiga (active LOW, use internal pull-ups) ===
    pinMode(PIN_DRVSEL0, INPUT_PULLUP);
    pinMode(PIN_MOTOR, INPUT_PULLUP);
    pinMode(PIN_DIRECTION, INPUT_PULLUP);
    pinMode(PIN_STEP, INPUT_PULLUP);
    pinMode(PIN_WRDATA, INPUT_PULLUP);
    pinMode(PIN_SIDE, INPUT_PULLUP);

    // === Interrupts for time-critical signals ===
    attachInterrupt(digitalPinToInterrupt(PIN_STEP), onStepISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_MOTOR), onMotorISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_SIDE), onSideISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_DRVSEL0), onDriveSelISR, CHANGE);

    // Read initial state of inputs
    _selected = !digitalRead(PIN_DRVSEL0);
    _motorOn = !digitalRead(PIN_MOTOR);
    _side = digitalRead(PIN_SIDE);  // LOW=side0, HIGH=side1

    Serial.printf("[FDD] Pins OK. sel=%d mot=%d side=%d\n",
                  (int)_selected, (int)_motorOn, (int)_side);
}

// =============================================================================
// RMT Peripheral Setup
//
// The RMT (Remote Control Transceiver) generates precise pulse trains.
// We use it to output the MFM bitstream at exactly 2µs per bit cell (DD).
//
// MFM signal encoding for floppy:
//   '1' bit → a short pulse (transition): HIGH for 1µs, LOW for 1µs
//   '0' bit → no transition: hold current level for 2µs
//
// But phloppy_0 / FlashFloppy use a different approach based on run lengths:
// They count consecutive '0' bits between '1' bits and generate pulses
// with variable spacing. This is more efficient for RMT.
// =============================================================================
void FloppyDrive::setupRMT() {
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(
        (gpio_num_t)PIN_RDDATA,
        (rmt_channel_t)RMT_CHANNEL
    );
    config.clk_div = RMT_CLK_DIV;      // 80MHz / 80 = 1MHz → 1µs per tick
    config.mem_block_num = 4;           // 4 × 64 items = 256 RMT items buffer
    config.tx_config.loop_en = false;
    config.tx_config.carrier_en = false;
    config.tx_config.idle_level = RMT_IDLE_LEVEL_HIGH;
    config.tx_config.idle_output_en = true;

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        Serial.printf("[FDD] RMT config error: %s\n", esp_err_to_name(err));
        return;
    }

    // Install driver with TX done callback buffer
    err = rmt_driver_install((rmt_channel_t)RMT_CHANNEL, 0, 0);
    if (err != ESP_OK) {
        Serial.printf("[FDD] RMT install error: %s\n", esp_err_to_name(err));
        return;
    }

    Serial.println("[FDD] RMT configured (1µs tick, DD mode)");
}

// =============================================================================
// Disk Management
// =============================================================================
bool FloppyDrive::loadADF(const uint8_t* adfData, size_t adfSize) {
    if (adfSize != ADF_FILE_SIZE) {
        Serial.printf("[FDD] Bad ADF size: %d (need %d)\n", adfSize, ADF_FILE_SIZE);
        return false;
    }

    // Stop any active streaming
    stopBitstream();

    // Allocate/reallocate in PSRAM
    if (_adfBuffer) free(_adfBuffer);
    _adfBuffer = (uint8_t*)ps_malloc(ADF_FILE_SIZE);
    if (!_adfBuffer) {
        Serial.println("[FDD] FATAL: Cannot allocate ADF buffer!");
        return false;
    }

    memcpy(_adfBuffer, adfData, ADF_FILE_SIZE);
    _diskLoaded = true;
    _diskChanged = true;
    _trackDirty = true;
    _cylinder = 0;
    _side = 0;

    // Assert DISKCHANGE (active LOW) — Amiga will see disk was changed
    digitalWrite(PIN_DISKCHANGE, LOW);
    // Assert TRACK0 (we reset to cylinder 0)
    digitalWrite(PIN_TRACK0, LOW);

    Serial.printf("[FDD] ADF loaded OK (%d bytes, %d tracks)\n",
                  adfSize, TOTAL_TRACKS);
    return true;
}

void FloppyDrive::ejectDisk() {
    stopBitstream();
    _diskLoaded = false;
    _diskChanged = true;
    digitalWrite(PIN_DISKCHANGE, LOW);  // Signal disk change

    if (_adfBuffer) {
        free(_adfBuffer);
        _adfBuffer = nullptr;
    }
    Serial.println("[FDD] Disk ejected");
}

// =============================================================================
// Track Data Access
// =============================================================================
const uint8_t* FloppyDrive::getTrackData(uint8_t cylinder, uint8_t side) {
    if (!_adfBuffer || cylinder >= TRACKS_PER_DISK || side > 1) return nullptr;
    // ADF: sequential tracks — cyl0/side0, cyl0/side1, cyl1/side0, ...
    uint32_t offset = ((uint32_t)cylinder * 2 + side) * TRACK_DATA_SIZE;
    return &_adfBuffer[offset];
}

void FloppyDrive::encodeCurrentTrack() {
    if (!_diskLoaded || !_mfmTrackBuffer) return;

    const uint8_t* td = getTrackData(_cylinder, _side);
    if (!td) return;

    unsigned long t0 = micros();
    _mfmTrackLength = _encoder.encodeTrack(td, _cylinder, _side,
                                            _mfmTrackBuffer, MFM_TRACK_BUFFER_SIZE);
    unsigned long dt = micros() - t0;

    Serial.printf("[FDD] Encoded cyl=%d side=%d → %d bytes in %luµs\n",
                  (int)_cylinder, (int)_side, _mfmTrackLength, dt);
    _trackDirty = false;
}

// =============================================================================
// RMT Bitstream Output
//
// Approach: convert MFM byte stream to RMT pulse items.
// We use a "run length" method similar to phloppy_0:
// - Scan the MFM bitstream for '1' bits
// - Count the distance (in bit cells) between consecutive '1's
// - Generate one RMT item per interval: a short LOW pulse followed by
//   a HIGH period sized to fill the gap
//
// This is more efficient than one RMT item per bit, as it compresses
// long runs of '0's into a single RMT entry.
// =============================================================================
void FloppyDrive::startBitstream() {
    if (_rmtActive || !_diskLoaded) return;
    if (_trackDirty) encodeCurrentTrack();

    _rmtBitPosition = 0;
    _rmtActive = true;
    _revolutionStartTime = micros();
}

void FloppyDrive::stopBitstream() {
    if (!_rmtActive) return;
    rmt_tx_stop((rmt_channel_t)RMT_CHANNEL);
    _rmtActive = false;
}

void FloppyDrive::feedRMT() {
    if (!_rmtActive || !_mfmTrackBuffer || _mfmTrackLength == 0) return;

    // Generate RMT items using run-length encoding
    // Each item represents the time between two '1' pulses in the MFM stream
    static const int MAX_ITEMS = 128;
    rmt_item32_t items[MAX_ITEMS];
    int itemCount = 0;

    size_t totalBits = _mfmTrackLength * 8;
    int runLength = 0;  // Bit cells since last '1'

    while (itemCount < MAX_ITEMS && _rmtBitPosition < totalBits) {
        size_t bytePos = _rmtBitPosition / 8;
        int bitIdx = 7 - (_rmtBitPosition % 8);
        uint8_t bit = (_mfmTrackBuffer[bytePos] >> bitIdx) & 1;

        _rmtBitPosition++;
        runLength++;

        if (bit == 1) {
            // Found a '1' — emit a pulse
            // The pulse timing encodes the distance from the previous '1'
            // Each bit cell = 2µs = 2 RMT ticks
            //
            // Run length 2 (pattern "10"): pulse at 4µs → 2 ticks HIGH + 2 ticks LOW
            // Run length 3 (pattern "100"): pulse at 6µs → 4 ticks HIGH + 2 ticks LOW
            // Run length 4 (pattern "1000"): pulse at 8µs → 6 ticks HIGH + 2 ticks LOW
            //
            // HIGH period = (runLength - 1) × 2 ticks
            // LOW period = 2 ticks (the pulse itself is ~1µs)

            uint16_t highTicks = (runLength - 1) * RMT_TICKS_PER_BIT;
            uint16_t lowTicks = RMT_TICKS_PER_BIT;

            if (highTicks == 0) highTicks = RMT_TICKS_PER_BIT;

            items[itemCount].level0 = 1;            // HIGH (idle)
            items[itemCount].duration0 = highTicks;
            items[itemCount].level1 = 0;            // LOW (pulse)
            items[itemCount].duration1 = lowTicks;
            itemCount++;

            runLength = 0;
        }

        // Safety: if we've gone too long without a '1', something is wrong
        if (runLength > 10) {
            // Emit a synthetic pulse to keep the Amiga's PLL locked
            items[itemCount].level0 = 1;
            items[itemCount].duration0 = runLength * RMT_TICKS_PER_BIT;
            items[itemCount].level1 = 0;
            items[itemCount].duration1 = RMT_TICKS_PER_BIT;
            itemCount++;
            runLength = 0;
        }
    }

    // Loop track (continuous rotation simulation)
    if (_rmtBitPosition >= totalBits) {
        _rmtBitPosition = 0;
        _revolutionStartTime = micros();
    }

    // Send items to RMT (blocking=false so we return quickly)
    if (itemCount > 0) {
        rmt_write_items((rmt_channel_t)RMT_CHANNEL, items, itemCount, false);
    }
}

// =============================================================================
// Interrupt Service Routines
// =============================================================================

// STEP pulse: Amiga sends falling edge to move head one cylinder
void IRAM_ATTR FloppyDrive::onStepISR() {
    if (!_instance) return;
    FloppyDrive* d = _instance;

    // Debounce
    unsigned long now = micros();
    if (now - d->_lastStepTime < STEP_DEBOUNCE_US) return;
    d->_lastStepTime = now;

    // DIRECTION pin: LOW = inward (toward higher cylinder numbers)
    bool inward = !digitalRead(PIN_DIRECTION);

    if (inward) {
        if (d->_cylinder < TRACKS_PER_DISK - 1) {
            d->_cylinder++;
            d->_trackDirty = true;
        }
    } else {
        if (d->_cylinder > 0) {
            d->_cylinder--;
            d->_trackDirty = true;
        }
    }

    // Update TRACK0 signal
    digitalWrite(PIN_TRACK0, (d->_cylinder == 0) ? LOW : HIGH);

    // Amiga disk change protocol: DISKCHANGE is cleared (set HIGH)
    // on the first STEP pulse after a disk insert
    if (d->_diskChanged && d->_diskLoaded) {
        d->_diskChanged = false;
        digitalWrite(PIN_DISKCHANGE, HIGH);  // HIGH = no change (inactive)
    }
}

// MOTOR signal changed
void IRAM_ATTR FloppyDrive::onMotorISR() {
    if (!_instance) return;
    FloppyDrive* d = _instance;

    bool active = !digitalRead(PIN_MOTOR);
    if (active != d->_motorOn) {
        d->_motorOn = active;
        if (active) {
            d->_motorStartTime = millis();
        }
    }
}

// SIDE SELECT changed
void IRAM_ATTR FloppyDrive::onSideISR() {
    if (!_instance) return;
    FloppyDrive* d = _instance;

    // Side select: LOW = side 0 (bottom), HIGH = side 1 (top)
    uint8_t newSide = digitalRead(PIN_SIDE) ? 1 : 0;
    if (newSide != d->_side) {
        d->_side = newSide;
        d->_trackDirty = true;
    }
}

// DRIVE SELECT changed
void IRAM_ATTR FloppyDrive::onDriveSelISR() {
    if (!_instance) return;
    _instance->_selected = !digitalRead(PIN_DRVSEL0);
}

// INDEX TIMER: fires every 200ms (= 300 RPM)
void IRAM_ATTR FloppyDrive::onIndexTimer() {
    if (!_instance) return;
    FloppyDrive* d = _instance;

    if (d->_motorOn && d->_selected) {
        // Generate index pulse (active LOW)
        digitalWrite(PIN_INDEX, LOW);
        d->_indexPulseActive = true;
        d->_indexPulseStart = micros();  // Record when pulse started
    }
}

// =============================================================================
// Main State Machine (called from loop())
// =============================================================================
void FloppyDrive::update() {
    unsigned long now = millis();
    unsigned long nowUs = micros();

    // --- End index pulse after INDEX_PULSE_US ---
    if (_indexPulseActive) {
        if (nowUs - _indexPulseStart >= INDEX_PULSE_US) {
            digitalWrite(PIN_INDEX, HIGH);  // Deassert (inactive)
            _indexPulseActive = false;
        }
    }

    // --- State machine ---
    switch (_state) {

    case DRIVE_IDLE:
        if (_selected && _motorOn) {
            _state = DRIVE_SPIN_UP;
            _motorStartTime = now;
            Serial.println("[FDD] → SPIN_UP");
        }
        break;

    case DRIVE_SPIN_UP:
        if (!_selected || !_motorOn) {
            _state = DRIVE_IDLE;
            stopBitstream();
            timerAlarmDisable(_indexTimer);
            Serial.println("[FDD] → IDLE (deselected/motor off)");
            break;
        }
        // Wait for spin-up time
        if (now - _motorStartTime >= MOTOR_SPINUP_MS) {
            _state = DRIVE_READY;
            // Start 200ms index pulse timer
            timerAlarmWrite(_indexTimer, REV_TIME_US, true);
            timerAlarmEnable(_indexTimer);
            Serial.println("[FDD] → READY");
        }
        break;

    case DRIVE_READY:
        if (!_selected || !_motorOn) {
            _state = DRIVE_IDLE;
            stopBitstream();
            timerAlarmDisable(_indexTimer);
            Serial.println("[FDD] → IDLE");
            break;
        }
        if (_diskLoaded) {
            _state = DRIVE_READING;
            startBitstream();
            Serial.println("[FDD] → READING");
        }
        break;

    case DRIVE_READING:
        if (!_selected || !_motorOn) {
            _state = DRIVE_IDLE;
            stopBitstream();
            timerAlarmDisable(_indexTimer);
            Serial.println("[FDD] → IDLE");
            break;
        }
        if (!_diskLoaded) {
            _state = DRIVE_READY;
            stopBitstream();
            Serial.println("[FDD] → READY (ejected)");
            break;
        }

        // Re-encode track after head movement
        if (_trackDirty) {
            // Brief pause in streaming while we encode
            rmt_tx_stop((rmt_channel_t)RMT_CHANNEL);
            encodeCurrentTrack();
            _rmtBitPosition = 0;
        }

        // Feed next batch of MFM data to RMT
        feedRMT();
        break;
    }
}
