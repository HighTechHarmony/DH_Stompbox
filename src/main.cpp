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

// Connect mixers to output
AudioConnection patchOutL(mixerLeft, 0, audioOutput, 0);  // mixer L → left output
AudioConnection patchOutR(mixerRight, 0, audioOutput, 1); // mixer R → right output

// keep track of last detected tonic frequency
float lastDetectedFrequency = 0.0f;

// chord state
bool chordActive = false;
bool chordSuppressed = true;      // when true, automatic restart is disabled (e.g., FS2 pressed)
float currentChordTonic = 440.0f; // current tonic being played
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

void startChord(float potNorm = 0.5f, float tonicFreq = 0.0f)
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

    // compute minor triad intervals (equal temperament)
    const float minorThird = powf(2.0f, 3.0f / 12.0f); // +3 semitones
    const float fifth = powf(2.0f, 7.0f / 12.0f);      // +7 semitones

    myEffect.frequency(tonic);
    myEffect2.frequency(tonic * minorThird);
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

void updateChordTonic(float tonicFreq)
{
    if (!chordActive || tonicFreq <= 0.0f)
        return;

    currentChordTonic = tonicFreq;

    // compute minor triad intervals (equal temperament)
    const float minorThird = powf(2.0f, 3.0f / 12.0f); // +3 semitones
    const float fifth = powf(2.0f, 7.0f / 12.0f);      // +7 semitones

    myEffect.frequency(tonicFreq);
    myEffect2.frequency(tonicFreq * minorThird);
    myEffect3.frequency(tonicFreq * fifth);

    Serial.println(">>> CHORD UPDATE tonic " + String(tonicFreq) + "Hz");
}

// Legacy function for timed beeps (kept for compatibility)
void startBeep(int durationMs, float potNorm = 0.5f, float tonicFreq = 0.0f)
{
    startChord(potNorm, tonicFreq);
    beepEndMillis = millis() + (unsigned long)durationMs;
    beepActive = true;
}

void stopBeep()
{
    if (!beepActive)
        return;
    // Only stop if using timed beep mode, not continuous chord
    beepActive = false;
    // Note: chord continues playing - use stopChord() to fully stop
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

void updateBeep()
{
    if (beepActive && (long)(millis() - beepEndMillis) >= 0)
    {
        stopBeep();
    }
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
    noteDetect.begin(0.15); // threshold 0.15 (0.0 = very sensitive, 1.0 = very picky)
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
        startChord(potNorm, currentChordTonic);
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
            startChord(potNorm, tonicToUse);
        }
    }

    // Read pitch detection
    float frequency = 0.0;
    float probability = 0.0;
    const char *noteName = "---";
    static float sampledFrequency = 0.0f; // frequency sampled while FS1 held

    if (noteDetect.available())
    {
        frequency = noteDetect.read();
        probability = noteDetect.probability();

        // Only sample input note when FS1 is held
        if (fs1 && probability > 0.85 && frequency > 50.0 && frequency < 2000.0)
        {
            // normalize to a consistent octave range (220-880 Hz) to avoid octave jumps
            float normalizedFreq = frequency;
            while (normalizedFreq < 220.0)
                normalizedFreq *= 2.0;
            while (normalizedFreq > 880.0)
                normalizedFreq /= 2.0;
            sampledFrequency = normalizedFreq;
            lastDetectedFrequency = normalizedFreq;

            // Update chord in real-time while sampling
            updateChordTonic(normalizedFreq);
        }

        // Simple note name lookup (A4 = 440 Hz)
        if (probability > 0.9) // only show if confident
        {
            // Calculate note from frequency: n = 12 * log2(f/440) + 69 (MIDI note number)
            float n = 12.0 * log2f(frequency / 440.0) + 69.0;
            int noteNum = (int)(n + 0.5) % 12;
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

    // handle non-blocking beep termination (for timed beeps if used)
    updateBeep();

    // Track FS1/FS2 state for next iteration
    prevFs1 = fs1;
    prevFs2 = fs2;

    delay(50);
}
