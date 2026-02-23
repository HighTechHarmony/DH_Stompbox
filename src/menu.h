#ifndef MENU_H
#define MENU_H

#include <Arduino.h>

// Menu state
enum MenuLevel
{
    MENU_TOP,
    MENU_KEY_SELECT,
    MENU_MODE_SELECT,
    MENU_OCTAVE_SELECT,
    MENU_BASSGUIT_SELECT,
    MENU_MUTING_SELECT,
    MENU_SYNTHSND_SELECT,
    MENU_SDCARD_BROWSE,
    MENU_ARP_SELECT,
    MENU_CONFIG_SELECT,
    MENU_OUTPUT_SELECT,
    MENU_STOPMODE_SELECT,
    MENU_SEVENTH_SELECT
};

extern MenuLevel currentMenuLevel;
extern int menuTopIndex;
extern int menuKeyIndex;
extern int menuModeIndex;
extern int menuOctaveIndex;
extern int menuBassGuitIndex;
extern int menuMutingIndex;
extern int menuSynthSndIndex;
extern int menuArpIndex;
extern int menuConfigIndex;
extern int menuOutputIndex;
extern int menuStopModeIndex;
extern int menuSeventhIndex;

// Viewport tracking for scrolling submenus
extern int keyViewportStart;
extern int modeViewportStart;
extern int octaveViewportStart;
extern int topViewportStart;
extern int bassGuitViewportStart;
extern int mutingViewportStart;
extern int synthSndViewportStart;
extern int arpViewportStart;
extern int configViewportStart;
extern int outputViewportStart;
extern int stopModeViewportStart;
extern int seventhViewportStart;

// Menu display names
extern const char *menuTopItems[];
extern const int MENU_TOP_COUNT;
extern const char *keyMenuNames[];
extern const int KEY_MENU_COUNT;
extern const int keyMenuToChromatic[];
extern const char *modeMenuNames[];
extern const int MODE_MENU_COUNT;
extern const char *bassGuitMenuNames[];
extern const int BASSGUIT_MENU_COUNT;
extern const int octaveOptions[];
extern const char *octaveMenuNames[];
extern const int OCTAVE_MENU_COUNT;
extern const char *mutingMenuNames[];
extern const int MUTING_MENU_COUNT;
extern const char *synthSndMenuNames[];
extern const int SYNTHSND_MENU_COUNT;
extern const char *arpMenuNames[];
extern const int ARP_MENU_COUNT;
extern const char *configMenuNames[];
extern const int CONFIG_MENU_COUNT;
extern const char *outputMenuNames[];
extern const int OUTPUT_MENU_COUNT;
extern const char *stopModeMenuNames[];
extern const int STOPMODE_MENU_COUNT;
extern const char *seventhMenuNames[];
extern const int SEVENTH_MENU_COUNT;

extern bool currentInstrumentIsBass;

void handleMenuEncoder(int delta);
void handleMenuButton();

#endif // MENU_H
