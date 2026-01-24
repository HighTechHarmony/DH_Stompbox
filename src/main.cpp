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

// Footswitch volume control state
bool fsVolumeControlActive = false;
float fsControlledVolume = 0.5f; // 0.0 to 1.0
bool useFsControlledVolume = false; // Whether to use FS volume (persists after exiting mode)
unsigned long lastFsVolumeActivityMs = 0;
int lastPotRaw = -1; // Track pot changes to detect override
bool fsVolumeJustActivated = false; // Skip first adjustment when entering mode
bool fsVolumeExitArmed = false;     // require release before allowing simultaneous-press exit
unsigned long fsVolumePreventReenterUntilMs = 0; // prevent immediate re-entry after exit
unsigned long fsIgnoreInputsUntilMs = 0; // settling time after FS volume mode changes

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

    // Detect pot rotation to override FS volume control
    if (lastPotRaw == -1)
    {
        lastPotRaw = potRaw; // Initialize on first loop
    }
    
    // If using FS-controlled volume and pot has moved significantly, switch back to pot control
    if (useFsControlledVolume && abs(potRaw - lastPotRaw) > 10) // ~1% threshold
    {
        useFsControlledVolume = false;
        if (fsVolumeControlActive)
        {
            fsVolumeControlActive = false;
            fsVolumeExitArmed = false;
            currentScreen = SCREEN_HOME;
        }
        lastPotRaw = potRaw;
        Serial.println("FS volume control overridden by pot");
    }
    // If not using FS volume, track pot changes normally
    if (!useFsControlledVolume)
    {
        lastPotRaw = potRaw;
    }

    // Determine effective volume based on control mode
    float effectiveVolume = useFsControlledVolume ? fsControlledVolume : potNorm;

    // Update chord volume in real-time
    updateChordVolume(effectiveVolume);

    // Handle non-blocking fade-out
    updateChordFade();

    // Apply any synth vibrato (e.g., organ) each loop
    updateVibrato();

    // Apply Rhodes decay if active
    updateRhodesDecay();

    // Return to home screen after fade completes
    if (!chordFading && currentScreen == SCREEN_FADE)
    {
        currentScreen = SCREEN_HOME;
    }

    // Restart chord if not active (ensure continuous playback)
    if (!chordActive && !chordSuppressed)
    {
        startChord(effectiveVolume, currentChordTonic, currentKey, currentModeIsMajor);
    }

    // Read inputs
    bool encButton = !digitalRead(ENC_BTN);
    bool fs1_raw = !digitalRead(FOOT1);
    bool fs2 = !digitalRead(FOOT2);

    // If currently in FS volume control, require a full release before
    // allowing a simultaneous FS1+FS2 press to exit the mode. This avoids
    // immediately exiting right after activation while the switches are
    // still held from the activation press.
    if (fsVolumeControlActive)
    {
        // If both released, arm exit on next simultaneous press
        if (!fs1_raw && !fs2)
        {
            fsVolumeExitArmed = true;
        }

        // If both pressed and we've seen a release since activation, exit
        if (fs1_raw && fs2 && fsVolumeExitArmed)
        {
            fsVolumeControlActive = false;
            fsVolumeExitArmed = false;
            // Prevent immediate re-entry on the same held press
            fsVolumePreventReenterUntilMs = now + 200; // 200ms cooldown
            // Ignore all FS inputs for settling time to prevent accidental triggers
            fsIgnoreInputsUntilMs = now + 250; // 250ms settling time
            // Keep useFsControlledVolume true so volume persists until pot is moved
            currentScreen = SCREEN_HOME;
            Serial.println("FS volume control exited by simultaneous FS press (armed)");
        }
    }

    // Detect both footswitches pressed simultaneously to enter FS volume control mode
    if (fs1_raw && fs2 && !prevFs1 && !prevFs2 && now >= fsVolumePreventReenterUntilMs)
    {
        fsVolumeControlActive = true;
        useFsControlledVolume = true;
        fsVolumeJustActivated = true; // Skip adjustment on activation
        fsVolumeExitArmed = false; // require a release before allowing exit
        fsControlledVolume = effectiveVolume; // Start with current volume
        lastFsVolumeActivityMs = now;
        // Ignore all FS inputs for settling time to prevent accidental triggers
        fsIgnoreInputsUntilMs = now + 250; // 250ms settling time
        currentScreen = SCREEN_VOLUME_CONTROL;
        Serial.println("FS volume control mode activated");
    }

    // Handle FS volume control mode
    if (fsVolumeControlActive)
    {
        // Check for timeout
        if ((now - lastFsVolumeActivityMs) > FS_VOLUME_TIMEOUT_MS)
        {
            fsVolumeControlActive = false;
            fsVolumeExitArmed = false;
            // Keep useFsControlledVolume true so volume persists until pot is moved
            currentScreen = SCREEN_HOME;
            Serial.println("FS volume control timeout");
        }
        else
        {
            // Skip adjustments on the initial activation cycle
            if (fsVolumeJustActivated)
            {
                fsVolumeJustActivated = false;
            }
            else
            {
                // Handle FS1 press (decrement volume)
                if (fs1_raw && !prevFs1)
                {
                    fsControlledVolume -= 0.10f; // Decrement by 10%
                    if (fsControlledVolume < 0.0f)
                        fsControlledVolume = 0.0f;
                    lastFsVolumeActivityMs = now;
                    updateChordVolume(fsControlledVolume);
                    Serial.print("FS volume decreased to: ");
                    Serial.println(fsControlledVolume * 100.0f);
                }
                
                // Handle FS2 press (increment volume)
                if (fs2 && !prevFs2)
                {
                    fsControlledVolume += 0.10f; // Increment by 10%
                    if (fsControlledVolume > 1.0f)
                        fsControlledVolume = 1.0f;
                    lastFsVolumeActivityMs = now;
                    updateChordVolume(fsControlledVolume);
                    Serial.print("FS volume increased to: ");
                    Serial.println(fsControlledVolume * 100.0f);
                }
            }
        }
    }

    // On raw press-edge, ensure FS1 remains true for at least the minimum window
    if (fs1_raw && !prevFs1 && !fsVolumeControlActive)
    {
        fs1ForcedUntilMs = now + FS1_MIN_ACTIVATION_MS;
    }

    // Effective FS1 seen by the rest of the loop (remains true for short taps)
    bool fs1 = fs1_raw || (now < fs1ForcedUntilMs);

    // Optionally suppress chord audio output while the FS1 forced window is active
    // Muting menu overrides the behavior: when Muting is Enabled -> suppress during transition, when Disabled -> do not suppress
    bool suppressChordDuringTransition = (now < fs1ForcedUntilMs) && currentMutingEnabled;
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

    // FS2 press edge: stop chord immediately on initial press (unless in FS volume control mode or within settling time)
    if (fs2 && !prevFs2 && !fsVolumeControlActive && now >= fsIgnoreInputsUntilMs)
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

    // FS1 press edge: if chord was suppressed (stopped by FS2), re-enable it (unless in FS volume control mode or within settling time)
    if (fs1_raw && !prevFs1 && !fsVolumeControlActive && now >= fsIgnoreInputsUntilMs)
    {
        // Reset pitch detection to clear stale frequency data
        resetPitchDetection();

        if (chordSuppressed)
        {
            // Start with tonic=0; chord will update once fresh pitch is detected
            startChord(effectiveVolume, 0.0f, currentKey, currentModeIsMajor);
        }
    }

    // Read pitch detection
    float frequency = 0.0;
    float probability = 0.0;
    const char *noteName = "---";
    updatePitchDetection(frequency, probability, noteName, currentInstrumentIsBass);

    // Update chord in real-time while sampling (only when FS1 is held and NOT in FS volume control mode)
    if (fs1 && lastDetectedFrequency > 0.0f && !fsVolumeControlActive)
    {
        updateChordTonic(lastDetectedFrequency, currentKey, currentModeIsMajor);
    }

    // FS1 release edge: start Rhodes decay if Rhodes is active
    if (!fs1_raw && prevFs1)
    {
        startRhodesDecay();
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
    else if (currentScreen == SCREEN_VOLUME_CONTROL)
    {
        renderVolumeControlScreen(fsControlledVolume);
    }

    // Track state for edge detection next iteration
    prevFs1 = fs1_raw;
    prevFs2 = fs2;
    prevEncButton = encButton;

    delay(50);
}
