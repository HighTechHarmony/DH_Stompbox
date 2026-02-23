#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "NVRAM.h"
#include "input.h"
#include "audio.h"
#include "pitch.h"
#include "menu.h"
#include "display.h"
#include "test.h"
#include "sdcard.h"

// Timing state
unsigned long fs1ForcedUntilMs = 0;

// Footswitch volume control state
bool fsVolumeControlActive = false;
float fsControlledVolume = 0.5f;    // 0.0 to 1.0
bool useFsControlledVolume = false; // Whether to use FS volume (persists after exiting mode)
unsigned long lastFsVolumeActivityMs = 0;
int lastPotRaw = -1;                             // Track pot changes to detect override
bool fsVolumeJustActivated = false;              // Skip first adjustment when entering mode
bool fsVolumeExitArmed = false;                  // require release before allowing simultaneous-press exit
unsigned long fsVolumePreventReenterUntilMs = 0; // prevent immediate re-entry after exit
unsigned long fsIgnoreInputsUntilMs = 0;         // settling time after FS volume mode changes

// Tap tempo state
bool tapTempoActive = false;
unsigned long lastFs2TapMs = 0;
unsigned long lastTapTempoActivityMs = 0;
float tapTempoAbortedVolume = 0.0f; // Store volume if fadeout was aborted

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
    // Must happen BEFORE initSDCard() because SD SPI uses pin 12 (MISO) which conflicts with FOOT1
    if (!digitalRead(FOOT1)) // FS1 is active low
    {
        Serial.println("*** ENTERING HARDWARE TEST MODE ***");
        hardwareTestMode();
        // Never returns
    }

    // Initialize SD card after test mode check to avoid SPI pin conflict with FOOT1
    initSDCard();
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

    // Track pot and show volume display when user adjusts pot (do NOT enter FS volume mode)
    if (lastPotRaw == -1)
    {
        lastPotRaw = potRaw; // Initialize on first loop
    }

    if (abs(potRaw - lastPotRaw) > 10) // ~1% threshold
    {
        // If we were using FS-controlled volume, revert back to pot control
        if (useFsControlledVolume)
        {
            useFsControlledVolume = false;
            if (fsVolumeControlActive)
            {
                fsVolumeControlActive = false;
                fsVolumeExitArmed = false;
            }
            Serial.println("FS volume control overridden by pot");
        }

        // Show the volume-control display and reset the idle timer.
        // NOTE: we do NOT set fsVolumeControlActive or useFsControlledVolume here.
        currentScreen = SCREEN_VOLUME_CONTROL;
        lastFsVolumeActivityMs = now;
        lastPotRaw = potRaw;
    }
    else if (!useFsControlledVolume)
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

    // Update arpeggiator if active
    updateArpeggiator();

    // Return to home screen after fade completes
    if (!chordFading && currentScreen == SCREEN_FADE)
    {
        currentScreen = SCREEN_HOME;
    }

    // Restart chord if not active (ensure continuous playback)
    // Skip auto-restart for sample mode – samples have finite length
    if (!chordActive && !chordSuppressed && currentSynthSound != SYNTHSND_SAMPLE)
    {
        startChord(effectiveVolume, currentChordTonic, currentKey, currentMode);
    }

    // Auto-stop when sample finishes playing
    if (currentSynthSound == SYNTHSND_SAMPLE && chordActive && !chordFading && !isSamplePlaying())
    {
        stopSampleChord();
        currentScreen = SCREEN_HOME;
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
        fsVolumeJustActivated = true;         // Skip adjustment on activation
        fsVolumeExitArmed = false;            // require a release before allowing exit
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
                // Handle FS1 press (decrement volume) - operate in dB space so
                // steps correspond to the logarithmic display. Each FS step
                // moves ~15 percentage points on the displayed scale -> that's
                // (15% of 60 dB) = 9 dB per step when minDb=-60.
                const float minDb = -60.0f;
                const float dbStep = 9.0f; // corresponds to ~15% displayed

                if (fs1_raw && !prevFs1)
                {
                    // convert current linear volume to dB
                    float curDb;
                    if (fsControlledVolume <= 0.000001f)
                        curDb = minDb;
                    else
                        curDb = 20.0f * log10f(fsControlledVolume);

                    curDb -= dbStep;
                    if (curDb < minDb)
                        curDb = minDb;

                    fsControlledVolume = powf(10.0f, curDb / 20.0f);
                    lastFsVolumeActivityMs = now;
                    updateChordVolume(fsControlledVolume);

                    // Show logged percent in serial to match display
                    float pct = (curDb - minDb) / (-minDb) * 100.0f;
                    Serial.print("FS volume decreased to: ");
                    Serial.println((int)(pct + 0.5f));
                }

                // Handle FS2 press (increment volume)
                if (fs2 && !prevFs2)
                {
                    float curDb;
                    if (fsControlledVolume <= 0.000001f)
                        curDb = minDb;
                    else
                        curDb = 20.0f * log10f(fsControlledVolume);

                    curDb += dbStep;
                    if (curDb > 0.0f)
                        curDb = 0.0f;

                    fsControlledVolume = powf(10.0f, curDb / 20.0f);
                    lastFsVolumeActivityMs = now;
                    updateChordVolume(fsControlledVolume);

                    float pct = (curDb - minDb) / (-minDb) * 100.0f;
                    Serial.print("FS volume increased to: ");
                    Serial.println((int)(pct + 0.5f));
                }
            }
        }
    }

    // Pot-driven volume display timeout (only when not in FS volume mode)
    if (!fsVolumeControlActive && currentScreen == SCREEN_VOLUME_CONTROL && (now - lastFsVolumeActivityMs) > FS_VOLUME_TIMEOUT_MS)
    {
        currentScreen = SCREEN_HOME;
        Serial.println("Volume display timeout (pot)");
    }

    // On raw press-edge, ensure FS1 remains true for at least the minimum window
    if (fs1_raw && !prevFs1 && !fsVolumeControlActive)
    {
        fs1ForcedUntilMs = now + FS1_MIN_ACTIVATION_MS;
    }

    // Pot-driven volume display timeout (only when not in FS volume mode)
    if (!fsVolumeControlActive && currentScreen == SCREEN_VOLUME_CONTROL && (now - lastFsVolumeActivityMs) > FS_VOLUME_TIMEOUT_MS)
    {
        currentScreen = SCREEN_HOME;
        Serial.println("Volume display timeout (pot)");
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
            if (currentSynthSound == SYNTHSND_SAMPLE)
            {
                setSampleGain(0.0f);
            }
            else
            {
                myEffect.amplitude(0);
                myEffect2.amplitude(0);
                myEffect3.amplitude(0);
            }
        }
    }
    else
    {
        // If chord is active and not fading, ensure amplitude reflects stored beepAmp
        if (chordActive && !chordFading)
        {
            if (currentSynthSound == SYNTHSND_SAMPLE)
            {
                setSampleGain(beepAmp);
            }
            else
            {
                float perVoice = beepAmp / 3.0f;
                myEffect.amplitude(perVoice);
                myEffect2.amplitude(perVoice);
                myEffect3.amplitude(perVoice);
            }
        }
    }

    // FS2 press edge: handle tap tempo or chord stop
    if (fs2 && !prevFs2 && !fsVolumeControlActive && now >= fsIgnoreInputsUntilMs)
    {
        // Check if we're in tap tempo mode
        if (tapTempoActive)
        {
            // Calculate tempo from tap interval
            unsigned long tapInterval = now - lastFs2TapMs;
            if (tapInterval > 50) // Debounce: ignore taps less than 50ms apart
            {
                // Convert interval to BPM: BPM = 60000 / interval_ms
                float newBPM = 60000.0f / (float)tapInterval;

                // Clamp to 40-200 BPM range
                if (newBPM < 40.0f)
                    newBPM = 40.0f;
                if (newBPM > 200.0f)
                    newBPM = 200.0f;

                globalTempoBPM = newBPM;

                // Update arp step duration: eighth note = (60000 / BPM) / 2
                arpStepDurationMs = (unsigned long)(30000.0f / globalTempoBPM);

                // Update the timer interval if arpeggiator is running
                updateArpTimerInterval();

                lastTapTempoActivityMs = now;
                Serial.print("Tap tempo: ");
                Serial.print(globalTempoBPM);
                Serial.println(" BPM");
            }
            lastFs2TapMs = now;
        }
        else
        {
            // Check for double-tap to enter tap tempo mode
            if ((now - lastFs2TapMs) <= 1000) // Double-tap within 1 second
            {
                // Enter tap tempo mode
                tapTempoActive = true;
                lastTapTempoActivityMs = now;
                lastFs2TapMs = now;
                currentScreen = SCREEN_TAP_TEMPO;

                // If fadeout was just started, abort it and restore volume
                if (chordFading)
                {
                    chordFading = false;
                    tapTempoAbortedVolume = chordFadeStartAmp;
                    beepAmp = tapTempoAbortedVolume;
                    // Restore oscillator amplitudes
                    updateChordVolume(tapTempoAbortedVolume);
                    Serial.println("Tap tempo mode activated - fadeout aborted");
                }
                else
                {
                    Serial.println("Tap tempo mode activated");
                }
            }
            else
            {
                // Normal FS2 behavior: stop chord
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

            lastFs2TapMs = now;
        }
    }

    // Tap tempo timeout: exit after 3 seconds of inactivity
    if (tapTempoActive && (now - lastTapTempoActivityMs) > 3000)
    {
        tapTempoActive = false;
        currentScreen = SCREEN_HOME;
        Serial.println("Tap tempo mode timeout");
    }

    // FS1 press edge: if chord was suppressed (stopped by FS2), re-enable it (unless in FS volume control mode, within settling time, or in tap tempo mode)
    if (fs1_raw && !prevFs1 && !fsVolumeControlActive && now >= fsIgnoreInputsUntilMs && !tapTempoActive)
    {
        // Connect pitch detector now that we need it
        enablePitchDetection();
        // Reset pitch detection to clear stale frequency data
        resetPitchDetection();

        if (chordSuppressed)
        {
            // Start with tonic=0; chord will update once fresh pitch is detected
            startChord(effectiveVolume, 0.0f, currentKey, currentMode);
        }
    }

    // Read pitch detection
    float frequency = 0.0;
    float probability = 0.0;
    const char *noteName = "---";
    updatePitchDetection(frequency, probability, noteName, currentInstrumentIsBass);

    // Update chord in real-time while sampling (only when FS1 is held and NOT in FS volume control mode or tap tempo mode)
    // Skip for sample mode – pitch detection doesn't affect sample playback
    if (fs1 && lastDetectedFrequency > 0.0f && !fsVolumeControlActive && !tapTempoActive && currentSynthSound != SYNTHSND_SAMPLE)
    {
        updateChordTonic(lastDetectedFrequency, currentKey, currentMode);
    }

    // FS1 release edge: disconnect pitch detector and start Rhodes decay if active
    if (!fs1_raw && prevFs1)
    {
        disablePitchDetection();
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
        renderVolumeControlScreen(effectiveVolume);
    }
    else if (currentScreen == SCREEN_TAP_TEMPO)
    {
        renderTapTempoScreen(globalTempoBPM);
    }

    // Track state for edge detection next iteration
    prevFs1 = fs1_raw;
    prevFs2 = fs2;
    prevEncButton = encButton;

    delay(50);
}
