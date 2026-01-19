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
    MENU_MUTING_SELECT
};

extern MenuLevel currentMenuLevel;
extern int menuTopIndex;
extern int menuKeyIndex;
extern int menuModeIndex;
extern int menuOctaveIndex;
extern int menuBassGuitIndex;
extern int menuMutingIndex;

// Viewport tracking for scrolling submenus
extern int keyViewportStart;
extern int modeViewportStart;
extern int octaveViewportStart;
extern int topViewportStart;
extern int bassGuitViewportStart;
extern int mutingViewportStart;

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

extern bool currentInstrumentIsBass;

void handleMenuEncoder(int delta);
void handleMenuButton();

#endif // MENU_H
