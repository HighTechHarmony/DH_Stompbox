#include <Arduino.h>
#include <Wire.h>
#include <Audio.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <core_pins.h>
#include <EEPROM.h>

// ----------------------
// Configuration
// ----------------------
#define BOOST_INPUT_GAIN false // set to false to use 0.5 gain (no boost)
#define SUPPRESS_CHORD_OUTPUT_DURING_TRANSITIONS true // when true, mute chord until FS1 forced window ends

// Define audio objects
AudioSynthWaveform myEffect;
AudioSynthWaveform myEffect2;         // second voice (minor 3rd)
AudioSynthWaveform myEffect3;         // third voice (5th)
AudioInputI2S audioInput;             // Audio shield input
AudioOutputI2S audioOutput;           // Audio shield output
AudioMixer4 mixerLeft;                // Mix input + synth for left channel
AudioMixer4 mixerRight;               // Mix input + synth for right channel
AudioAnalyzeNoteFrequency noteDetect; // Pitch detection

// Reverb + wet/dry mixers
AudioEffectFreeverb reverb;       // stereo freeverb
AudioMixer4 wetDryLeft;          // mix dry (mixerLeft) + wet (reverb L)
AudioMixer4 wetDryRight;         // mix dry (mixerRight) + wet (reverb R)

// Synth-only mixers (so reverb is applied only to synth voices)
AudioMixer4 synthOnlyLeft;       // mix synth voices for left reverb input
AudioMixer4 synthOnlyRight;      // mix synth voices for right reverb input

// Reverb wet control (0.0 = dry, 1.0 = fully wet)
float reverbWet = 0.10f;

void setReverbWet(float wet)
{
    if (wet < 0.0f) wet = 0.0f;
    if (wet > 1.0f) wet = 1.0f;
    reverbWet = wet;
    float dryGain = 1.0f - reverbWet;
    float wetGain = reverbWet;
    wetDryLeft.gain(0, dryGain);
    wetDryLeft.gain(1, wetGain);
    wetDryRight.gain(0, dryGain);
    wetDryRight.gain(1, wetGain);
}

// Connect input (passthrough) to mixers
AudioConnection patchInL(audioInput, 0, mixerLeft, 0);  // left input → mixer L ch0
AudioConnection patchInR(audioInput, 1, mixerRight, 0); // right input → mixer R ch0

// Connect input to pitch detector (use left channel)
AudioConnection patchPitch(audioInput, 0, noteDetect, 0);

// Connect synth to mixers (root, minor 3rd, 5th)
AudioConnection patchSynthL(myEffect, 0, mixerLeft, 1);    // synth root → mixer L ch1
AudioConnection patchSynthR(myEffect, 0, mixerRight, 1);   // synth root → mixer R ch1
AudioConnection patchSynth2L(myEffect2, 0, mixerLeft, 2);  // synth 3rd → mixer L ch2
AudioConnection patchSynth2R(myEffect2, 0, mixerRight, 2); // synth 3rd → mixer R ch2
AudioConnection patchSynth3L(myEffect3, 0, mixerLeft, 3);  // synth 5th → mixer L ch3
AudioConnection patchSynth3R(myEffect3, 0, mixerRight, 3); // synth 5th → mixer R ch3

// Mix synth voices separately so reverb processes ONLY synths
AudioConnection patchSynthOnly1L(myEffect, 0, synthOnlyLeft, 0);
AudioConnection patchSynthOnly2L(myEffect2, 0, synthOnlyLeft, 1);
AudioConnection patchSynthOnly3L(myEffect3, 0, synthOnlyLeft, 2);

AudioConnection patchSynthOnly1R(myEffect, 0, synthOnlyRight, 0);
AudioConnection patchSynthOnly2R(myEffect2, 0, synthOnlyRight, 1);
AudioConnection patchSynthOnly3R(myEffect3, 0, synthOnlyRight, 2);

// Route synth-only mixers -> reverb (stereo)
AudioConnection patchReverbInL(synthOnlyLeft, 0, reverb, 0);
AudioConnection patchReverbInR(synthOnlyRight, 0, reverb, 1);

// Combine dry (original mixer with input passthrough) and wet (reverb)
AudioConnection patchDryL(mixerLeft, 0, wetDryLeft, 0);
AudioConnection patchWetL(reverb, 0, wetDryLeft, 1);

AudioConnection patchDryR(mixerRight, 0, wetDryRight, 0);
AudioConnection patchWetR(reverb, 1, wetDryRight, 1);

AudioConnection patchOutL(wetDryLeft, 0, audioOutput, 0);
AudioConnection patchOutR(wetDryRight, 0, audioOutput, 1);

// keep track of last detected tonic frequency
float lastDetectedFrequency = 0.0f;

// chord state
bool chordActive = false;
bool chordSuppressed = true;      // when true, automatic restart is disabled (e.g., FS2 pressed)
float currentChordTonic = 440.0f; // current tonic being played
// Key and mode configuration
int currentKey = 0;               // 0=C, 1=C#, 2=D, etc. (chromatic scale)
bool currentModeIsMajor = true;   // true=major, false=minor

// NVRAM (EEPROM) layout
#define NVRAM_SIGNATURE_ADDR 0
#define NVRAM_SIGNATURE 0xA5
#define NVRAM_KEY_ADDR 1
#define NVRAM_MODE_ADDR 2

void saveNVRAM()
{
    EEPROM.write(NVRAM_SIGNATURE_ADDR, NVRAM_SIGNATURE);
    EEPROM.write(NVRAM_KEY_ADDR, (uint8_t)currentKey);
    EEPROM.write(NVRAM_MODE_ADDR, (uint8_t)(currentModeIsMajor ? 1 : 0));
}

void loadNVRAM()
{
    if (EEPROM.read(NVRAM_SIGNATURE_ADDR) == NVRAM_SIGNATURE)
    {
        uint8_t k = EEPROM.read(NVRAM_KEY_ADDR);
        if (k < 12) currentKey = k;
        uint8_t m = EEPROM.read(NVRAM_MODE_ADDR);
        currentModeIsMajor = (m == 1);
        Serial.print("NVRAM: loaded key=");
        Serial.print(currentKey);
        Serial.print(" mode=");
        Serial.println(currentModeIsMajor ? "Major" : "Minor");
    }
    else
    {
        // Not initialized: use defaults and write them
        Serial.println("NVRAM: empty, using default C Major");
        currentKey = 0; // C
        currentModeIsMajor = true;
        saveNVRAM();
    }
}
// fade state for graceful stop
bool chordFading = false;
unsigned long chordFadeStartMs = 0;
unsigned long chordFadeDurationMs = 1500; //ramp down time (ms)
float chordFadeStartAmp = 0.0f;

unsigned long fs1ForcedUntilMs = 0;
const unsigned long FS1_MIN_ACTIVATION_MS = 500;  // Window of time after FS1 press that tracking is on

// UI screen state
enum ScreenMode { SCREEN_HOME, SCREEN_MENU, SCREEN_FADE };
ScreenMode currentScreen = SCREEN_HOME;
unsigned long lastEncoderActivityMs = 0;
const unsigned long SCREEN_TIMEOUT_MS = 5000;  // 5 seconds

// Menu state
enum MenuLevel { MENU_TOP, MENU_KEY_SELECT, MENU_MODE_SELECT };
MenuLevel currentMenuLevel = MENU_TOP;
int menuTopIndex = 0;        // 0=Key, 1=Mode
int menuKeyIndex = 0;        // 0-11 for key selection
int menuModeIndex = 0;       // 0=Major, 1=Minor

// Menu display names
const char* menuTopItems[] = {"Key", "Mode"};
const int MENU_TOP_COUNT = 2;

const char* keyMenuNames[] = {"A", "Bb", "B", "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab"};
const int KEY_MENU_COUNT = 12;
// Map menu index to chromatic scale (C=0, C#=1, ... B=11)
const int keyMenuToChromatic[] = {9, 10, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8}; // A, Bb, B, C, C#, D, D#, E, F, F#, G, G#

const char* modeMenuNames[] = {"Major", "Minor"};
const int MODE_MENU_COUNT = 2;

// Pin Assignments
const int ENC_A = 2;
const int ENC_B = 3;
const int ENC_BTN = 4;

const int FOOT1 = 12;
const int FOOT2 = 9;

const int POT_PIN = A1; // Audio shield VOL pad

// OLED Setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Audio Shield Objects
AudioControlSGTL5000 audioShield;
bool audioShieldEnabled = false; // Track status without calling enable() repeatedly

// Encoder State (interrupt-driven)
volatile int encoderRaw = 0;      // high-resolution transitions (4 per detent)
volatile int encoderPosition = 0; // user-facing detent count
volatile uint8_t encoderState = 0;
volatile unsigned long lastEncoderMicros = 0;

// state transition table: index = (prev<<2)|curr
const int8_t encoder_table[16] = {
    0, -1, +1, 0,
    +1, 0, 0, -1,
    -1, 0, 0, +1,
    0, +1, -1, 0};

// Encoder Reading
void encoderISR()
{
    uint8_t A = digitalRead(ENC_A);
    uint8_t B = digitalRead(ENC_B);
    uint8_t curr = (A << 1) | B;
    uint8_t idx = (encoderState << 2) | curr;
    encoderRaw += encoder_table[idx];
    encoderState = curr;

    // Only update user-facing position on full detent (typically 4 transitions)
    while (encoderRaw >= 4)
    {
        encoderPosition++;
        encoderRaw -= 4;
    }
    while (encoderRaw <= -4)
    {
        encoderPosition--;
        encoderRaw += 4;
    }
}

// Non-blocking beep state
bool beepActive = false;
unsigned long beepEndMillis = 0;
float beepAmp = 0.5f;

// Helper function to get diatonic third interval (in semitones) for a given note
// within a specified key and mode
float getDiatonicThird(float noteFreq, int keyNote, bool isMajor)
{
    // Convert frequency to MIDI note number
    float midiNote = 12.0f * log2f(noteFreq / 440.0f) + 69.0f;
    int noteClass = ((int)round(midiNote)) % 12; // 0-11 chromatic position
    
    // Calculate position in scale relative to key
    int relativePosition = (noteClass - keyNote + 12) % 12;
    
    // Determine the third interval based on scale degree
    // Major scale intervals: W-W-H-W-W-W-H (steps from root: 0,2,4,5,7,9,11)
    // Natural minor scale intervals: W-H-W-W-H-W-W (steps from root: 0,2,3,5,7,8,10)
    
    int thirdSemitones = 3; // default to minor third
    
    if (isMajor) {
        // Major scale: major thirds on scale degrees I, IV, V (positions 0, 5, 7)
        if (relativePosition == 0 || relativePosition == 5 || relativePosition == 7) {
            thirdSemitones = 4; // major third
        } else if (relativePosition == 2 || relativePosition == 4 || relativePosition == 9 || relativePosition == 11) {
            thirdSemitones = 3; // minor third
        }
        // For diminished chord (degree VII, position 11), use minor third
    } else {
        // Natural minor scale: major thirds on scale degrees III, VI, VII (positions 3, 8, 10)
        if (relativePosition == 3 || relativePosition == 8 || relativePosition == 10) {
            thirdSemitones = 4; // major third
        } else if (relativePosition == 0 || relativePosition == 2 || relativePosition == 5 || relativePosition == 7) {
            thirdSemitones = 3; // minor third
        }
        // For diminished chord (degree II, position 2), use minor third
    }
    
    return powf(2.0f, thirdSemitones / 12.0f);
}

void startChord(float potNorm = 0.5f, float tonicFreq = 0.0f, int keyNote = 0, bool isMajor = true)
{
    // choose tonic: passed in or last detected
    float tonic = (tonicFreq > 1.0f) ? tonicFreq : lastDetectedFrequency;
    if (tonic <= 0.0f)
        tonic = 440.0f; // fallback to A4

    currentChordTonic = tonic;
    // clear suppression when starting chord explicitly
    chordSuppressed = false;
    // cancel any fade in progress
    chordFading = false;

    // compute triad intervals (diatonic third, perfect fifth)
    const float third = getDiatonicThird(tonic, keyNote, isMajor);
    const float fifth = powf(2.0f, 7.0f / 12.0f);      // +7 semitones

    myEffect.frequency(tonic);
    myEffect2.frequency(tonic * third);
    myEffect3.frequency(tonic * fifth);

    // distribute amplitude to avoid clipping (sum ~= potNorm)
    float perVoice = potNorm / 3.0f;
    myEffect.amplitude(perVoice);
    myEffect2.amplitude(perVoice);
    myEffect3.amplitude(perVoice);

    if (!chordActive)
    {
        Serial.println(">>> CHORD START at tonic " + String(tonic) + "Hz vol " + String(potNorm));
    }
    digitalWrite(LED_BUILTIN, HIGH);

    beepAmp = potNorm;
    chordActive = true;
}

void updateChordTonic(float tonicFreq, int keyNote = 0, bool isMajor = true)
{
    if (!chordActive || tonicFreq <= 0.0f)
        return;

    currentChordTonic = tonicFreq;

    // compute triad intervals (diatonic third, perfect fifth)
    const float third = getDiatonicThird(tonicFreq, keyNote, isMajor);
    const float fifth = powf(2.0f, 7.0f / 12.0f);      // +7 semitones

    myEffect.frequency(tonicFreq);
    myEffect2.frequency(tonicFreq * third);
    myEffect3.frequency(tonicFreq * fifth);

    Serial.println(">>> CHORD UPDATE tonic " + String(tonicFreq) + "Hz");
}


void stopChord()
{
    if (!chordActive)
        return;

    // If a fade duration is set, perform a non-blocking fade-out
    if (chordFadeDurationMs > 0)
    {
        chordFading = true;
        chordFadeStartMs = millis();
        chordFadeStartAmp = beepAmp; // capture current overall amplitude (0.0-1.0)
        // brief visible/audible indication that fade has started
        Serial.println(">>> CHORD END (fade start)");
        digitalWrite(LED_BUILTIN, HIGH);
        // leave chordActive true while fading; final suppression will be applied when fade completes
        return;
    }

    // Immediate stop (no fade)
    myEffect.amplitude(0);
    myEffect2.amplitude(0);
    myEffect3.amplitude(0);
    digitalWrite(LED_BUILTIN, LOW);
    chordActive = false;
    beepActive = false;
    // suppress automatic restart after an explicit stop (e.g., FS2 press)
    chordSuppressed = true;
    Serial.println(">>> CHORD END");
}

void updateChordVolume(float potNorm)
{
    if (chordActive || chordFading)
    {
        // If we're NOT currently fading, apply pot-based amplitude immediately.
        // While a fade is in progress, do not reset the fade start time or start amp,
        // otherwise the fade will never progress.
        if (!chordFading)
        {
            float perVoice = potNorm / 3.0f;
            myEffect.amplitude(perVoice);
            myEffect2.amplitude(perVoice);
            myEffect3.amplitude(perVoice);
            // Update overall stored amplitude
            beepAmp = potNorm;
        }
    }
}

/************* Setup   ****************/
void setup()
{
    Serial.begin(9600);
    // delay(500);

    Serial.println("=== SETUP START ===");

    // Allocate audio memory
    AudioMemory(64);
    Serial.println("Audio memory allocated");

    // Initialize audio shield FIRST - only call enable() ONCE
    if (audioShield.enable())
    {
        Serial.println("Audio Shield enabled");
        audioShieldEnabled = true;
    }
    else
    {
        Serial.println("Audio Shield FAILED");
        audioShieldEnabled = false;
    }

    audioShield.volume(0.5);
    Serial.println("Init Beep @ 0.5 volume");

    // Initialize waveforms
    myEffect.begin(WAVEFORM_SINE);
    myEffect.frequency(1000);
    myEffect.amplitude(0);

    myEffect2.begin(WAVEFORM_SINE);
    myEffect2.frequency(1000);
    myEffect2.amplitude(0);

    myEffect3.begin(WAVEFORM_SINE);
    myEffect3.frequency(1000);
    myEffect3.amplitude(0);

    Serial.println("Waveforms initialized (3 voices for chord)");

    // Initialize pitch detector
    noteDetect.begin(0.10); // threshold typical 0.15 (0.0 = very sensitive, 1.0 = very picky)
    Serial.println("Pitch detector initialized");

    // Load saved Key/Mode from NVRAM (EEPROM). If empty, defaults to C Major.
    loadNVRAM();

    // Configure mixers: boost input passthrough, lower synth to prevent clipping
    float inputGain = BOOST_INPUT_GAIN ? 1.5 : 1.0; // full volume passthrough
    float synthGain = 0.2;                          // lower synth voices to allow headroom when combined with input

    mixerLeft.gain(0, inputGain); // input left
    mixerLeft.gain(1, synthGain); // synth root
    mixerLeft.gain(2, synthGain); // synth 3rd
    mixerLeft.gain(3, synthGain); // synth 5th

    mixerRight.gain(0, inputGain); // input right
    mixerRight.gain(1, synthGain); // synth root
    mixerRight.gain(2, synthGain); // synth 3rd
    mixerRight.gain(3, synthGain); // synth 5th

    Serial.print("Audio mixers configured: input gain ");
    Serial.print(inputGain);
    Serial.print(", synth gain ");
    Serial.println(synthGain);

    // Initialize reverb wet/dry balance (hardcoded to 30% wet)
    setReverbWet(reverbWet);
    // Optional reverb character settings
    reverb.roomsize(0.6f);
    reverb.damping(0.5f);

    // Configure synth-only mixers so reverb receives synth voices at unity
    synthOnlyLeft.gain(0, 1.0f);
    synthOnlyLeft.gain(1, 1.0f);
    synthOnlyLeft.gain(2, 1.0f);

    synthOnlyRight.gain(0, 1.0f);
    synthOnlyRight.gain(1, 1.0f);
    synthOnlyRight.gain(2, 1.0f);

    Serial.println("Playing startup beep 100ms @ 0.7");
    myEffect.frequency(1000);
    myEffect.amplitude(0.7);
    delay(100);
    myEffect.amplitude(0);
    Serial.println("Startup beep complete");

    // Pins
    pinMode(ENC_A, INPUT_PULLUP);
    pinMode(ENC_B, INPUT_PULLUP);
    pinMode(ENC_BTN, INPUT_PULLUP);
    pinMode(FOOT1, INPUT_PULLUP);
    pinMode(FOOT2, INPUT_PULLUP);
    pinMode(POT_PIN, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    // OLED init
    bool oledAvailable = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    if (!oledAvailable)
    {
        Serial.println("OLED not found");
    }

    if (oledAvailable)
    {
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
    else
    {
        // fallback: log status to serial if OLED is not available
        Serial.print("AudioShield: ");
        Serial.println(audioShieldEnabled ? "ENABLED" : "DISABLED");
    }

    // Initial encoder state and attach interrupts
    encoderState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
    attachInterrupt(digitalPinToInterrupt(ENC_A), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B), encoderISR, CHANGE);

    Serial.println("=== SETUP COMPLETE ===");
}

/************* Loop    ****************/
void loop()
{
    static bool prevFs1 = false; // track previous FS1 state for edge detection
    static bool prevFs2 = false; // track previous FS2 state for edge detection
    unsigned long now = millis();

    int potRaw = analogRead(POT_PIN); // 0–1023
    float potNorm = potRaw / 1023.0;  // 0.0–1.0

    // Update chord volume in real-time
    updateChordVolume(potNorm);

    // Handle non-blocking fade-out if requested
    if (chordFading)
    {
        unsigned long nowMs = millis();
        unsigned long elapsed = nowMs - chordFadeStartMs;
        if (elapsed >= chordFadeDurationMs)
        {
            // Fade complete: ensure amplitudes are zero and mark stopped
            myEffect.amplitude(0);
            myEffect2.amplitude(0);
            myEffect3.amplitude(0);
            digitalWrite(LED_BUILTIN, LOW);
            chordFading = false;
            chordActive = false;
            beepActive = false;
            chordSuppressed = true;
            beepAmp = 0.0f;
            Serial.println(">>> CHORD END (fade complete)");
            // return to home screen after fade completes
            currentScreen = SCREEN_HOME;
        }
        else
        {
            float t = (float)elapsed / (float)chordFadeDurationMs; // 0..1
            float curAmp = chordFadeStartAmp * (1.0f - t);
            float perVoice = curAmp / 3.0f;
            myEffect.amplitude(perVoice);
            myEffect2.amplitude(perVoice);
            myEffect3.amplitude(perVoice);
            beepAmp = curAmp;
        }
    }

    // Restart chord if not active (ensure continuous playback)
    if (!chordActive && !chordSuppressed)
    {
        startChord(potNorm, currentChordTonic, currentKey, currentModeIsMajor);
    }

    bool encButton = !digitalRead(ENC_BTN);
    static bool prevEncButton = false;
    bool fs1_raw = !digitalRead(FOOT1);
    bool fs2 = !digitalRead(FOOT2);

    // On raw press-edge, ensure FS1 remains true for at least the minimum window
    if (fs1_raw && !prevFs1) {
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

    // Read pitch detection with median filtering
    float frequency = 0.0;
    float probability = 0.0;
    const char *noteName = "---";
    static float sampledFrequency = 0.0f; // frequency sampled while FS1 held
    static float freqBuf[3] = {0, 0, 0}; // median filter buffer
    static int freqBufIdx = 0;

    if (noteDetect.available())
    {
        frequency = noteDetect.read();
        probability = noteDetect.probability();

        // Add raw frequency to median filter buffer
        freqBuf[freqBufIdx] = frequency;
        freqBufIdx = (freqBufIdx + 1) % 3;

        // Copy and sort for median calculation
        float sorted[5];
        memcpy(sorted, freqBuf, sizeof(sorted));
        for (int i = 0; i < 5; i++)
            for (int j = i + 1; j < 5; j++)
                if (sorted[j] < sorted[i])
                {
                    float temp = sorted[i];
                    sorted[i] = sorted[j];
                    sorted[j] = temp;
                }

        float medianFreq = sorted[2]; // middle value after sorting

        // Use probability as a smoothing weight (rather than gating on a threshold).
        // Normalize the new median into the same octave range as `sampledFrequency`
        if (medianFreq > 50.0 && medianFreq < 2000.0)
        {
            float newNorm = medianFreq;
            if (newNorm < 200.0f) newNorm = newNorm * 2.0f;
            else if (newNorm > 950.0f) newNorm = newNorm / 2.0f;
            if (sampledFrequency <= 0.0f)
            {
                // First valid sample: initialize without smoothing
                sampledFrequency = newNorm;
            }
            else
            {
                // Smooth using probability as weight: higher probability -> more trust in new value
                float smoothed = probability * newNorm + (1.0f - probability) * sampledFrequency;
                sampledFrequency = smoothed;
            }

            // Update last detected frequency for external use
            lastDetectedFrequency = sampledFrequency;

            // Update chord in real-time while sampling (only when FS1 is held)
            if (fs1)
            {
                updateChordTonic(sampledFrequency, currentKey, currentModeIsMajor);
            }
        }

        // Simple note name lookup (A4 = 440 Hz)
        // Use the smoothed `sampledFrequency` (which already uses `probability` as a weight)
        if (sampledFrequency > 0.0f)
        {
            // Calculate note from frequency: n = 12 * log2(f/440) + 69 (MIDI note number)
            float n = 12.0 * log2f(sampledFrequency / 440.0) + 69.0;
            int noteNum = (int)(n + 0.5) % 12;
            if (noteNum < 0) noteNum += 12;
            const char *noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            noteName = noteNames[noteNum];
        }
    }

    // Detect encoder activity for UI timeout and menu navigation
    static int lastEncoderPosition = 0;
    
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

            if (currentMenuLevel == MENU_TOP)
            {
                menuTopIndex += delta;
                if (menuTopIndex < 0) menuTopIndex = 0;
                if (menuTopIndex > MENU_TOP_COUNT) menuTopIndex = MENU_TOP_COUNT; // allow Parent as final entry
            }
            else if (currentMenuLevel == MENU_KEY_SELECT)
            {
                menuKeyIndex += delta;
                if (menuKeyIndex < 0) menuKeyIndex = 0;
                if (menuKeyIndex > KEY_MENU_COUNT) menuKeyIndex = KEY_MENU_COUNT; // allow Parent
            }
            else if (currentMenuLevel == MENU_MODE_SELECT)
            {
                menuModeIndex += delta;
                if (menuModeIndex < 0) menuModeIndex = 0;
                if (menuModeIndex > MODE_MENU_COUNT) menuModeIndex = MODE_MENU_COUNT; // allow Parent
            }
        }

        lastEncoderPosition = encoderPosition;
    }
    
    // Handle encoder button press for menu selection
    if (encButton && !prevEncButton)  // button press edge
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
            lastEncoderActivityMs = now;  // keep menu active

            if (currentMenuLevel == MENU_TOP)
            {
                // Enter submenu based on selected top-level item or handle Parent
                if (menuTopIndex == 0)  // Key
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
                else if (menuTopIndex == 1)  // Mode
                {
                    currentMenuLevel = MENU_MODE_SELECT;
                    menuModeIndex = currentModeIsMajor ? 0 : 1;
                }
                else if (menuTopIndex == MENU_TOP_COUNT)
                {
                    // Parent selected at top -> exit to main screen
                    currentScreen = SCREEN_HOME;
                    currentMenuLevel = MENU_TOP;
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
        }
    }
    
    // Timeout back to home screen after inactivity
    if (currentScreen == SCREEN_MENU && (now - lastEncoderActivityMs) > SCREEN_TIMEOUT_MS)
    {
        currentScreen = SCREEN_HOME;
        currentMenuLevel = MENU_TOP;  // Reset to top level when timing out
    }

    // Serial output
    int rawBtn = digitalRead(ENC_BTN);
    int rawFS1 = digitalRead(FOOT1);
    int rawFS2 = digitalRead(FOOT2);

    Serial.print("AudioShield: ");
    Serial.print(audioShieldEnabled ? "ENABLED " : "DISABLED ");
    Serial.print("Enc:");
    Serial.print(encoderPosition);
    Serial.print(" Btn:");
    Serial.print(encButton ? "PRESSED" : "released");
    Serial.print(" (raw:");
    Serial.print(rawBtn);
    Serial.print(") FS1:");
    Serial.print(fs1 ? "ON" : "off");
    Serial.print(" (raw:");
    Serial.print(rawFS1);
    Serial.print(") FS2:");
    Serial.print(fs2 ? "ON" : "off");
    Serial.print(" (raw:");
    Serial.print(rawFS2);
    Serial.println(")");
    Serial.print("Pot raw: ");
    Serial.print(potRaw);
    Serial.print(" Note: ");
    Serial.print(noteName);
    Serial.print(" (");
    Serial.print(frequency, 1);
    Serial.print(" Hz, prob: ");
    Serial.print(probability, 2);
    Serial.println(")");

    // OLED output - show appropriate screen
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    if (currentScreen == SCREEN_HOME)
    {
        // Home screen with larger font
        display.setTextSize(2);
        
        int y = 0;
        
        // Display key and mode
        display.setCursor(0, y);
        display.print("Key: ");
        const char *keyNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        display.print(keyNames[currentKey]);
        if (!currentModeIsMajor)
        {
            display.print("m");
        }
        y += 20;
        
        // Display detected note and frequency
        display.setCursor(0, y);
        display.setTextSize(2);
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
        
    }
    else if (currentScreen == SCREEN_MENU)
    {
        // Menu screen - show current menu level
        display.setTextSize(2);

        if (currentMenuLevel == MENU_TOP)
        {
            // Top-level menu
            display.setCursor(0, 0);
            display.println("Menu");

            // Show top items + Parent at end
            int visibleCount = MENU_TOP_COUNT + 1; // includes Parent
            for (int i = 0; i < visibleCount; i++)
            {
                int y = 18 + i * 18;
                display.setCursor(0, y);
                if (i == menuTopIndex)
                {
                    display.print("> ");
                }
                else
                {
                    display.print("  ");
                }

                if (i < MENU_TOP_COUNT)
                    display.println(menuTopItems[i]);
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
            int visible = 3; // number of lines to show (fits size2)
            int startIdx = menuKeyIndex - 1;
            if (startIdx < 0) startIdx = 0;
            if (startIdx > totalCount - visible) startIdx = totalCount - visible;
            if (startIdx < 0) startIdx = 0;

            for (int i = 0; i < visible; i++)
            {
                int idx = startIdx + i;
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
            int startIdx = menuModeIndex - 1;
            if (startIdx < 0) startIdx = 0;
            if (startIdx > totalCount - visible) startIdx = totalCount - visible;
            if (startIdx < 0) startIdx = 0;

            for (int i = 0; i < visible; i++)
            {
                int idx = startIdx + i;
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
    }
    else if (currentScreen == SCREEN_FADE)
    {
        // Fullscreen fade progress bar
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
                if (elapsed < 0) elapsed = 0;
                if (elapsed > (long)chordFadeDurationMs) elapsed = chordFadeDurationMs;
                progress = (float)elapsed / (float)chordFadeDurationMs;
            }
            int barW = SCREEN_WIDTH - (int)(progress * (float)SCREEN_WIDTH);
        // draw filled bar (starts full and empties as progress increases)
        display.fillRect(0, 0, barW, SCREEN_HEIGHT, SSD1306_WHITE);
        // draw the word "FADEOUT" in inverse color centered
        char buf[8];
        // int pct = (int)(progress * 100.0f + 0.5f);
        sprintf(buf, "%s", "FADEOUT");
        display.setTextSize(2);
        display.setTextColor(SSD1306_BLACK);
        int tx = (SCREEN_WIDTH - (6 * strlen(buf))) / 2; // approx width per char
        int ty = (SCREEN_HEIGHT / 2) - 8;
        if (tx < 0) tx = 0;
        display.setCursor(tx, ty);
        display.println(buf);
        display.setTextColor(SSD1306_WHITE);
    }
    }

    display.display();

    // Track raw FS1/FS2 and button state for edge detection next iteration
    prevFs1 = fs1_raw;
    prevFs2 = fs2;
    prevEncButton = encButton;

    delay(50);
}
