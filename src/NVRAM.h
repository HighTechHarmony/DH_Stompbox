#ifndef NVRAM_H
#define NVRAM_H

#include <Arduino.h>

// NVRAM (EEPROM) layout
#define NVRAM_SIGNATURE_ADDR 0
#define NVRAM_SIGNATURE 0xA5
#define NVRAM_KEY_ADDR 1
#define NVRAM_MODE_ADDR 2
#define NVRAM_OCTAVE_ADDR 3
// Address for Bass/Guitar mode (0=Guitar, 1=Bass)
#define NVRAM_BASSGUIT_ADDR 4
// Address for Muting (0=Disabled, 1=Enabled)
#define NVRAM_MUTING_ADDR 5
// Address for Synth Sound (0=Sine, 1=Organ, etc.)
#define NVRAM_SYNTHSND_ADDR 6
// Address for Arp Mode (0=Arp, 1=Poly)
#define NVRAM_ARP_ADDR 7

extern int currentKey;
extern bool currentModeIsMajor;
extern int currentOctaveShift;
extern bool currentInstrumentIsBass;
extern bool currentMutingEnabled;
extern int currentSynthSound; // 0=Sine, 1=Organ, etc.
extern int currentArpMode; // 0=Arp, 1=Poly

void saveNVRAM();
void loadNVRAM();

#endif // NVRAM_H
