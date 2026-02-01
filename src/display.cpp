#include "display.h"
#include "config.h"
#include "menu.h"
#include "audio.h"
#include "NVRAM.h"
#include <Wire.h>
#include <math.h>

// OLED Setup
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

ScreenMode currentScreen = SCREEN_HOME;
unsigned long lastEncoderActivityMs = 0;

void setupDisplay()
{
    bool oledAvailable = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    if (!oledAvailable)
    {
        Serial.println("OLED not found");
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("OLED OK");

    // show audio shield init status underneath the OLED report
    display.setCursor(0, 10); // next line (8px font + small gap)
    display.print("AudioShield: ");
    display.println(audioShieldEnabled ? "ENABLED" : "DISABLED");

    display.display();
}

void renderHomeScreen(const char *noteName, float frequency)
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);

    int y = 0;

    // Display key and mode
    display.setCursor(0, y);
    
    const char *keyNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    // If in Fixed interval modes, show a descriptive fixed-interval label
    if (currentMode == 2)
    {
        display.print("Fix Int M");
    }

    else if (currentMode == 3)
    {
        display.print("Fix Int m");
    }
    else
    {
        // Print key and a short mode code that fits within 10 characters total
        // (including the space after the key). For example: "E Mj", "Bb Mn".
        display.print(keyNames[currentKey]);
        // Short mode codes for diatonic modes
        const char *modeShort[] = {"Maj", "Min"};
        String keyStr = String(keyNames[currentKey]);
        int keyLen = keyStr.length();
        int maxTotal = 10;                         // maximum chars including space after key
        int maxModeLen = maxTotal - (keyLen + 1); // leave room for space
        
        if (maxModeLen >= 1)
        {
            String m = String(modeShort[currentMode]);
            if ((int)m.length() > maxModeLen)
                m = m.substring(0, maxModeLen);
            display.print(" ");
            display.print(m);
        }
    }
    // Add a bit more vertical spacing to avoid text overlap with the next line
    y += 24;

    // Display detected note and frequency
    display.setTextSize(2);
    // Clear the area where the "Note" label/value will be drawn to avoid
    // any remnants from previous frames or longer mode names.
    display.fillRect(0, y, SCREEN_WIDTH, 20, SSD1306_BLACK);
    display.setCursor(0, y);
    display.print("Note: ");
    display.print(noteName);
    y += 20;

    // Display a tuner bar graph that shows deviation from nearest semitone
    if (frequency > 0.0f)
    {
        // Calculate deviation from nearest semitone in cents
        float n = 12.0 * log2f(frequency / 440.0) + 69.0;
        int nearestNote = (int)(n + 0.5);
        float cents = (n - nearestNote) * 100.0f; // deviation in cents
        // Draw bar graph
        int barWidth = map(cents, -50, 50, -30, 30); // map -50 to +50 cents to -30 to +30 pixels
        display.drawRect(0, y, 64, 10, SSD1306_WHITE);
        if (barWidth < 0)
        {
            display.fillRect(32 + barWidth, y + 1, -barWidth, 8, SSD1306_WHITE);
        }
        else
        {
            display.fillRect(32, y + 1, barWidth, 8, SSD1306_WHITE);
        }
        y += 15;
        display.setTextSize(1);
        display.setCursor(0, y);
        display.print(frequency, 1);
        display.print(" Hz");
    }

    display.display();
}

void renderMenuScreen()
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);

    if (currentMenuLevel == MENU_TOP)
    {
        // Top-level menu
        display.setCursor(0, 0);
        display.println("Menu");

        // Show top items with Parent at end using stable viewport scrolling
        int totalCount = MENU_TOP_COUNT + 1; // includes Parent
        int visible = 3;                     // number of lines to show (fits size2)

        // Stable viewport scrolling for top menu
        if (menuTopIndex < topViewportStart)
        {
            topViewportStart = menuTopIndex;
        }
        else if (menuTopIndex >= topViewportStart + visible)
        {
            topViewportStart = menuTopIndex - visible + 1;
        }
        if (topViewportStart < 0)
            topViewportStart = 0;
        if (topViewportStart > totalCount - visible)
            topViewportStart = totalCount - visible;
        if (topViewportStart < 0)
            topViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = topViewportStart + i;
            if (idx >= totalCount)
                break;
            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuTopIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < MENU_TOP_COUNT)
                display.println(menuTopItems[idx]);
            else
                display.println("^");
        }
    }
    else if (currentMenuLevel == MENU_KEY_SELECT)
    {
        // Key selection submenu - show current menu name at top
        display.setCursor(0, 0);
        display.println(menuTopItems[0]);

        // Build a scrolling view over keys + Parent
        int totalCount = KEY_MENU_COUNT + 1; // includes Parent
        int visible = 3;                     // number of lines to show (fits size2)

        // Stable viewport scrolling
        if (menuKeyIndex < keyViewportStart)
        {
            keyViewportStart = menuKeyIndex;
        }
        else if (menuKeyIndex >= keyViewportStart + visible)
        {
            keyViewportStart = menuKeyIndex - visible + 1;
        }

        if (keyViewportStart < 0)
            keyViewportStart = 0;
        if (keyViewportStart > totalCount - visible)
            keyViewportStart = totalCount - visible;
        if (keyViewportStart < 0)
            keyViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = keyViewportStart + i;
            if (idx >= totalCount)
                break;

            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuKeyIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < KEY_MENU_COUNT)
                display.println(keyMenuNames[idx]);
            else
                display.println("^");
        }
    }
    else if (currentMenuLevel == MENU_MODE_SELECT)
    {
        // Mode selection submenu - show current menu name at top
        display.setCursor(0, 0);
        display.println(menuTopItems[1]);

        int totalCount = MODE_MENU_COUNT + 1; // includes Parent
        int visible = 3;

        // Stable viewport scrolling
        if (menuModeIndex < modeViewportStart)
        {
            modeViewportStart = menuModeIndex;
        }
        else if (menuModeIndex >= modeViewportStart + visible)
        {
            modeViewportStart = menuModeIndex - visible + 1;
        }

        if (modeViewportStart < 0)
            modeViewportStart = 0;
        if (modeViewportStart > totalCount - visible)
            modeViewportStart = totalCount - visible;
        if (modeViewportStart < 0)
            modeViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = modeViewportStart + i;
            if (idx >= totalCount)
                break;

            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuModeIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < MODE_MENU_COUNT)
                display.println(modeMenuNames[idx]);
            else
                display.println("^");
        }
    }
    else if (currentMenuLevel == MENU_OCTAVE_SELECT)
    {
        // Octave selection submenu - show current menu name at top
        display.setCursor(0, 0);
        display.println(menuTopItems[2]);

        int totalCount = OCTAVE_MENU_COUNT + 1; // includes Parent
        int visible = 3;

        // Stable viewport scrolling
        if (menuOctaveIndex < octaveViewportStart)
        {
            octaveViewportStart = menuOctaveIndex;
        }
        else if (menuOctaveIndex >= octaveViewportStart + visible)
        {
            octaveViewportStart = menuOctaveIndex - visible + 1;
        }

        if (octaveViewportStart < 0)
            octaveViewportStart = 0;
        if (octaveViewportStart > totalCount - visible)
            octaveViewportStart = totalCount - visible;
        if (octaveViewportStart < 0)
            octaveViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = octaveViewportStart + i;
            if (idx >= totalCount)
                break;

            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuOctaveIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < OCTAVE_MENU_COUNT)
                display.println(octaveMenuNames[idx]);
            else
                display.println("^");
        }
    }
    else if (currentMenuLevel == MENU_BASSGUIT_SELECT)
    {
        // Bass/Guitar selection submenu - show Config as parent title
        display.setCursor(0, 0);
        display.println("Bass/Gtr");

        int totalCount = BASSGUIT_MENU_COUNT + 1; // includes Parent
        int visible = 3;

        // Stable viewport scrolling
        if (menuBassGuitIndex < bassGuitViewportStart)
        {
            bassGuitViewportStart = menuBassGuitIndex;
        }
        else if (menuBassGuitIndex >= bassGuitViewportStart + visible)
        {
            bassGuitViewportStart = menuBassGuitIndex - visible + 1;
        }

        if (bassGuitViewportStart < 0)
            bassGuitViewportStart = 0;
        if (bassGuitViewportStart > totalCount - visible)
            bassGuitViewportStart = totalCount - visible;
        if (bassGuitViewportStart < 0)
            bassGuitViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = bassGuitViewportStart + i;
            if (idx >= totalCount)
                break;

            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuBassGuitIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < BASSGUIT_MENU_COUNT)
                display.println(bassGuitMenuNames[idx]);
            else
                display.println("^");
        }
    }
    else if (currentMenuLevel == MENU_MUTING_SELECT)
    {
        // Muting selection submenu - show Config as parent title
        display.setCursor(0, 0);
        display.println("Muting");

        int totalCount = MUTING_MENU_COUNT + 1; // includes Parent
        int visible = 3;

        // Stable viewport scrolling
        if (menuMutingIndex < mutingViewportStart)
        {
            mutingViewportStart = menuMutingIndex;
        }
        else if (menuMutingIndex >= mutingViewportStart + visible)
        {
            mutingViewportStart = menuMutingIndex - visible + 1;
        }

        if (mutingViewportStart < 0)
            mutingViewportStart = 0;
        if (mutingViewportStart > totalCount - visible)
            mutingViewportStart = totalCount - visible;
        if (mutingViewportStart < 0)
            mutingViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = mutingViewportStart + i;
            if (idx >= totalCount)
                break;

            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuMutingIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < MUTING_MENU_COUNT)
                display.println(mutingMenuNames[idx]);
            else
                display.println("^");
        }
    }
    else if (currentMenuLevel == MENU_SYNTHSND_SELECT)
    {
        // Synth Sound selection submenu - show current menu name at top
        display.setCursor(0, 0);
        display.println(menuTopItems[3]);

        int totalCount = SYNTHSND_MENU_COUNT + 1; // includes Parent
        int visible = 3;

        // Stable viewport scrolling
        if (menuSynthSndIndex < synthSndViewportStart)
        {
            synthSndViewportStart = menuSynthSndIndex;
        }
        else if (menuSynthSndIndex >= synthSndViewportStart + visible)
        {
            synthSndViewportStart = menuSynthSndIndex - visible + 1;
        }

        if (synthSndViewportStart < 0)
            synthSndViewportStart = 0;
        if (synthSndViewportStart > totalCount - visible)
            synthSndViewportStart = totalCount - visible;
        if (synthSndViewportStart < 0)
            synthSndViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = synthSndViewportStart + i;
            if (idx >= totalCount)
                break;

            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuSynthSndIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < SYNTHSND_MENU_COUNT)
                display.println(synthSndMenuNames[idx]);
            else
                display.println("^");
        }
    }
    else if (currentMenuLevel == MENU_ARP_SELECT)
    {
        // Arp selection submenu - show current menu name at top
        display.setCursor(0, 0);
        display.println(menuTopItems[4]);

        int totalCount = ARP_MENU_COUNT + 1; // includes Parent
        int visible = 3;

        // Stable viewport scrolling
        if (menuArpIndex < arpViewportStart)
        {
            arpViewportStart = menuArpIndex;
        }
        else if (menuArpIndex >= arpViewportStart + visible)
        {
            arpViewportStart = menuArpIndex - visible + 1;
        }

        if (arpViewportStart < 0)
            arpViewportStart = 0;
        if (arpViewportStart > totalCount - visible)
            arpViewportStart = totalCount - visible;
        if (arpViewportStart < 0)
            arpViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = arpViewportStart + i;
            if (idx >= totalCount)
                break;

            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuArpIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < ARP_MENU_COUNT)
                display.println(arpMenuNames[idx]);
            else
                display.println("^");
        }
    }
    else if (currentMenuLevel == MENU_CONFIG_SELECT)
    {
        // Config selection submenu - show current menu name at top
        display.setCursor(0, 0);
        display.println(menuTopItems[5]);

        int totalCount = CONFIG_MENU_COUNT + 1; // includes Parent
        int visible = 3;

        // Stable viewport scrolling
        if (menuConfigIndex < configViewportStart)
        {
            configViewportStart = menuConfigIndex;
        }
        else if (menuConfigIndex >= configViewportStart + visible)
        {
            configViewportStart = menuConfigIndex - visible + 1;
        }

        if (configViewportStart < 0)
            configViewportStart = 0;
        if (configViewportStart > totalCount - visible)
            configViewportStart = totalCount - visible;
        if (configViewportStart < 0)
            configViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = configViewportStart + i;
            if (idx >= totalCount)
                break;

            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuConfigIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < CONFIG_MENU_COUNT)
                display.println(configMenuNames[idx]);
            else
                display.println("^");
        }
    }
    else if (currentMenuLevel == MENU_OUTPUT_SELECT)
    {
        // Output selection submenu - show "Output" as title
        display.setCursor(0, 0);
        display.println("Output");

        int totalCount = OUTPUT_MENU_COUNT + 1; // includes Parent
        int visible = 3;

        // Stable viewport scrolling
        if (menuOutputIndex < outputViewportStart)
        {
            outputViewportStart = menuOutputIndex;
        }
        else if (menuOutputIndex >= outputViewportStart + visible)
        {
            outputViewportStart = menuOutputIndex - visible + 1;
        }

        if (outputViewportStart < 0)
            outputViewportStart = 0;
        if (outputViewportStart > totalCount - visible)
            outputViewportStart = totalCount - visible;
        if (outputViewportStart < 0)
            outputViewportStart = 0;

        for (int i = 0; i < visible; i++)
        {
            int idx = outputViewportStart + i;
            if (idx >= totalCount)
                break;

            int y = 18 + i * 18;
            display.setCursor(0, y);
            if (idx == menuOutputIndex)
            {
                display.print("> ");
            }
            else
            {
                display.print("  ");
            }

            if (idx < OUTPUT_MENU_COUNT)
                display.println(outputMenuNames[idx]);
            else
                display.println("^");
        }
    }

    display.display();
}

void renderFadeScreen()
{
    display.clearDisplay();
    display.setTextSize(1);

    // if no fade is actually in progress, go back to home to avoid a blank screen
    if (!chordFading)
    {
        currentScreen = SCREEN_HOME;
    }
    else
    {
        // compute progress (0..1)
        float progress = 0.0f;
        if (chordFadeDurationMs > 0)
        {
            unsigned long nowMs = millis();
            long elapsed = (long)(nowMs - chordFadeStartMs);
            if (elapsed < 0)
                elapsed = 0;
            if (elapsed > (long)chordFadeDurationMs)
                elapsed = chordFadeDurationMs;
            progress = (float)elapsed / (float)chordFadeDurationMs;
        }
        int barW = SCREEN_WIDTH - (int)(progress * (float)SCREEN_WIDTH);
        // draw filled bar (starts full and empties as progress increases)
        display.fillRect(0, 0, barW, SCREEN_HEIGHT, SSD1306_WHITE);
        // draw the word "FADEOUT" in inverse color centered
        char buf[8];
        sprintf(buf, "%s", "FADEOUT");
        display.setTextSize(2);
        display.setTextColor(SSD1306_BLACK);
        int tx = (SCREEN_WIDTH - (6 * strlen(buf))) / 2; // approx width per char
        int ty = (SCREEN_HEIGHT / 2) - 8;
        if (tx < 0)
            tx = 0;
        display.setCursor(tx, ty);
        display.println(buf);
        display.setTextColor(SSD1306_WHITE);
    }

    display.display();
}

void renderVolumeControlScreen(float volumeLevel)
{
    display.clearDisplay();
    display.setTextSize(1);

    // Display title at the top
    display.setCursor(0, 0);
    display.setTextColor(SSD1306_WHITE);
    display.println("FS VOLUME CONTROL");

    // Display volume percentage using a logarithmic (dB) mapping so
    // displayed percent better matches perceived loudness.
    // Map linear amplitude (0..1) -> dB (minDb..0) -> percent (0..100)
    const float minDb = -60.0f; // floor for lowest visible volume
    int volumePercent = 0;
    if (volumeLevel <= 0.000001f)
    {
        volumePercent = 0;
    }
    else
    {
        float db = 20.0f * log10f(volumeLevel);
        if (db < minDb)
            db = minDb;
        float pct = (db - minDb) / (-minDb) * 100.0f; // 0..100
        volumePercent = (int)(pct + 0.5f);
    }

    display.setCursor(0, 12);
    display.setTextSize(2);
    display.print(volumePercent);
    display.print("%");

    // Draw volume bar graph using the same percent so the bar matches the
    // displayed percentage (perceived/logarithmic scale).
    int barY = 35;
    int barHeight = 20;
    int barWidth = (int)((volumePercent / 100.0f) * (float)SCREEN_WIDTH);

    // Draw outline
    display.drawRect(0, barY, SCREEN_WIDTH, barHeight, SSD1306_WHITE);

    // Fill bar based on percentage
    if (barWidth > 2)
    {
        display.fillRect(1, barY + 1, barWidth - 1, barHeight - 2, SSD1306_WHITE);
    }

    display.display();
}

void renderTapTempoScreen(float bpm)
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    
    // Display title at the top
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("TAP TEMPO MODE");
    
    // Display current BPM in large text
    display.setTextSize(3);
    int bpmInt = (int)(bpm + 0.5f); // Round to nearest integer
    char bpmStr[8];
    sprintf(bpmStr, "%d", bpmInt);
    
    // Center the BPM display
    int textWidth = strlen(bpmStr) * 18; // Approximate width for size 3 text
    int xPos = (SCREEN_WIDTH - textWidth) / 2;
    if (xPos < 0) xPos = 0;
    
    display.setCursor(xPos, 20);
    display.print(bpmStr);
    
    // Display "BPM" label below
    display.setTextSize(1);
    display.setCursor(52, 50);
    display.print("BPM");
    
    display.display();
}
