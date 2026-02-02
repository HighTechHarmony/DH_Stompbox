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
// Address for Output Mode (0=Mix, 1=Split)
#define NVRAM_OUTPUT_ADDR 8
// Address for Stop Mode (0=Fade, 1=Immediate)
#define NVRAM_STOPMODE_ADDR 9

extern int currentKey;
// Mode: 0=Major, 1=Minor, 2=Fixed Major, 3=Fixed Minor
extern int currentMode;
extern int currentOctaveShift;
extern bool currentInstrumentIsBass;
extern bool currentMutingEnabled;
extern int currentSynthSound;       // 0=Sine, 1=Organ, etc.
extern volatile int currentArpMode; // 0=Arp, 1=Poly
extern int currentOutputMode;       // 0=Mix, 1=Split
extern int currentStopMode;         // 0=Fade, 1=Immediate

void saveNVRAM();
void loadNVRAM();

#endif // NVRAM_H
