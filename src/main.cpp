#include <Arduino.h>
#include "config.h"
#include "NVRAM.h"
#include "input.h"
#include "audio.h"
#include "pitch.h"
#include "menu.h"
#include "display.h"
#include "test.h"

// Timing state
unsigned long fs1ForcedUntilMs = 0;

void setup()
{
    Serial.begin(9600);
    Serial.println("=== SETUP START ===");

    // Initialize subsystems
    setupAudio();
    setupPitchDetection();
    loadNVRAM();
    setupInput();
    setupDisplay();

    Serial.println("=== SETUP COMPLETE ===");

    // Check for hardware test mode (FS1 held at startup)
    if (!digitalRead(FOOT1)) // FS1 is active low
    {
        Serial.println("*** ENTERING HARDWARE TEST MODE ***");
        hardwareTestMode();
        // Never returns
    }
}

void loop()
{
    static bool prevFs1 = false;
    static bool prevFs2 = false;
    static bool prevEncButton = false;
    static int lastEncoderPosition = 0;
    unsigned long now = millis();

    // Read pot for volume control
    int potRaw = analogRead(POT_PIN);
    float potNorm = potRaw / 1023.0;

    // Update chord volume in real-time
    updateChordVolume(potNorm);

    // Handle non-blocking fade-out
    updateChordFade();

    // Return to home screen after fade completes
    if (!chordFading && currentScreen == SCREEN_FADE)
    {
        currentScreen = SCREEN_HOME;
    }

    // Restart chord if not active (ensure continuous playback)
    if (!chordActive && !chordSuppressed)
    {
        startChord(potNorm, currentChordTonic, currentKey, currentModeIsMajor);
    }

    // Read inputs
    bool encButton = !digitalRead(ENC_BTN);
    bool fs1_raw = !digitalRead(FOOT1);
    bool fs2 = !digitalRead(FOOT2);

    // On raw press-edge, ensure FS1 remains true for at least the minimum window
    if (fs1_raw && !prevFs1)
    {
        fs1ForcedUntilMs = now + FS1_MIN_ACTIVATION_MS;
    }

    // Effective FS1 seen by the rest of the loop (remains true for short taps)
    bool fs1 = fs1_raw || (now < fs1ForcedUntilMs);

    // Optionally suppress chord audio output while the FS1 forced window is active
    bool suppressChordDuringTransition = (SUPPRESS_CHORD_OUTPUT_DURING_TRANSITIONS && (now < fs1ForcedUntilMs));
    if (suppressChordDuringTransition)
    {
        // Only mute if not currently fading (let fades complete)
        if (!chordFading)
        {
            myEffect.amplitude(0);
            myEffect2.amplitude(0);
            myEffect3.amplitude(0);
        }
    }
    else
    {
        // If chord is active and not fading, ensure amplitude reflects stored beepAmp
        if (chordActive && !chordFading)
        {
            float perVoice = beepAmp / 3.0f;
            myEffect.amplitude(perVoice);
            myEffect2.amplitude(perVoice);
            myEffect3.amplitude(perVoice);
        }
    }

    // FS2 press edge: stop chord immediately on initial press
    if (fs2 && !prevFs2)
    {
        stopChord();
        // show fade progress screen only if a fade was actually started
        if (chordFading)
        {
            currentScreen = SCREEN_FADE;
        }
        else
        {
            // No fade to show; return to home immediately
            currentScreen = SCREEN_HOME;
        }
    }

    // FS1 press edge: if chord was suppressed (stopped by FS2), re-enable it
    if (fs1_raw && !prevFs1)
    {
        if (chordSuppressed)
        {
            float tonicToUse = (lastDetectedFrequency > 1.0f) ? lastDetectedFrequency : currentChordTonic;
            startChord(potNorm, tonicToUse, currentKey, currentModeIsMajor);
        }
    }

    // Read pitch detection
    float frequency = 0.0;
    float probability = 0.0;
    const char *noteName = "---";
    updatePitchDetection(frequency, probability, noteName, currentInstrumentIsBass);

    // Update chord in real-time while sampling (only when FS1 is held)
    if (fs1 && lastDetectedFrequency > 0.0f)
    {
        updateChordTonic(lastDetectedFrequency, currentKey, currentModeIsMajor);
    }

    // Detect encoder activity for UI timeout and menu navigation
    if (encoderPosition != lastEncoderPosition)
    {
        lastEncoderActivityMs = now;
        if (currentScreen == SCREEN_HOME)
        {
            currentScreen = SCREEN_MENU;
            currentMenuLevel = MENU_TOP;
        }

        // Handle encoder changes based on current menu level
        if (currentScreen == SCREEN_MENU)
        {
            // REVERSED direction: turning encoder one way now moves selection opposite
            int delta = lastEncoderPosition - encoderPosition;
            handleMenuEncoder(delta);
        }

        lastEncoderPosition = encoderPosition;
    }

    // Handle encoder button press for menu selection
    if (encButton && !prevEncButton) // button press edge
    {
        // If at home, button now brings up the menu
        if (currentScreen == SCREEN_HOME)
        {
            currentScreen = SCREEN_MENU;
            currentMenuLevel = MENU_TOP;
            lastEncoderActivityMs = now;
        }
        else if (currentScreen == SCREEN_MENU)
        {
            lastEncoderActivityMs = now; // keep menu active
            handleMenuButton();

            // Check if we should exit to home after button action
            if (currentMenuLevel == MENU_TOP && menuTopIndex == MENU_TOP_COUNT)
            {
                currentScreen = SCREEN_HOME;
                currentMenuLevel = MENU_TOP;
            }
        }
    }

    // Timeout back to home screen after inactivity
    if (currentScreen == SCREEN_MENU && (now - lastEncoderActivityMs) > SCREEN_TIMEOUT_MS)
    {
        currentScreen = SCREEN_HOME;
        currentMenuLevel = MENU_TOP;
    }

    // Serial output for debugging
    // Serial.print("AudioShield: ");
    // Serial.print(audioShieldEnabled ? "ENABLED " : "DISABLED ");
    // Serial.print("Enc:");
    // Serial.print(encoderPosition);
    // Serial.print(" Btn:");
    // Serial.print(encButton ? "PRESSED" : "released");
    // Serial.print(" FS1:");
    // Serial.print(fs1 ? "ON" : "off");
    // Serial.print(" FS2:");
    // Serial.print(fs2 ? "ON" : "off");
    // Serial.print(" Pot raw: ");
    // Serial.print(potRaw);
    // Serial.print(" Note: ");
    // Serial.print(noteName);
    // Serial.print(" (");
    // Serial.print(frequency, 1);
    // Serial.print(" Hz, prob: ");
    // Serial.print(probability, 2);
    // Serial.println(")");

    // Render appropriate screen
    if (currentScreen == SCREEN_HOME)
    {
        renderHomeScreen(noteName, frequency);
    }
    else if (currentScreen == SCREEN_MENU)
    {
        renderMenuScreen();
    }
    else if (currentScreen == SCREEN_FADE)
    {
        renderFadeScreen();
    }

    // Track state for edge detection next iteration
    prevFs1 = fs1_raw;
    prevFs2 = fs2;
    prevEncButton = encButton;

    delay(50);
}
