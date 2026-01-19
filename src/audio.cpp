#include "audio.h"
#include "pitch.h"
#include "NVRAM.h"

// Define audio objects
AudioSynthWaveform myEffect;
AudioSynthWaveform myEffect2; // second voice (minor 3rd)
AudioSynthWaveform myEffect3; // third voice (5th)
AudioInputI2S audioInput;     // Audio shield input
AudioOutputI2S audioOutput;   // Audio shield output
AudioMixer4 mixerLeft;        // Mix input + synth for left channel
AudioMixer4 mixerRight;       // Mix input + synth for right channel
AudioAnalyzePeak peak1;       // Input peak detection for test mode

// Reverb + wet/dry mixers
AudioEffectFreeverb reverb; // stereo freeverb
AudioMixer4 wetDryLeft;     // mix dry (mixerLeft) + wet (reverb L)
AudioMixer4 wetDryRight;    // mix dry (mixerRight) + wet (reverb R)

// Synth-only mixers (so reverb is applied only to synth voices)
AudioMixer4 synthOnlyLeft;  // mix synth voices for left reverb input
AudioMixer4 synthOnlyRight; // mix synth voices for right reverb input

// Audio shield control
AudioControlSGTL5000 audioShield;
bool audioShieldEnabled = false;

// Reverb wet control (0.0 = dry, 1.0 = fully wet)
float reverbWet = 0.10f;

// chord state
bool chordActive = false;
bool chordSuppressed = true;      // when true, automatic restart is disabled (e.g., FS2 pressed)
float currentChordTonic = 440.0f; // current tonic being played

// fade state for graceful stop
bool chordFading = false;
unsigned long chordFadeStartMs = 0;
unsigned long chordFadeDurationMs = 1500; // ramp down time (ms)
float chordFadeStartAmp = 0.0f;
float beepAmp = 0.5f;

// Audio connections
AudioConnection patchInL(audioInput, 0, mixerLeft, 0);  // left input → mixer L ch0
AudioConnection patchInR(audioInput, 1, mixerRight, 0); // right input → mixer R ch0
AudioConnection patchPitch(audioInput, 0, noteDetect, 0);
AudioConnection patchPeak(audioInput, 0, peak1, 0);
AudioConnection patchSynthL(myEffect, 0, mixerLeft, 1);    // synth root → mixer L ch1
AudioConnection patchSynthR(myEffect, 0, mixerRight, 1);   // synth root → mixer R ch1
AudioConnection patchSynth2L(myEffect2, 0, mixerLeft, 2);  // synth 3rd → mixer L ch2
AudioConnection patchSynth2R(myEffect2, 0, mixerRight, 2); // synth 3rd → mixer R ch2
AudioConnection patchSynth3L(myEffect3, 0, mixerLeft, 3);  // synth 5th → mixer L ch3
AudioConnection patchSynth3R(myEffect3, 0, mixerRight, 3); // synth 5th → mixer R ch3
AudioConnection patchSynthOnly1L(myEffect, 0, synthOnlyLeft, 0);
AudioConnection patchSynthOnly2L(myEffect2, 0, synthOnlyLeft, 1);
AudioConnection patchSynthOnly3L(myEffect3, 0, synthOnlyLeft, 2);
AudioConnection patchSynthOnly1R(myEffect, 0, synthOnlyRight, 0);
AudioConnection patchSynthOnly2R(myEffect2, 0, synthOnlyRight, 1);
AudioConnection patchSynthOnly3R(myEffect3, 0, synthOnlyRight, 2);
AudioConnection patchReverbInL(synthOnlyLeft, 0, reverb, 0);
AudioConnection patchReverbInR(synthOnlyRight, 0, reverb, 1);
AudioConnection patchDryL(mixerLeft, 0, wetDryLeft, 0);
AudioConnection patchWetL(reverb, 0, wetDryLeft, 1);
AudioConnection patchDryR(mixerRight, 0, wetDryRight, 0);
AudioConnection patchWetR(reverb, 1, wetDryRight, 1);
AudioConnection patchOutL(wetDryLeft, 0, audioOutput, 0);
AudioConnection patchOutR(wetDryRight, 0, audioOutput, 1);

void setReverbWet(float wet)
{
    if (wet < 0.0f)
        wet = 0.0f;
    if (wet > 1.0f)
        wet = 1.0f;
    reverbWet = wet;
    float dryGain = 1.0f - reverbWet;
    float wetGain = reverbWet;
    wetDryLeft.gain(0, dryGain);
    wetDryLeft.gain(1, wetGain);
    wetDryRight.gain(0, dryGain);
    wetDryRight.gain(1, wetGain);
}

float getDiatonicThird(float noteFreq, int keyNote, bool isMajor)
{
    // Convert frequency to MIDI note number
    float midiNote = 12.0f * log2f(noteFreq / 440.0f) + 69.0f;
    int noteClass = ((int)round(midiNote)) % 12; // 0-11 chromatic position

    // Calculate position in scale relative to key
    int relativePosition = (noteClass - keyNote + 12) % 12;

    // Determine the third interval based on scale degree
    int thirdSemitones = 3; // default to minor third

    if (isMajor)
    {
        // Major scale: major thirds on scale degrees I, IV, V (positions 0, 5, 7)
        if (relativePosition == 0 || relativePosition == 5 || relativePosition == 7)
        {
            thirdSemitones = 4; // major third
        }
        else if (relativePosition == 2 || relativePosition == 4 || relativePosition == 9 || relativePosition == 11)
        {
            thirdSemitones = 3; // minor third
        }
    }
    else
    {
        // Natural minor scale: major thirds on scale degrees III, VI, VII (positions 3, 8, 10)
        if (relativePosition == 3 || relativePosition == 8 || relativePosition == 10)
        {
            thirdSemitones = 4; // major third
        }
        else if (relativePosition == 0 || relativePosition == 2 || relativePosition == 5 || relativePosition == 7)
        {
            thirdSemitones = 3; // minor third
        }
    }

    return powf(2.0f, thirdSemitones / 12.0f);
}

void startChord(float potNorm, float tonicFreq, int keyNote, bool isMajor)
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
    const float fifth = powf(2.0f, 7.0f / 12.0f); // +7 semitones

    // apply octave shift
    float octaveMul = powf(2.0f, (float)currentOctaveShift);
    myEffect.frequency(tonic * octaveMul);
    myEffect2.frequency(tonic * third * octaveMul);
    myEffect3.frequency(tonic * fifth * octaveMul);

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

void updateChordTonic(float tonicFreq, int keyNote, bool isMajor)
{
    if (!chordActive || tonicFreq <= 0.0f)
        return;

    currentChordTonic = tonicFreq;

    // compute triad intervals (diatonic third, perfect fifth)
    const float third = getDiatonicThird(tonicFreq, keyNote, isMajor);
    const float fifth = powf(2.0f, 7.0f / 12.0f); // +7 semitones

    // apply octave shift
    float octaveMul = powf(2.0f, (float)currentOctaveShift);
    myEffect.frequency(tonicFreq * octaveMul);
    myEffect2.frequency(tonicFreq * third * octaveMul);
    myEffect3.frequency(tonicFreq * fifth * octaveMul);

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

void updateChordFade()
{
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
}

void setupAudio()
{
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

// Configure mixers
#define BOOST_INPUT_GAIN false
    float inputGain = BOOST_INPUT_GAIN ? 1.5 : 1.0;
    float synthGain = 0.2;

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

    // Initialize reverb wet/dry balance
    setReverbWet(reverbWet);
    reverb.roomsize(0.6f);
    reverb.damping(0.5f);

    // Configure synth-only mixers
    synthOnlyLeft.gain(0, 1.0f);
    synthOnlyLeft.gain(1, 1.0f);
    synthOnlyLeft.gain(2, 1.0f);

    synthOnlyRight.gain(0, 1.0f);
    synthOnlyRight.gain(1, 1.0f);
    synthOnlyRight.gain(2, 1.0f);

    // Startup beep
    Serial.println("Playing startup beep 100ms @ 0.7");
    myEffect.frequency(1000);
    myEffect.amplitude(0.7);
    delay(100);
    myEffect.amplitude(0);
    Serial.println("Startup beep complete");
}
