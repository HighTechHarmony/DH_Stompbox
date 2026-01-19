#include "menu.h"
#include "NVRAM.h"
#include "audio.h"

// Menu state
MenuLevel currentMenuLevel = MENU_TOP;
int menuTopIndex = 0;      // 0=Key, 1=Mode
int menuKeyIndex = 0;      // 0-11 for key selection
int menuModeIndex = 0;     // 0=Major, 1=Minor
int menuOctaveIndex = 1;   // index into octave options (default 0)
int menuBassGuitIndex = 0; // 0=Bass, 1=Guitar

// Viewport tracking for scrolling submenus
int keyViewportStart = 0;
int modeViewportStart = 0;
int octaveViewportStart = 0;
int topViewportStart = 0;
int bassGuitViewportStart = 0;
int mutingViewportStart = 0;

// Menu display names
const char *menuTopItems[] = {"MusicKey", "Maj/Min", "Octave", "Bass/Gtr", "Muting"};
const int MENU_TOP_COUNT = 5;

const char *keyMenuNames[] = {"A", "Bb", "B", "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab"};
const int KEY_MENU_COUNT = 12;
// Map menu index to chromatic scale (C=0, C#=1, ... B=11)
const int keyMenuToChromatic[] = {9, 10, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8}; // A, Bb, B, C, C#, D, D#, E, F, F#, G, G#

const char *modeMenuNames[] = {"Major", "Minor"};
const int MODE_MENU_COUNT = 2;

const char *bassGuitMenuNames[] = {"Bass", "Guitar"};
const int BASSGUIT_MENU_COUNT = 2;

// Octave options
const int octaveOptions[] = {-1, 0, 1, 2};
const char *octaveMenuNames[] = {"-1", "0", "1", "2"};
const int OCTAVE_MENU_COUNT = 4;

// Muting options
const char *mutingMenuNames[] = {"Disabled", "Enabled"};
const int MUTING_MENU_COUNT = 2;
int menuMutingIndex = 0; // 0=Disabled, 1=Enabled

void handleMenuEncoder(int delta)
{
    if (currentMenuLevel == MENU_TOP)
    {
        menuTopIndex += delta;
        if (menuTopIndex < 0)
            menuTopIndex = 0;
        if (menuTopIndex > MENU_TOP_COUNT)
            menuTopIndex = MENU_TOP_COUNT; // allow Parent as final entry
    }
    else if (currentMenuLevel == MENU_KEY_SELECT)
    {
        menuKeyIndex += delta;
        if (menuKeyIndex < 0)
            menuKeyIndex = 0;
        if (menuKeyIndex > KEY_MENU_COUNT)
            menuKeyIndex = KEY_MENU_COUNT; // allow Parent
    }
    else if (currentMenuLevel == MENU_MODE_SELECT)
    {
        menuModeIndex += delta;
        if (menuModeIndex < 0)
            menuModeIndex = 0;
        if (menuModeIndex > MODE_MENU_COUNT)
            menuModeIndex = MODE_MENU_COUNT; // allow Parent
    }
    else if (currentMenuLevel == MENU_OCTAVE_SELECT)
    {
        menuOctaveIndex += delta;
        if (menuOctaveIndex < 0)
            menuOctaveIndex = 0;
        if (menuOctaveIndex > OCTAVE_MENU_COUNT)
            menuOctaveIndex = OCTAVE_MENU_COUNT; // allow Parent
    }
    else if (currentMenuLevel == MENU_BASSGUIT_SELECT)
    {
        menuBassGuitIndex += delta;
        if (menuBassGuitIndex < 0)
            menuBassGuitIndex = 0;
        if (menuBassGuitIndex > BASSGUIT_MENU_COUNT)
            menuBassGuitIndex = BASSGUIT_MENU_COUNT; // allow Parent
    }
    else if (currentMenuLevel == MENU_MUTING_SELECT)
    {
        menuMutingIndex += delta;
        if (menuMutingIndex < 0)
            menuMutingIndex = 0;
        if (menuMutingIndex > MUTING_MENU_COUNT)
            menuMutingIndex = MUTING_MENU_COUNT; // allow Parent
    }
}

void handleMenuButton()
{
    if (currentMenuLevel == MENU_TOP)
    {
        // Enter submenu based on selected top-level item or handle Parent
        if (menuTopIndex == 0) // Key
        {
            currentMenuLevel = MENU_KEY_SELECT;
            // Initialize to current key
            for (int i = 0; i < KEY_MENU_COUNT; i++)
            {
                if (keyMenuToChromatic[i] == currentKey)
                {
                    menuKeyIndex = i;
                    break;
                }
            }
        }
        else if (menuTopIndex == 1) // Mode
        {
            currentMenuLevel = MENU_MODE_SELECT;
            menuModeIndex = currentModeIsMajor ? 0 : 1;
        }
        else if (menuTopIndex == 2) // Octave
        {
            currentMenuLevel = MENU_OCTAVE_SELECT;
            // Initialize octave index from currentOctaveShift
            for (int i = 0; i < OCTAVE_MENU_COUNT; i++)
            {
                if (octaveOptions[i] == currentOctaveShift)
                {
                    menuOctaveIndex = i;
                    break;
                }
            }
        }
        else if (menuTopIndex == 3) // Bass/Guit
        {
            currentMenuLevel = MENU_BASSGUIT_SELECT;
            // Initialize to current instrument setting (0=Bass,1=Guitar)
            menuBassGuitIndex = currentInstrumentIsBass ? 0 : 1;
        }
        else if (menuTopIndex == 4) // Muting
        {
            currentMenuLevel = MENU_MUTING_SELECT;
            // Initialize to current muting setting (0=Disabled,1=Enabled)
            menuMutingIndex = currentMutingEnabled ? 1 : 0;
        }
    }
    else if (currentMenuLevel == MENU_KEY_SELECT)
    {
        // If Parent selected, just return to top. Otherwise apply key.
        if (menuKeyIndex == KEY_MENU_COUNT)
        {
            currentMenuLevel = MENU_TOP;
        }
        else
        {
            currentKey = keyMenuToChromatic[menuKeyIndex];
            saveNVRAM();
            currentMenuLevel = MENU_TOP;
        }
    }
    else if (currentMenuLevel == MENU_MODE_SELECT)
    {
        // If Parent selected, return to top. Otherwise apply mode.
        if (menuModeIndex == MODE_MENU_COUNT)
        {
            currentMenuLevel = MENU_TOP;
        }
        else
        {
            currentModeIsMajor = (menuModeIndex == 0);
            saveNVRAM();
            currentMenuLevel = MENU_TOP;
        }
    }
    else if (currentMenuLevel == MENU_OCTAVE_SELECT)
    {
        // If Parent selected, return to top. Otherwise apply octave shift.
        if (menuOctaveIndex == OCTAVE_MENU_COUNT)
        {
            currentMenuLevel = MENU_TOP;
        }
        else
        {
            currentOctaveShift = octaveOptions[menuOctaveIndex];
            saveNVRAM();
            // apply immediately if chord active
            if (chordActive)
            {
                updateChordTonic(currentChordTonic, currentKey, currentModeIsMajor);
            }
            currentMenuLevel = MENU_TOP;
        }
    }
    else if (currentMenuLevel == MENU_BASSGUIT_SELECT)
    {
        // If Parent selected, return to top. Otherwise apply instrument mode.
        if (menuBassGuitIndex == BASSGUIT_MENU_COUNT)
        {
            currentMenuLevel = MENU_TOP;
        }
        else
        {
            // menuBassGuitIndex: 0=Bass,1=Guitar
            currentInstrumentIsBass = (menuBassGuitIndex == 0);
            saveNVRAM();
            currentMenuLevel = MENU_TOP;
        }
    }
    else if (currentMenuLevel == MENU_MUTING_SELECT)
    {
        // If Parent selected, return to top. Otherwise apply muting setting.
        if (menuMutingIndex == MUTING_MENU_COUNT)
        {
            currentMenuLevel = MENU_TOP;
        }
        else
        {
            // menuMutingIndex: 0=Disabled,1=Enabled
            currentMutingEnabled = (menuMutingIndex == 1);
            saveNVRAM();
            currentMenuLevel = MENU_TOP;
        }
    }
}
