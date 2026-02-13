#include "menu.h"
#include "NVRAM.h"
#include "audio.h"
#include "sdcard.h"

// Menu state
MenuLevel currentMenuLevel = MENU_TOP;
int menuTopIndex = 0;      // 0=Key, 1=Mode
int menuKeyIndex = 0;      // 0-11 for key selection
int menuModeIndex = 0;     // 0=Major, 1=Minor
int menuOctaveIndex = 1;   // index into octave options (default 0)
int menuBassGuitIndex = 0; // 0=Bass, 1=Guitar
int menuSynthSndIndex = 0; // 0=Sine, 1=Organ
int menuArpIndex = 1;      // 0=Arp, 1=Poly (default Poly)
int menuConfigIndex = 0;   // 0=Bass/Gtr, 1=Muting, 2=Output
int menuOutputIndex = 0;   // 0=Mix, 1=Split

// Viewport tracking for scrolling submenus
int keyViewportStart = 0;
int modeViewportStart = 0;
int octaveViewportStart = 0;
int topViewportStart = 0;
int bassGuitViewportStart = 0;
int mutingViewportStart = 0;
int synthSndViewportStart = 0;
int arpViewportStart = 0;
int configViewportStart = 0;
int outputViewportStart = 0;
int stopModeViewportStart = 0;

// Menu display names
const char *menuTopItems[] = {"MusicKey", "Maj/Min", "Octave", "SynthSnd", "Arp/Poly", "Config"};
const int MENU_TOP_COUNT = 6;

const char *keyMenuNames[] = {"A", "Bb", "B", "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab"};
const int KEY_MENU_COUNT = 12;
// Map menu index to chromatic scale (C=0, C#=1, ... B=11)
const int keyMenuToChromatic[] = {9, 10, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8}; // A, Bb, B, C, C#, D, D#, E, F, F#, G, G#

const char *modeMenuNames[] = {"Major", "Minor", "Fixed Ma", "Fixed Mi"};
const int MODE_MENU_COUNT = 4;

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

// Synth sound options
const char *synthSndMenuNames[] = {"Sine", "Organ", "Rhodes", "Strings", "SDcard"};
const int SYNTHSND_MENU_COUNT = 5;

// Arpeggiator options
const char *arpMenuNames[] = {"Arp", "Poly"};
const int ARP_MENU_COUNT = 2;

// Config submenu options
const char *configMenuNames[] = {"Bass/Gtr", "Muting", "Output", "StopMode"};
const int CONFIG_MENU_COUNT = 4;

// Output options
const char *outputMenuNames[] = {"Mix", "Split"};
const int OUTPUT_MENU_COUNT = 2;

// Stop mode options
const char *stopModeMenuNames[] = {"Fade", "Immediate"};
const int STOPMODE_MENU_COUNT = 2;
int menuStopModeIndex = 0; // 0=Fade, 1=Immediate

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
    else if (currentMenuLevel == MENU_SYNTHSND_SELECT)
    {
        menuSynthSndIndex += delta;
        if (menuSynthSndIndex < 0)
            menuSynthSndIndex = 0;
        if (menuSynthSndIndex > SYNTHSND_MENU_COUNT)
            menuSynthSndIndex = SYNTHSND_MENU_COUNT; // allow Parent
    }
    else if (currentMenuLevel == MENU_SDCARD_BROWSE)
    {
        int total = sdTotalVisibleCount();
        sdBrowseIndex += delta;
        if (sdBrowseIndex < 0)
            sdBrowseIndex = 0;
        if (sdBrowseIndex >= total)
            sdBrowseIndex = total - 1;
    }
    else if (currentMenuLevel == MENU_ARP_SELECT)
    {
        menuArpIndex += delta;
        if (menuArpIndex < 0)
            menuArpIndex = 0;
        if (menuArpIndex > ARP_MENU_COUNT)
            menuArpIndex = ARP_MENU_COUNT; // allow Parent
    }
    else if (currentMenuLevel == MENU_CONFIG_SELECT)
    {
        menuConfigIndex += delta;
        if (menuConfigIndex < 0)
            menuConfigIndex = 0;
        if (menuConfigIndex > CONFIG_MENU_COUNT)
            menuConfigIndex = CONFIG_MENU_COUNT; // allow Parent
    }
    else if (currentMenuLevel == MENU_OUTPUT_SELECT)
    {
        menuOutputIndex += delta;
        if (menuOutputIndex < 0)
            menuOutputIndex = 0;
        if (menuOutputIndex > OUTPUT_MENU_COUNT)
            menuOutputIndex = OUTPUT_MENU_COUNT; // allow Parent
    }
    else if (currentMenuLevel == MENU_STOPMODE_SELECT)
    {
        menuStopModeIndex += delta;
        if (menuStopModeIndex < 0)
            menuStopModeIndex = 0;
        if (menuStopModeIndex > STOPMODE_MENU_COUNT)
            menuStopModeIndex = STOPMODE_MENU_COUNT; // allow Parent
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
            // initialize to current mode (0..3)
            menuModeIndex = currentMode;
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
        else if (menuTopIndex == 3) // SynthSnd (was index 5)
        {
            currentMenuLevel = MENU_SYNTHSND_SELECT;
            // Initialize to current synth sound setting
            menuSynthSndIndex = currentSynthSound;
        }
        else if (menuTopIndex == 4) // Arp/Poly
        {
            currentMenuLevel = MENU_ARP_SELECT;
            // Initialize to current arp mode (0=Arp, 1=Poly)
            menuArpIndex = currentArpMode;
        }
        else if (menuTopIndex == 5) // Config
        {
            currentMenuLevel = MENU_CONFIG_SELECT;
            menuConfigIndex = 0;
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
            currentMode = menuModeIndex; // 0..3
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
                updateChordTonic(currentChordTonic, currentKey, currentMode);
            }
            currentMenuLevel = MENU_TOP;
        }
    }
    else if (currentMenuLevel == MENU_BASSGUIT_SELECT)
    {
        // If Parent selected, return to Config. Otherwise apply instrument mode.
        if (menuBassGuitIndex == BASSGUIT_MENU_COUNT)
        {
            currentMenuLevel = MENU_CONFIG_SELECT;
        }
        else
        {
            // menuBassGuitIndex: 0=Bass,1=Guitar
            currentInstrumentIsBass = (menuBassGuitIndex == 0);
            saveNVRAM();
            currentMenuLevel = MENU_CONFIG_SELECT;
        }
    }
    else if (currentMenuLevel == MENU_MUTING_SELECT)
    {
        // If Parent selected, return to Config. Otherwise apply muting setting.
        if (menuMutingIndex == MUTING_MENU_COUNT)
        {
            currentMenuLevel = MENU_CONFIG_SELECT;
        }
        else
        {
            // menuMutingIndex: 0=Disabled,1=Enabled
            currentMutingEnabled = (menuMutingIndex == 1);
            saveNVRAM();
            currentMenuLevel = MENU_CONFIG_SELECT;
        }
    }
    else if (currentMenuLevel == MENU_SYNTHSND_SELECT)
    {
        // If Parent selected, return to top.
        if (menuSynthSndIndex == SYNTHSND_MENU_COUNT)
        {
            currentMenuLevel = MENU_TOP;
        }
        else if (menuSynthSndIndex == 4) // SDcard
        {
            // Enter SD card browser as a sub-submenu of SynthSnd
            currentMenuLevel = MENU_SDCARD_BROWSE;
            if (!sdCardAvailable)
                initSDCard();
            else
                scanDirectory(sdCurrentPath);
        }
        else
        {
            currentSynthSound = menuSynthSndIndex;
            saveNVRAM();
            currentMenuLevel = MENU_TOP;
        }
    }
    else if (currentMenuLevel == MENU_SDCARD_BROWSE)
    {
        // Determine what the current browse index points to.
        // Layout: [..] (if not root) | entries... | ^ (exit)
        bool atRoot = sdAtRoot();
        int total = sdTotalVisibleCount();
        int exitIdx = total - 1;          // last row is always "^"
        int dotdotIdx = atRoot ? -1 : 0;  // ".." row index (or -1 if root)
        int entryOffset = atRoot ? 0 : 1; // first sdEntries[] row index

        if (sdBrowseIndex == exitIdx)
        {
            // "^" – return to SynthSnd submenu
            currentMenuLevel = MENU_SYNTHSND_SELECT;
        }
        else if (!atRoot && sdBrowseIndex == dotdotIdx)
        {
            // ".." – go up one directory
            sdNavigateUp();
        }
        else
        {
            // Map visible index to sdEntries[] index
            int entryIdx = sdBrowseIndex - entryOffset;
            if (entryIdx >= 0 && entryIdx < sdEntryCount)
            {
                if (sdEntries[entryIdx].isDirectory)
                {
                    sdNavigateInto(entryIdx);
                }
                else
                {
                    // File selected – set as current sample
                    sdSelectFile(entryIdx);
                    currentSynthSound = SYNTHSND_SAMPLE;
                    // Don't saveNVRAM — sample mode is session-only
                    // (path can't be persisted, would cause dead FS on reboot)
                    currentMenuLevel = MENU_SYNTHSND_SELECT;
                }
            }
        }
    }
    else if (currentMenuLevel == MENU_ARP_SELECT)
    {
        // If Parent selected, return to top. Otherwise apply arp mode.
        if (menuArpIndex == ARP_MENU_COUNT)
        {
            currentMenuLevel = MENU_TOP;
        }
        else
        {
            currentArpMode = menuArpIndex;
            saveNVRAM();
            currentMenuLevel = MENU_TOP;
        }
    }
    else if (currentMenuLevel == MENU_CONFIG_SELECT)
    {
        // If Parent selected, return to top. Otherwise enter sub-submenu.
        if (menuConfigIndex == CONFIG_MENU_COUNT)
        {
            currentMenuLevel = MENU_TOP;
        }
        else if (menuConfigIndex == 0) // Bass/Gtr
        {
            currentMenuLevel = MENU_BASSGUIT_SELECT;
            // Initialize to current instrument setting (0=Bass,1=Guitar)
            menuBassGuitIndex = currentInstrumentIsBass ? 0 : 1;
        }
        else if (menuConfigIndex == 1) // Muting
        {
            currentMenuLevel = MENU_MUTING_SELECT;
            // Initialize to current muting setting (0=Disabled,1=Enabled)
            menuMutingIndex = currentMutingEnabled ? 1 : 0;
        }
        else if (menuConfigIndex == 2) // Output
        {
            currentMenuLevel = MENU_OUTPUT_SELECT;
            // Initialize to current output setting (0=Mix,1=Split)
            menuOutputIndex = currentOutputMode;
        }
        else if (menuConfigIndex == 3) // StopMode
        {
            currentMenuLevel = MENU_STOPMODE_SELECT;
            // Initialize to current stop mode setting (0=Fade,1=Immediate)
            menuStopModeIndex = currentStopMode;
        }
    }
    else if (currentMenuLevel == MENU_OUTPUT_SELECT)
    {
        // If Parent selected, return to Config. Otherwise apply output setting.
        if (menuOutputIndex == OUTPUT_MENU_COUNT)
        {
            currentMenuLevel = MENU_CONFIG_SELECT;
        }
        else
        {
            currentOutputMode = menuOutputIndex;
            applyOutputMode(); // Apply the routing change
            saveNVRAM();
            currentMenuLevel = MENU_CONFIG_SELECT;
        }
    }
    else if (currentMenuLevel == MENU_STOPMODE_SELECT)
    {
        // If Parent selected, return to Config. Otherwise apply stop mode setting.
        if (menuStopModeIndex == STOPMODE_MENU_COUNT)
        {
            currentMenuLevel = MENU_CONFIG_SELECT;
        }
        else
        {
            currentStopMode = menuStopModeIndex;
            // Apply the fade duration based on mode
            if (currentStopMode == 1) // Immediate
            {
                chordFadeDurationMs = 0;
            }
            else // Fade
            {
                chordFadeDurationMs = 1500;
            }
            saveNVRAM();
            currentMenuLevel = MENU_CONFIG_SELECT;
        }
    }
}
