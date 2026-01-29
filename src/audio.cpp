#include "audio.h"
#include "pitch.h"
#include "NVRAM.h"

// Define audio objects
AudioSynthWaveform myEffect;
AudioSynthWaveform myEffect2; // second voice (minor 3rd)
AudioSynthWaveform myEffect3; // third voice (5th)
// Additional oscillators for organ sound
AudioSynthWaveform myEffectOrg2;
AudioSynthWaveform myEffectOrg3;
AudioSynthWaveform myEffect2Org2;
AudioSynthWaveform myEffect2Org3;
AudioSynthWaveform myEffect3Org2;
AudioSynthWaveform myEffect3Org3;
// Additional oscillators for Rhodes and Strings (2 per voice)
AudioSynthWaveform myEffectRhodes2;
AudioSynthWaveform myEffect2Rhodes2;
AudioSynthWaveform myEffect3Rhodes2;
AudioInputI2S audioInput;   // Audio shield input
AudioOutputI2S audioOutput; // Audio shield output
AudioMixer4 mixerLeft;      // Mix input + synth for left channel
AudioMixer4 mixerRight;     // Mix input + synth for right channel
AudioAnalyzePeak peak1;     // Input peak detection for test mode

// Reverb + wet/dry mixers
AudioEffectFreeverb reverb; // stereo freeverb
AudioMixer4 wetDryLeft;     // mix dry (mixerLeft) + wet (reverb L)
AudioMixer4 wetDryRight;    // mix dry (mixerRight) + wet (reverb R)

// Synth-only mixers (so reverb is applied only to synth voices)
AudioMixer4 synthOnlyLeft;  // mix synth voices for left reverb input
AudioMixer4 synthOnlyRight; // mix synth voices for right reverb input

// Sub-mixers to combine multiple oscillators per voice before main mixer
AudioMixer4 voiceMix1; // combines all root voice oscillators
AudioMixer4 voiceMix2; // combines all third voice oscillators
AudioMixer4 voiceMix3; // combines all fifth voice oscillators

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

// Organ vibrato state (deep vibrato for organ sound)
bool organVibratoEnabled = true;
float organVibratoRate = 6.0f;    // Hz
float organVibratoDepth = 0.015f; // fractional depth (±1.5%)

// Detune ratios used by organ voices
float organDetune1 = 1.002f; // +~3.5 cents
float organDetune2 = 0.998f; // -~3.5 cents

// Base frequencies for vibrato application (set when organ is initialized/updated)
float organBaseRootFreq = 440.0f;
float organBaseThirdFreq = 440.0f;
float organBaseFifthFreq = 440.0f;

// Rhodes decay state (2 second decay after FS1 release)
bool rhodesDecaying = false;
unsigned long rhodesDecayStartMs = 0;
unsigned long rhodesDecayDurationMs = 2000; // 2 seconds
float rhodesDecayStartAmp = 0.0f;

// Arpeggiator state (120 BPM eighth notes)
volatile int currentArpMode = 1;                // 0=Arp, 1=Poly (default Poly)
volatile int arpCurrentStep = 0;                // 0=root, 1=third, 2=fifth
volatile unsigned long arpStepDurationMs = 125; // 125ms = eighth note at 120 BPM (will be updated by tempo)
volatile bool arpTimerActive = false;           // True when arp timer is running
float globalTempoBPM = 120.0f;                  // Global tempo

// IntervalTimer for precise arpeggiator timing
IntervalTimer arpTimer;

// Audio connections
AudioConnection patchInL(audioInput, 0, mixerLeft, 0);  // left input → mixer L ch0
AudioConnection patchInR(audioInput, 1, mixerRight, 0); // right input → mixer R ch0
AudioConnection patchPitch(audioInput, 0, noteDetect, 0);
AudioConnection patchPeak(audioInput, 0, peak1, 0);

// Connect all oscillators to voice sub-mixers first
// Voice 1 (root) - all root oscillators to voiceMix1
AudioConnection patchVoice1a(myEffect, 0, voiceMix1, 0);
AudioConnection patchVoice1b(myEffectOrg2, 0, voiceMix1, 1);
AudioConnection patchVoice1c(myEffectOrg3, 0, voiceMix1, 2);
AudioConnection patchVoice1d(myEffectRhodes2, 0, voiceMix1, 3);
// Voice 2 (third) - all third oscillators to voiceMix2
AudioConnection patchVoice2a(myEffect2, 0, voiceMix2, 0);
AudioConnection patchVoice2b(myEffect2Org2, 0, voiceMix2, 1);
AudioConnection patchVoice2c(myEffect2Org3, 0, voiceMix2, 2);
AudioConnection patchVoice2d(myEffect2Rhodes2, 0, voiceMix2, 3);
// Voice 3 (fifth) - all fifth oscillators to voiceMix3
AudioConnection patchVoice3a(myEffect3, 0, voiceMix3, 0);
AudioConnection patchVoice3b(myEffect3Org2, 0, voiceMix3, 1);
AudioConnection patchVoice3c(myEffect3Org3, 0, voiceMix3, 2);
AudioConnection patchVoice3d(myEffect3Rhodes2, 0, voiceMix3, 3);

// Connect voice mixers to main output mixers
AudioConnection patchVoice1ToL(voiceMix1, 0, mixerLeft, 1);
AudioConnection patchVoice1ToR(voiceMix1, 0, mixerRight, 1);
AudioConnection patchVoice2ToL(voiceMix2, 0, mixerLeft, 2);
AudioConnection patchVoice2ToR(voiceMix2, 0, mixerRight, 2);
AudioConnection patchVoice3ToL(voiceMix3, 0, mixerLeft, 3);
AudioConnection patchVoice3ToR(voiceMix3, 0, mixerRight, 3);

// Connect voice mixers to synth-only mixers (for reverb)
AudioConnection patchVoice1ToSynthL(voiceMix1, 0, synthOnlyLeft, 0);
AudioConnection patchVoice1ToSynthR(voiceMix1, 0, synthOnlyRight, 0);
AudioConnection patchVoice2ToSynthL(voiceMix2, 0, synthOnlyLeft, 1);
AudioConnection patchVoice2ToSynthR(voiceMix2, 0, synthOnlyRight, 1);
AudioConnection patchVoice3ToSynthL(voiceMix3, 0, synthOnlyLeft, 2);
AudioConnection patchVoice3ToSynthR(voiceMix3, 0, synthOnlyRight, 2);

// Reverb and output
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

float getDiatonicThird(float noteFreq, int keyNote, int mode)
{
    // Convert frequency to MIDI note number
    float midiNote = 12.0f * log2f(noteFreq / 440.0f) + 69.0f;
    int noteClass = ((int)round(midiNote)) % 12; // 0-11 chromatic position

    // Calculate position in scale relative to key
    int relativePosition = (noteClass - keyNote + 12) % 12;

    // Determine the third interval based on selected mode
    int thirdSemitones = 3; // default to minor third

    if (mode == 0)
    {
        // Major mode: diatonic selection based on scale degree
        if (relativePosition == 0 || relativePosition == 5 || relativePosition == 7)
        {
            thirdSemitones = 4; // major third
        }
        else if (relativePosition == 2 || relativePosition == 4 || relativePosition == 9 || relativePosition == 11)
        {
            thirdSemitones = 3; // minor third
        }
    }
    else if (mode == 1)
    {
        // Natural minor: diatonic selection
        if (relativePosition == 3 || relativePosition == 8 || relativePosition == 10)
        {
            thirdSemitones = 4; // major third
        }
        else if (relativePosition == 0 || relativePosition == 2 || relativePosition == 5 || relativePosition == 7)
        {
            thirdSemitones = 3; // minor third
        }
    }
    else if (mode == 2)
    {
        // Fixed Major: always major third
        thirdSemitones = 4;
    }
    else if (mode == 3)
    {
        // Fixed Minor: always minor third
        thirdSemitones = 3;
    }

    return powf(2.0f, thirdSemitones / 12.0f);
}

void stopAllOscillators()
{
    myEffect.amplitude(0);
    myEffect2.amplitude(0);
    myEffect3.amplitude(0);
    myEffectOrg2.amplitude(0);
    myEffectOrg3.amplitude(0);
    myEffect2Org2.amplitude(0);
    myEffect2Org3.amplitude(0);
    myEffect3Org2.amplitude(0);
    myEffect3Org3.amplitude(0);
    myEffectRhodes2.amplitude(0);
    myEffect2Rhodes2.amplitude(0);
    myEffect3Rhodes2.amplitude(0);

    // Restore mixer gains to normal when stopping (in case arp mode left them muted)
    float synthGain = 0.8f;
    mixerLeft.gain(1, synthGain);
    mixerLeft.gain(2, synthGain);
    mixerLeft.gain(3, synthGain);
    mixerRight.gain(1, synthGain);
    mixerRight.gain(2, synthGain);
    mixerRight.gain(3, synthGain);
    synthOnlyLeft.gain(0, 1.0f);
    synthOnlyLeft.gain(1, 1.0f);
    synthOnlyLeft.gain(2, 1.0f);
    synthOnlyRight.gain(0, 1.0f);
    synthOnlyRight.gain(1, 1.0f);
    synthOnlyRight.gain(2, 1.0f);
}

void initSineSound(float tonic, float third, float fifth, float octaveMul, float perVoice)
{
    // Original sine wave sound - single oscillator per voice
    stopAllOscillators();

    // Ensure waveform type is sine (in case it was changed to sawtooth)
    myEffect.begin(WAVEFORM_SINE);
    myEffect2.begin(WAVEFORM_SINE);
    myEffect3.begin(WAVEFORM_SINE);

    myEffect.frequency(tonic * octaveMul);
    myEffect2.frequency(tonic * third * octaveMul);
    myEffect3.frequency(tonic * fifth * octaveMul);

    myEffect.amplitude(perVoice);
    myEffect2.amplitude(perVoice);
    myEffect3.amplitude(perVoice);
}

void initOrganSound(float tonic, float third, float fifth, float octaveMul, float perVoice)
{
    // Organ sound - 3 slightly detuned oscillators per voice
    stopAllOscillators();

    // Set all oscillators to sine waveform
    myEffect.begin(WAVEFORM_SINE);
    myEffect2.begin(WAVEFORM_SINE);
    myEffect3.begin(WAVEFORM_SINE);
    myEffectOrg2.begin(WAVEFORM_SINE);
    myEffectOrg3.begin(WAVEFORM_SINE);
    myEffect2Org2.begin(WAVEFORM_SINE);
    myEffect2Org3.begin(WAVEFORM_SINE);
    myEffect3Org2.begin(WAVEFORM_SINE);
    myEffect3Org3.begin(WAVEFORM_SINE);

    float ampPerOsc = perVoice / 3.0f; // divide amplitude across 3 oscillators

    // Root voice (3 oscillators)
    myEffect.frequency(tonic * octaveMul);
    myEffect.amplitude(ampPerOsc);
    myEffectOrg2.frequency(tonic * octaveMul * organDetune1);
    myEffectOrg2.amplitude(ampPerOsc);
    myEffectOrg3.frequency(tonic * octaveMul * organDetune2);
    myEffectOrg3.amplitude(ampPerOsc);

    // Third voice (3 oscillators)
    myEffect2.frequency(tonic * third * octaveMul);
    myEffect2.amplitude(ampPerOsc);
    myEffect2Org2.frequency(tonic * third * octaveMul * organDetune1);
    myEffect2Org2.amplitude(ampPerOsc);
    myEffect2Org3.frequency(tonic * third * octaveMul * organDetune2);
    myEffect2Org3.amplitude(ampPerOsc);

    // Fifth voice (3 oscillators)
    myEffect3.frequency(tonic * fifth * octaveMul);
    myEffect3.amplitude(ampPerOsc);
    myEffect3Org2.frequency(tonic * fifth * octaveMul * organDetune1);
    myEffect3Org2.amplitude(ampPerOsc);
    myEffect3Org3.frequency(tonic * fifth * octaveMul * organDetune2);
    myEffect3Org3.amplitude(ampPerOsc);

    // store base freqs for vibrato
    organBaseRootFreq = tonic * octaveMul;
    organBaseThirdFreq = tonic * third * octaveMul;
    organBaseFifthFreq = tonic * fifth * octaveMul;

    Serial.print("Organ sound: root=");
    Serial.print(organBaseRootFreq);
    Serial.print("Hz, ampPerOsc=");
    Serial.println(ampPerOsc);
}

void initRhodesSound(float tonic, float third, float fifth, float octaveMul, float perVoice)
{
    // Rhodes sound - 2 oscillators per voice with bell-like character
    // Primary sine + slightly detuned companion with lower amplitude
    stopAllOscillators();

    // Set all oscillators to sine waveform
    myEffect.begin(WAVEFORM_SINE);
    myEffect2.begin(WAVEFORM_SINE);
    myEffect3.begin(WAVEFORM_SINE);
    myEffectRhodes2.begin(WAVEFORM_SINE);
    myEffect2Rhodes2.begin(WAVEFORM_SINE);
    myEffect3Rhodes2.begin(WAVEFORM_SINE);

    float detune = 1.0015f;                // +2.6 cents - subtle bell-like chorus
    float mainAmp = perVoice * 0.65f;      // 65% primary
    float companionAmp = perVoice * 0.35f; // 35% companion

    // Root voice (2 oscillators)
    myEffect.frequency(tonic * octaveMul);
    myEffect.amplitude(mainAmp);
    myEffectRhodes2.frequency(tonic * octaveMul * detune);
    myEffectRhodes2.amplitude(companionAmp);

    // Third voice (2 oscillators)
    myEffect2.frequency(tonic * third * octaveMul);
    myEffect2.amplitude(mainAmp);
    myEffect2Rhodes2.frequency(tonic * third * octaveMul * detune);
    myEffect2Rhodes2.amplitude(companionAmp);

    // Fifth voice (2 oscillators)
    myEffect3.frequency(tonic * fifth * octaveMul);
    myEffect3.amplitude(mainAmp);
    myEffect3Rhodes2.frequency(tonic * fifth * octaveMul * detune);
    myEffect3Rhodes2.amplitude(companionAmp);

    Serial.print("Rhodes sound: root=");
    Serial.print(tonic * octaveMul);
    Serial.print("Hz, main=");
    Serial.print(mainAmp);
    Serial.print(", companion=");
    Serial.println(companionAmp);
}

void initStringsSound(float tonic, float third, float fifth, float octaveMul, float perVoice)
{
    // Strings sound - 2 sawtooth oscillators per voice for rich ensemble
    stopAllOscillators();

    float detune = 1.004f;             // +6.9 cents - wider detune for ensemble
    float ampPerOsc = perVoice / 2.0f; // equal amplitude for both oscillators

    // Root voice (2 sawtooth oscillators)
    myEffect.begin(WAVEFORM_SAWTOOTH);
    myEffect.frequency(tonic * octaveMul);
    myEffect.amplitude(ampPerOsc);
    myEffectRhodes2.begin(WAVEFORM_SAWTOOTH);
    myEffectRhodes2.frequency(tonic * octaveMul * detune);
    myEffectRhodes2.amplitude(ampPerOsc);

    // Third voice (2 sawtooth oscillators)
    myEffect2.begin(WAVEFORM_SAWTOOTH);
    myEffect2.frequency(tonic * third * octaveMul);
    myEffect2.amplitude(ampPerOsc);
    myEffect2Rhodes2.begin(WAVEFORM_SAWTOOTH);
    myEffect2Rhodes2.frequency(tonic * third * octaveMul * detune);
    myEffect2Rhodes2.amplitude(ampPerOsc);

    // Fifth voice (2 sawtooth oscillators)
    myEffect3.begin(WAVEFORM_SAWTOOTH);
    myEffect3.frequency(tonic * fifth * octaveMul);
    myEffect3.amplitude(ampPerOsc);
    myEffect3Rhodes2.begin(WAVEFORM_SAWTOOTH);
    myEffect3Rhodes2.frequency(tonic * fifth * octaveMul * detune);
    myEffect3Rhodes2.amplitude(ampPerOsc);
}

void startChord(float potNorm, float tonicFreq, int keyNote, int mode)
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
    const float third = getDiatonicThird(tonic, keyNote, mode);
    const float fifth = powf(2.0f, 7.0f / 12.0f); // +7 semitones

    // apply octave shift
    float octaveMul = powf(2.0f, (float)currentOctaveShift);

    // distribute amplitude to avoid clipping (sum ~= potNorm)
    float perVoice = potNorm / 3.0f;

    // Initialize sound based on currentSynthSound selection
    if (currentSynthSound == 1) // Organ
    {
        initOrganSound(tonic, third, fifth, octaveMul, perVoice);
    }
    else if (currentSynthSound == 2) // Rhodes
    {
        initRhodesSound(tonic, third, fifth, octaveMul, perVoice);
    }
    else if (currentSynthSound == 3) // Strings
    {
        initStringsSound(tonic, third, fifth, octaveMul, perVoice);
    }
    else // Sine (default)
    {
        initSineSound(tonic, third, fifth, octaveMul, perVoice);
    }

    if (!chordActive)
    {
        Serial.println(">>> CHORD START at tonic " + String(tonic) + "Hz vol " + String(potNorm));
    }
    digitalWrite(LED_BUILTIN, HIGH);

    beepAmp = potNorm;
    chordActive = true;

    // Reset arpeggiator state when starting chord
    arpCurrentStep = 0;

    // Start arp timer if in arp mode
    if (currentArpMode == 0)
    {
        startArpTimer();
    }
}

void updateChordTonic(float tonicFreq, int keyNote, int mode)
{
    if (!chordActive || tonicFreq <= 0.0f)
        return;

    currentChordTonic = tonicFreq;

    // compute triad intervals (diatonic third, perfect fifth)
    const float third = getDiatonicThird(tonicFreq, keyNote, mode);
    const float fifth = powf(2.0f, 7.0f / 12.0f); // +7 semitones

    // apply octave shift
    float octaveMul = powf(2.0f, (float)currentOctaveShift);

    // Update frequencies based on current sound
    if (currentSynthSound == 1) // Organ
    {
        // update base frequencies (vibrato will modulate these)
        organBaseRootFreq = tonicFreq * octaveMul;
        organBaseThirdFreq = tonicFreq * third * octaveMul;
        organBaseFifthFreq = tonicFreq * fifth * octaveMul;

        myEffect.frequency(organBaseRootFreq);
        myEffectOrg2.frequency(organBaseRootFreq * organDetune1);
        myEffectOrg3.frequency(organBaseRootFreq * organDetune2);

        myEffect2.frequency(organBaseThirdFreq);
        myEffect2Org2.frequency(organBaseThirdFreq * organDetune1);
        myEffect2Org3.frequency(organBaseThirdFreq * organDetune2);

        myEffect3.frequency(organBaseFifthFreq);
        myEffect3Org2.frequency(organBaseFifthFreq * organDetune1);
        myEffect3Org3.frequency(organBaseFifthFreq * organDetune2);
    }
    else if (currentSynthSound == 2) // Rhodes
    {
        float detune = 1.0015f;

        myEffect.frequency(tonicFreq * octaveMul);
        myEffectRhodes2.frequency(tonicFreq * octaveMul * detune);

        myEffect2.frequency(tonicFreq * third * octaveMul);
        myEffect2Rhodes2.frequency(tonicFreq * third * octaveMul * detune);

        myEffect3.frequency(tonicFreq * fifth * octaveMul);
        myEffect3Rhodes2.frequency(tonicFreq * fifth * octaveMul * detune);
    }
    else if (currentSynthSound == 3) // Strings
    {
        float detune = 1.004f;

        myEffect.frequency(tonicFreq * octaveMul);
        myEffectRhodes2.frequency(tonicFreq * octaveMul * detune);

        myEffect2.frequency(tonicFreq * third * octaveMul);
        myEffect2Rhodes2.frequency(tonicFreq * third * octaveMul * detune);

        myEffect3.frequency(tonicFreq * fifth * octaveMul);
        myEffect3Rhodes2.frequency(tonicFreq * fifth * octaveMul * detune);
    }
    else // Sine
    {
        myEffect.frequency(tonicFreq * octaveMul);
        myEffect2.frequency(tonicFreq * third * octaveMul);
        myEffect3.frequency(tonicFreq * fifth * octaveMul);
    }

    Serial.println(">>> CHORD UPDATE tonic " + String(tonicFreq) + "Hz");
}

// Periodic vibrato update: called from main loop
void updateVibrato()
{
    // Only apply vibrato when organ is selected, chord is active, and vibrato enabled
    if (!organVibratoEnabled || currentSynthSound != 1 || !chordActive || chordFading)
        return;

    // time in seconds
    float t = (float)micros() / 1000000.0f;
    float lfo = sinf(TWO_PI * organVibratoRate * t);
    float mult = 1.0f + lfo * organVibratoDepth;

    // Apply vibrato to each voice (root, third, fifth) and their detuned companions
    myEffect.frequency(organBaseRootFreq * mult);
    myEffectOrg2.frequency(organBaseRootFreq * mult * organDetune1);
    myEffectOrg3.frequency(organBaseRootFreq * mult * organDetune2);

    myEffect2.frequency(organBaseThirdFreq * mult);
    myEffect2Org2.frequency(organBaseThirdFreq * mult * organDetune1);
    myEffect2Org3.frequency(organBaseThirdFreq * mult * organDetune2);

    myEffect3.frequency(organBaseFifthFreq * mult);
    myEffect3Org2.frequency(organBaseFifthFreq * mult * organDetune1);
    myEffect3Org3.frequency(organBaseFifthFreq * mult * organDetune2);
}

void startRhodesDecay()
{
    if (currentSynthSound != 2 || !chordActive)
        return;

    rhodesDecaying = true;
    rhodesDecayStartMs = millis();
    rhodesDecayStartAmp = beepAmp;
    Serial.println(">>> RHODES DECAY START");
}

void updateRhodesDecay()
{
    if (!rhodesDecaying)
        return;

    unsigned long nowMs = millis();
    unsigned long elapsed = nowMs - rhodesDecayStartMs;

    if (elapsed >= rhodesDecayDurationMs)
    { // Decay complete: silence Rhodes oscillators
        myEffect.amplitude(0);
        myEffectRhodes2.amplitude(0);
        myEffect2.amplitude(0);
        myEffect2Rhodes2.amplitude(0);
        myEffect3.amplitude(0);
        myEffect3Rhodes2.amplitude(0);
        rhodesDecaying = false;
        chordActive = false;
        chordSuppressed = true;
        beepAmp = 0.0f;
        Serial.println(">>> RHODES DECAY COMPLETE");
    }
    else
    {
        // Apply exponential decay curve for natural piano-like envelope
        float t = (float)elapsed / (float)rhodesDecayDurationMs; // 0..1
        float curAmp = rhodesDecayStartAmp * (1.0f - t);

        // Rhodes uses 2 oscillators per voice with 65/35 split
        float perVoice = curAmp / 3.0f;
        float mainAmp = perVoice * 0.65f;
        float companionAmp = perVoice * 0.35f;

        myEffect.amplitude(mainAmp);
        myEffectRhodes2.amplitude(companionAmp);
        myEffect2.amplitude(mainAmp);
        myEffect2Rhodes2.amplitude(companionAmp);
        myEffect3.amplitude(mainAmp);
        myEffect3Rhodes2.amplitude(companionAmp);

        beepAmp = curAmp;
    }
}

void stopChord()
{
    if (!chordActive)
        return;

    // Stop arp timer if running
    if (arpTimerActive)
    {
        stopArpTimer();
    }

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
    stopAllOscillators();
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
            // Apply volume based on current sound
            if (currentSynthSound == 1) // Organ
            {
                float ampPerOsc = potNorm / 9.0f;
                myEffect.amplitude(ampPerOsc);
                myEffectOrg2.amplitude(ampPerOsc);
                myEffectOrg3.amplitude(ampPerOsc);
                myEffect2.amplitude(ampPerOsc);
                myEffect2Org2.amplitude(ampPerOsc);
                myEffect2Org3.amplitude(ampPerOsc);
                myEffect3.amplitude(ampPerOsc);
                myEffect3Org2.amplitude(ampPerOsc);
                myEffect3Org3.amplitude(ampPerOsc);
            }
            else if (currentSynthSound == 2) // Rhodes
            {
                float mainAmp = potNorm / 3.0f * 0.65f;
                float companionAmp = potNorm / 3.0f * 0.35f;
                myEffect.amplitude(mainAmp);
                myEffectRhodes2.amplitude(companionAmp);
                myEffect2.amplitude(mainAmp);
                myEffect2Rhodes2.amplitude(companionAmp);
                myEffect3.amplitude(mainAmp);
                myEffect3Rhodes2.amplitude(companionAmp);
            }
            else if (currentSynthSound == 3) // Strings
            {
                float ampPerOsc = potNorm / 6.0f; // 6 total oscillators
                myEffect.amplitude(ampPerOsc);
                myEffectRhodes2.amplitude(ampPerOsc);
                myEffect2.amplitude(ampPerOsc);
                myEffect2Rhodes2.amplitude(ampPerOsc);
                myEffect3.amplitude(ampPerOsc);
                myEffect3Rhodes2.amplitude(ampPerOsc);
            }
            else // Sine
            {
                float perVoice = potNorm / 3.0f;
                myEffect.amplitude(perVoice);
                myEffect2.amplitude(perVoice);
                myEffect3.amplitude(perVoice);
            }
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
            stopAllOscillators();
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

            // Calculate per-oscillator amplitude based on current sound
            if (currentSynthSound == 1) // Organ
            {
                float ampPerOsc = curAmp / 9.0f; // 9 total oscillators
                myEffect.amplitude(ampPerOsc);
                myEffectOrg2.amplitude(ampPerOsc);
                myEffectOrg3.amplitude(ampPerOsc);
                myEffect2.amplitude(ampPerOsc);
                myEffect2Org2.amplitude(ampPerOsc);
                myEffect2Org3.amplitude(ampPerOsc);
                myEffect3.amplitude(ampPerOsc);
                myEffect3Org2.amplitude(ampPerOsc);
                myEffect3Org3.amplitude(ampPerOsc);
            }
            else if (currentSynthSound == 2) // Rhodes
            {
                float perVoice = curAmp / 3.0f;
                float mainAmp = perVoice * 0.65f;
                float companionAmp = perVoice * 0.35f;
                myEffect.amplitude(mainAmp);
                myEffectRhodes2.amplitude(companionAmp);
                myEffect2.amplitude(mainAmp);
                myEffect2Rhodes2.amplitude(companionAmp);
                myEffect3.amplitude(mainAmp);
                myEffect3Rhodes2.amplitude(companionAmp);
            }
            else if (currentSynthSound == 3) // Strings
            {
                float ampPerOsc = curAmp / 6.0f; // 6 total oscillators
                myEffect.amplitude(ampPerOsc);
                myEffectRhodes2.amplitude(ampPerOsc);
                myEffect2.amplitude(ampPerOsc);
                myEffect2Rhodes2.amplitude(ampPerOsc);
                myEffect3.amplitude(ampPerOsc);
                myEffect3Rhodes2.amplitude(ampPerOsc);
            }
            else // Sine
            {
                float perVoice = curAmp / 3.0f;
                myEffect.amplitude(perVoice);
                myEffect2.amplitude(perVoice);
                myEffect3.amplitude(perVoice);
            }
            beepAmp = curAmp;
        }
    }
}

void setupAudio()
{
    // Allocate audio memory
    AudioMemory(128);
    Serial.println("Audio memory allocated");

    // Initialize audio shield FIRST - only call enable() ONCE
    if (audioShield.enable())
    {
        Serial.println("Audio Shield enabled");
        audioShieldEnabled = true;

        // Configure audio input - use lineIn for external audio sources
        audioShield.inputSelect(AUDIO_INPUT_LINEIN);
        audioShield.lineInLevel(10); // 0-15, where 5 is 1.33Vp-p, 10 is 0.63Vp-p
        Serial.println("Audio input configured: LINE IN, level 10");
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

    // Initialize organ sound oscillators
    myEffectOrg2.begin(WAVEFORM_SINE);
    myEffectOrg2.frequency(1000);
    myEffectOrg2.amplitude(0);

    myEffectOrg3.begin(WAVEFORM_SINE);
    myEffectOrg3.frequency(1000);
    myEffectOrg3.amplitude(0);

    myEffect2Org2.begin(WAVEFORM_SINE);
    myEffect2Org2.frequency(1000);
    myEffect2Org2.amplitude(0);

    myEffect2Org3.begin(WAVEFORM_SINE);
    myEffect2Org3.frequency(1000);
    myEffect2Org3.amplitude(0);

    myEffect3Org2.begin(WAVEFORM_SINE);
    myEffect3Org2.frequency(1000);
    myEffect3Org2.amplitude(0);

    myEffect3Org3.begin(WAVEFORM_SINE);
    myEffect3Org3.frequency(1000);
    myEffect3Org3.amplitude(0);

    // Initialize Rhodes and Strings oscillators
    myEffectRhodes2.begin(WAVEFORM_SINE);
    myEffectRhodes2.frequency(1000);
    myEffectRhodes2.amplitude(0);

    myEffect2Rhodes2.begin(WAVEFORM_SINE);
    myEffect2Rhodes2.frequency(1000);
    myEffect2Rhodes2.amplitude(0);

    myEffect3Rhodes2.begin(WAVEFORM_SINE);
    myEffect3Rhodes2.frequency(1000);
    myEffect3Rhodes2.amplitude(0);

    Serial.println("Waveforms initialized (12 oscillators total for all synth sounds)");

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

    // Configure voice sub-mixers (all gains at 1.0 to pass signals through)
    voiceMix1.gain(0, 1.0f);
    voiceMix1.gain(1, 1.0f);
    voiceMix1.gain(2, 1.0f);
    voiceMix1.gain(3, 1.0f);

    voiceMix2.gain(0, 1.0f);
    voiceMix2.gain(1, 1.0f);
    voiceMix2.gain(2, 1.0f);
    voiceMix2.gain(3, 1.0f);

    voiceMix3.gain(0, 1.0f);
    voiceMix3.gain(1, 1.0f);
    voiceMix3.gain(2, 1.0f);
    voiceMix3.gain(3, 1.0f);

    Serial.println("Voice sub-mixers configured");

    // Startup beep
    Serial.println("Playing startup beep 100ms @ 0.7");
    myEffect.frequency(1000);
    myEffect.amplitude(0.7);
    delay(100);
    myEffect.amplitude(0);
    Serial.println("Startup beep complete");
}

// Arpeggiator timer ISR - called at precise intervals for stable timing
void arpTimerISR()
{
    // Only process if chord is active and not fading
    if (!chordActive || chordFading)
        return;

    // Mute all voices first
    mixerLeft.gain(1, 0.0f); // mute root
    mixerLeft.gain(2, 0.0f); // mute third
    mixerLeft.gain(3, 0.0f); // mute fifth
    mixerRight.gain(1, 0.0f);
    mixerRight.gain(2, 0.0f);
    mixerRight.gain(3, 0.0f);

    synthOnlyLeft.gain(0, 0.0f); // mute root
    synthOnlyLeft.gain(1, 0.0f); // mute third
    synthOnlyLeft.gain(2, 0.0f); // mute fifth
    synthOnlyRight.gain(0, 0.0f);
    synthOnlyRight.gain(1, 0.0f);
    synthOnlyRight.gain(2, 0.0f);

    // Unmute only the current step voice
    float synthGain = 0.8f;
    switch (arpCurrentStep)
    {
    case 0: // Root voice
        mixerLeft.gain(1, synthGain);
        mixerRight.gain(1, synthGain);
        synthOnlyLeft.gain(0, 1.0f);
        synthOnlyRight.gain(0, 1.0f);
        break;
    case 1: // Third voice
        mixerLeft.gain(2, synthGain);
        mixerRight.gain(2, synthGain);
        synthOnlyLeft.gain(1, 1.0f);
        synthOnlyRight.gain(1, 1.0f);
        break;
    case 2: // Fifth voice
        mixerLeft.gain(3, synthGain);
        mixerRight.gain(3, synthGain);
        synthOnlyLeft.gain(2, 1.0f);
        synthOnlyRight.gain(2, 1.0f);
        break;
    }

    // Advance to next step
    arpCurrentStep++;
    if (arpCurrentStep > 2)
    {
        arpCurrentStep = 0;
    }
}

// Start the arpeggiator timer
void startArpTimer()
{
    if (arpTimerActive)
        return;

    // Convert milliseconds to microseconds for IntervalTimer
    unsigned long intervalUs = arpStepDurationMs * 1000;
    arpTimer.begin(arpTimerISR, intervalUs);
    arpTimerActive = true;

    // Immediately apply the first step
    arpTimerISR();

    Serial.println("Arp timer started");
}

// Stop the arpeggiator timer
void stopArpTimer()
{
    if (!arpTimerActive)
        return;

    arpTimer.end();
    arpTimerActive = false;

    // Restore normal voice levels when stopping
    float synthGain = 0.8f;
    mixerLeft.gain(1, synthGain);
    mixerLeft.gain(2, synthGain);
    mixerLeft.gain(3, synthGain);
    mixerRight.gain(1, synthGain);
    mixerRight.gain(2, synthGain);
    mixerRight.gain(3, synthGain);
    synthOnlyLeft.gain(0, 1.0f);
    synthOnlyLeft.gain(1, 1.0f);
    synthOnlyLeft.gain(2, 1.0f);
    synthOnlyRight.gain(0, 1.0f);
    synthOnlyRight.gain(1, 1.0f);
    synthOnlyRight.gain(2, 1.0f);

    Serial.println("Arp timer stopped");
}

// Update the arpeggiator timer interval (call when tempo changes)
void updateArpTimerInterval()
{
    if (!arpTimerActive)
        return;

    // Stop and restart with new interval
    arpTimer.end();
    unsigned long intervalUs = arpStepDurationMs * 1000;
    arpTimer.begin(arpTimerISR, intervalUs);

    Serial.print("Arp timer interval updated to ");
    Serial.print(arpStepDurationMs);
    Serial.println(" ms");
}

void updateArpeggiator()
{
    // This function is now called from main loop primarily for mode management,
    // not timing. The actual timing is handled by arpTimerISR.

    // If chord is not active or is fading, ensure timer is stopped
    if (!chordActive || chordFading)
    {
        if (arpTimerActive)
        {
            stopArpTimer();
        }
        return;
    }

    // Handle mode transitions
    if (currentArpMode == 0) // Arp mode
    {
        // Start timer if not already running
        if (!arpTimerActive)
        {
            arpCurrentStep = 0;
            startArpTimer();
        }
    }
    else // Poly mode (1)
    {
        // Stop timer if running and restore normal voice levels
        if (arpTimerActive)
        {
            stopArpTimer();
        }

        // Ensure all voices are on for poly mode
        float synthGain = 0.8f;
        mixerLeft.gain(1, synthGain);
        mixerLeft.gain(2, synthGain);
        mixerLeft.gain(3, synthGain);
        mixerRight.gain(1, synthGain);
        mixerRight.gain(2, synthGain);
        mixerRight.gain(3, synthGain);
        synthOnlyLeft.gain(0, 1.0f);
        synthOnlyLeft.gain(1, 1.0f);
        synthOnlyLeft.gain(2, 1.0f);
        synthOnlyRight.gain(0, 1.0f);
        synthOnlyRight.gain(1, 1.0f);
        synthOnlyRight.gain(2, 1.0f);
    }
}
