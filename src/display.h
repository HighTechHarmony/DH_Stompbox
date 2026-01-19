#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

// UI screen state
enum ScreenMode
{
    SCREEN_HOME,
    SCREEN_MENU,
    SCREEN_FADE
};

extern Adafruit_SSD1306 display;
extern ScreenMode currentScreen;
extern unsigned long lastEncoderActivityMs;

void setupDisplay();
void renderHomeScreen(const char *noteName, float frequency);
void renderMenuScreen();
void renderFadeScreen();

#endif // DISPLAY_H
