#include "NVRAM.h"
#include <EEPROM.h>
#include "audio.h"

// Define global variables declared as extern in NVRAM.h
int currentKey = 0;                   // 0=C, 1=C#, 2=D, etc. (chromatic scale)
int currentMode = 0;                  // 0=Major, 1=Minor, 2=Fixed Major, 3=Fixed Minor
int currentOctaveShift = 0;           // -1..2
bool currentInstrumentIsBass = false; // false=Guitar (default), true=Bass
// Muting setting persisted (false=Disabled, true=Enabled)
bool currentMutingEnabled = false;
int currentSynthSound = 0; // 0=Sine (default), 1=Organ
int currentOutputMode = 0; // 0=Mix, 1=Split

void saveNVRAM()
{
    EEPROM.write(NVRAM_SIGNATURE_ADDR, NVRAM_SIGNATURE);
    EEPROM.write(NVRAM_KEY_ADDR, (uint8_t)currentKey);
    // store mode as 0..3
    uint8_t modeVal = (uint8_t)currentMode;
    if (modeVal > 3)
        modeVal = 0;
    EEPROM.write(NVRAM_MODE_ADDR, modeVal);
    EEPROM.write(NVRAM_BASSGUIT_ADDR, (uint8_t)(currentInstrumentIsBass ? 1 : 0));
    EEPROM.write(NVRAM_MUTING_ADDR, (uint8_t)(currentMutingEnabled ? 1 : 0));
    EEPROM.write(NVRAM_SYNTHSND_ADDR, (uint8_t)currentSynthSound);
    EEPROM.write(NVRAM_ARP_ADDR, (uint8_t)currentArpMode);
    EEPROM.write(NVRAM_OUTPUT_ADDR, (uint8_t)currentOutputMode);
    // store octave shifted by +2 to fit into unsigned byte (valid -1..2 -> 1..4)
    int8_t enc = currentOctaveShift + 2;
    if (enc < 0)
        enc = 0;
    if (enc > 255)
        enc = 255;
    EEPROM.write(NVRAM_OCTAVE_ADDR, (uint8_t)enc);
}

void loadNVRAM()
{
    if (EEPROM.read(NVRAM_SIGNATURE_ADDR) == NVRAM_SIGNATURE)
    {
        uint8_t k = EEPROM.read(NVRAM_KEY_ADDR);
        if (k < 12)
            currentKey = k;
        uint8_t m = EEPROM.read(NVRAM_MODE_ADDR);
        if (m <= 3)
            currentMode = m;
        else
            currentMode = 0;
        Serial.print("NVRAM: loaded key=");
        Serial.print(currentKey);
        const char *modeNames[] = {"Major", "Minor", "Fixed Major", "Fixed Minor"};
        Serial.print(" mode=");
        Serial.println(modeNames[currentMode]);
        // load octave (stored as +2 offset)
        uint8_t oe = EEPROM.read(NVRAM_OCTAVE_ADDR);
        if (oe >= 1 && oe <= 5) // sanity check (1..5 corresponds to -1..3 but we expect 1..4)
        {
            int8_t decoded = (int8_t)oe - 2;
            if (decoded >= -1 && decoded <= 2)
            {
                currentOctaveShift = decoded;
            }
        }
        Serial.print(" octave=");
        Serial.println(currentOctaveShift);
        // load instrument mode (0 = Guitar, 1 = Bass)
        uint8_t ig = EEPROM.read(NVRAM_BASSGUIT_ADDR);
        currentInstrumentIsBass = (ig == 1);
        Serial.print(" instrument=");
        Serial.println(currentInstrumentIsBass ? "Bass" : "Guitar");
        // load muting (0 = Disabled, 1 = Enabled)
        uint8_t mu = EEPROM.read(NVRAM_MUTING_ADDR);
        currentMutingEnabled = (mu == 1);
        Serial.print(" muting=");
        Serial.println(currentMutingEnabled ? "Enabled" : "Disabled");
        // load synth sound (0 = Sine, 1 = Organ, 2 = Rhodes, 3 = Strings)
        uint8_t ss = EEPROM.read(NVRAM_SYNTHSND_ADDR);
        if (ss <= 3) // validate range
            currentSynthSound = ss;
        Serial.print(" synthSound=");
        const char *soundNames[] = {"Sine", "Organ", "Rhodes", "Strings"};
        Serial.println(soundNames[currentSynthSound]);
        // load arp mode (0 = Arp, 1 = Poly)
        uint8_t ar = EEPROM.read(NVRAM_ARP_ADDR);
        if (ar <= 1) // validate range
            currentArpMode = ar;
        Serial.print(" arpMode=");
        const char *arpNames[] = {"Arp", "Poly"};
        Serial.println(arpNames[currentArpMode]);
        // load output mode (0 = Mix, 1 = Split)
        uint8_t om = EEPROM.read(NVRAM_OUTPUT_ADDR);
        if (om <= 1) // validate range
            currentOutputMode = om;
        Serial.print(" outputMode=");
        const char *outputNames[] = {"Mix", "Split"};
        Serial.println(outputNames[currentOutputMode]);
    }
    else
    {
        // Not initialized: use defaults and write them
        Serial.println("NVRAM: empty, using default C Major");
        currentKey = 0;                  // C
        currentMode = 0;                 // Major
        currentInstrumentIsBass = false; // default to Guitar
        currentMutingEnabled = false;    // default muting disabled        currentSynthSound = 0; // default to Sine        saveNVRAM();
    }
}
