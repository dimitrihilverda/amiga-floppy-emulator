#ifndef FLOPPY_H
#define FLOPPY_H

#include <stdint.h>
#include "config.h"
#include "mfm.h"

// =============================================================================
// Floppy Drive Emulator
// Emulates a standard Amiga DD floppy drive (DF0)
// =============================================================================

enum DriveState {
    DRIVE_IDLE,         // Motor off or not selected
    DRIVE_SPIN_UP,      // Motor on, waiting 500ms for spin-up
    DRIVE_READY,        // Motor at speed, ready to stream
    DRIVE_READING       // Actively outputting MFM bitstream via RMT
};

class FloppyDrive {
public:
    FloppyDrive();

    void begin();           // Initialize hardware
    void update();          // Main loop tick (call from loop())

    // Disk management
    bool loadADF(const uint8_t* adfData, size_t adfSize);
    void ejectDisk();
    bool isDiskLoaded() const { return _diskLoaded; }

    // State queries
    DriveState getState() const { return _state; }
    uint8_t getCylinder() const { return _cylinder; }
    uint8_t getSide() const { return _side; }
    uint8_t getTrack() const { return _cylinder * 2 + _side; }

private:
    // --- Drive State ---
    volatile DriveState _state;
    volatile bool _diskLoaded;
    volatile bool _diskChanged;
    volatile bool _writeProtected;

    // --- Amiga signals (updated by ISRs, must be volatile) ---
    volatile bool _motorOn;
    volatile bool _selected;
    volatile uint8_t _cylinder;     // 0-79
    volatile uint8_t _side;         // 0 or 1
    volatile bool _trackDirty;      // Needs re-encode after head move

    // --- Timing ---
    volatile unsigned long _motorStartTime;
    volatile unsigned long _lastStepTime;
    volatile unsigned long _lastIndexTime;
    unsigned long _revolutionStartTime;

    // --- Index Pulse ---
    hw_timer_t* _indexTimer;
    volatile bool _indexPulseActive;
    volatile unsigned long _indexPulseStart;

    // --- ADF Data (in PSRAM) ---
    uint8_t* _adfBuffer;

    // --- MFM Encoding ---
    uint8_t* _mfmTrackBuffer;
    size_t   _mfmTrackLength;
    MFMEncoder _encoder;

    // --- RMT Streaming ---
    bool _rmtActive;
    size_t _rmtBitPosition;

    // --- Internal methods ---
    void setupPins();
    void setupRMT();
    void encodeCurrentTrack();
    const uint8_t* getTrackData(uint8_t cylinder, uint8_t side);
    void startBitstream();
    void stopBitstream();
    void feedRMT();

    // --- ISRs ---
    static FloppyDrive* _instance;
    static void IRAM_ATTR onStepISR();
    static void IRAM_ATTR onMotorISR();
    static void IRAM_ATTR onSideISR();
    static void IRAM_ATTR onDriveSelISR();
    static void IRAM_ATTR onIndexTimer();
};

#endif // FLOPPY_H
