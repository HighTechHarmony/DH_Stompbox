#ifndef NVRAM_H
#define NVRAM_H

#include <Arduino.h>

// NVRAM (EEPROM) layout
#define NVRAM_SIGNATURE_ADDR 0
#define NVRAM_SIGNATURE 0xA5
#define NVRAM_KEY_ADDR 1
#define NVRAM_MODE_ADDR 2
#define NVRAM_OCTAVE_ADDR 3

extern int currentKey;
extern bool currentModeIsMajor;
extern int currentOctaveShift;

void saveNVRAM();
void loadNVRAM();

#endif // NVRAM_H
