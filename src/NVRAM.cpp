#include "NVRAM.h"
#include <EEPROM.h>

// Define global variables declared as extern in NVRAM.h
int currentKey = 0;             // 0=C, 1=C#, 2=D, etc. (chromatic scale)
bool currentModeIsMajor = true; // true=major, false=minor
int currentOctaveShift = 0;     // -1..2

void saveNVRAM()
{
    EEPROM.write(NVRAM_SIGNATURE_ADDR, NVRAM_SIGNATURE);
    EEPROM.write(NVRAM_KEY_ADDR, (uint8_t)currentKey);
    EEPROM.write(NVRAM_MODE_ADDR, (uint8_t)(currentModeIsMajor ? 1 : 0));
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
        currentModeIsMajor = (m == 1);
        Serial.print("NVRAM: loaded key=");
        Serial.print(currentKey);
        Serial.print(" mode=");
        Serial.println(currentModeIsMajor ? "Major" : "Minor");
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
    }
    else
    {
        // Not initialized: use defaults and write them
        Serial.println("NVRAM: empty, using default C Major");
        currentKey = 0; // C
        currentModeIsMajor = true;
        saveNVRAM();
    }
}
