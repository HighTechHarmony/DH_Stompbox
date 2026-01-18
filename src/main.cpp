#include <Arduino.h>
#include <Wire.h>
#include <Audio.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <core_pins.h>

// ----------------------
// Configuration
// ----------------------
#define BOOST_INPUT_GAIN false // set to false to use 0.5 gain (no boost)

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
// fade state for graceful stop
bool chordFading = false;
unsigned long chordFadeStartMs = 0;
unsigned long chordFadeDurationMs = 2000; // default ramp down time (ms) - easily changeable
float chordFadeStartAmp = 0.0f;

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
    noteDetect.begin(0.10); // threshold 0.15 (0.0 = very sensitive, 1.0 = very picky)
    Serial.println("Pitch detector initialized");

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
    bool fs1 = !digitalRead(FOOT1);
    bool fs2 = !digitalRead(FOOT2);

    // FS2 press edge: stop chord immediately on initial press
    if (fs2 && !prevFs2)
    {
        stopChord();
    }

    // FS1 press edge: if chord was suppressed (stopped by FS2), re-enable it
    if (fs1 && !prevFs1)
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
        freqBufIdx = (freqBufIdx + 1) % 5;

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

    // OLED output with adjustable line spacing
    display.clearDisplay();

    int ts = 1; // text size
    display.setTextSize(ts);
    display.setTextColor(SSD1306_WHITE);

    const int fontHeight = 8;
    const int extraSpacing = 2;
    const int lineHeight = fontHeight * ts + extraSpacing;

    int y = 0;
    display.setCursor(0, y);
    display.println("DH Stomp");
    y += lineHeight;

    display.setCursor(0, y);
    display.print("Note: ");
    display.print(noteName);
    display.print(" ");
    display.print(frequency, 1);
    display.println("Hz");
    y += lineHeight;

    display.setCursor(0, y);
    display.print("Enc: ");
    display.println(encoderPosition);
    y += lineHeight;

    display.setCursor(0, y);
    display.print("Btn: ");
    display.println(encButton ? "PRESSED" : "released");
    y += lineHeight;

    display.setCursor(0, y);
    display.print("FS1: ");
    display.print(fs1 ? "ON " : "off");
    display.print(" FS2: ");
    display.println(fs2 ? "ON" : "off");

    display.display();

    // Track FS1/FS2 state for next iteration
    prevFs1 = fs1;
    prevFs2 = fs2;

    delay(25);
}
