#ifndef MENU_H
#define MENU_H

#include <Arduino.h>

// Menu state
enum MenuLevel
{
    MENU_TOP,
    MENU_KEY_SELECT,
    MENU_MODE_SELECT,
    MENU_OCTAVE_SELECT
};

extern MenuLevel currentMenuLevel;
extern int menuTopIndex;
extern int menuKeyIndex;
extern int menuModeIndex;
extern int menuOctaveIndex;

// Viewport tracking for scrolling submenus
extern int keyViewportStart;
extern int modeViewportStart;
extern int octaveViewportStart;
extern int topViewportStart;

// Menu display names
extern const char *menuTopItems[];
extern const int MENU_TOP_COUNT;
extern const char *keyMenuNames[];
extern const int KEY_MENU_COUNT;
extern const int keyMenuToChromatic[];
extern const char *modeMenuNames[];
extern const int MODE_MENU_COUNT;
extern const int octaveOptions[];
extern const char *octaveMenuNames[];
extern const int OCTAVE_MENU_COUNT;

void handleMenuEncoder(int delta);
void handleMenuButton();

#endif // MENU_H
