#ifndef PITCH_H
#define PITCH_H

#include <Arduino.h>
#include <Audio.h>

// Pitch detection object
extern AudioAnalyzeNoteFrequency noteDetect;

// Pitch tracking state
extern float lastDetectedFrequency;

void setupPitchDetection();
void updatePitchDetection(float &frequency, float &probability, const char *&noteName, bool currentInstrumentIsBass);

// Reset pitch detection state (call when starting fresh sampling)
void resetPitchDetection();

// Connect/disconnect the noteDetect input.  YIN only runs when connected,
// so disconnect whenever FS1 is not held to save CPU (and SPI headroom
// for SD sample streaming).
void enablePitchDetection();
void disablePitchDetection();

#endif // PITCH_H
