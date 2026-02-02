#include "audio.h"
#include "pitch.h"
#include "NVRAM.h"

// Define audio objects - Simplified: 2 oscillators per voice (primary + detuned)
// Voice 1 (root): myEffect + myEffect1b
// Voice 2 (third): myEffect2 + myEffect2b
// Voice 3 (fifth): myEffect3 + myEffect3b
AudioSynthWaveform myEffect;   // root voice primary
AudioSynthWaveform myEffect1b; // root voice detuned
AudioSynthWaveform myEffect2;  // third voice primary
AudioSynthWaveform myEffect2b; // third voice detuned
AudioSynthWaveform myEffect3;  // fifth voice primary
AudioSynthWaveform myEffect3b; // fifth voice detuned

AudioInputI2S audioInput;   // Audio shield input
AudioOutputI2S audioOutput; // Audio shield output
AudioMixer4 mixerLeft;      // Mix input + synth for left channel
AudioMixer4 mixerRight;     // Mix input + synth for right channel
AudioAnalyzePeak peak1;     // Input peak detection for test mode

// Reverb + wet/dry mixers
AudioEffectFreeverb reverb; // stereo freeverb
AudioMixer4 wetDryLeft;     // mix dry (mixerLeft) + wet (reverb L)
AudioMixer4 wetDryRight;    // mix dry (mixerRight) + wet (reverb R)

// Synth voice mixer (combines oscillators for reverb input)
AudioMixer4 synthMix; // combines all synth oscillators for reverb

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

// Detune ratios used by different sounds
float organDetune = 1.002f;   // +~3.5 cents for organ
float rhodesDetune = 1.0015f; // +2.6 cents for rhodes
float stringsDetune = 1.004f; // +6.9 cents for strings

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

// Connect primary oscillators directly to main mixers (ch1, ch2, ch3)
AudioConnection patchOsc1ToL(myEffect, 0, mixerLeft, 1);
AudioConnection patchOsc1ToR(myEffect, 0, mixerRight, 1);
AudioConnection patchOsc2ToL(myEffect2, 0, mixerLeft, 2);
AudioConnection patchOsc2ToR(myEffect2, 0, mixerRight, 2);
AudioConnection patchOsc3ToL(myEffect3, 0, mixerLeft, 3);
AudioConnection patchOsc3ToR(myEffect3, 0, mixerRight, 3);

// Connect all oscillators to synth mixer for reverb input
AudioConnection patchSynth1(myEffect, 0, synthMix, 0);
AudioConnection patchSynth2(myEffect2, 0, synthMix, 1);
AudioConnection patchSynth3(myEffect3, 0, synthMix, 2);
// Note: detuned oscillators (myEffect1b, myEffect2b, myEffect3b) are summed
// with primary oscillators at the amplitude level, so no separate routing needed

// Reverb and output
AudioConnection patchReverbIn(synthMix, 0, reverb, 0);
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

// Determine fifth interval ratio.
// Use flat-5 (tritone, +6 semitones) when building a chord on the 7th scale degree
// in Major (mode==0) or Natural Minor (mode==1).
float getDiatonicFifth(float noteFreq, int keyNote, int mode)
{
    int fifthSemitones = 7; // perfect fifth by default

    // Major or Natural Minor: use diminished fifth for 7th scale degree
    if (mode == 0 || mode == 1)
    {
        // Determine the scale degree of the detected note
        float midiNote = 12.0f * log2f(noteFreq / 440.0f) + 69.0f;
        int noteClass = ((int)round(midiNote)) % 12; // 0-11 chromatic position
        int relativePosition = (noteClass - keyNote + 12) % 12;
        
        if (mode == 0) // Major mode
        {
            // 7th scale degree in major is 11 semitones above root (e.g., D# in E major)
            if (relativePosition == 11)
            {
                Serial.println("Using flat-5 for 7th degree (diminished fifth)");
                fifthSemitones = 6; // flat-5
            }
        }
        else if (mode == 1) // Natural minor mode
        {
            // 7th scale degree in natural minor is 10 semitones above root
            if (relativePosition == 10)
            {
                Serial.println("Using flat-5 for 7th degree (diminished fifth)");
                fifthSemitones = 6; // flat-5
            }
        }
    }
    return powf(2.0f, fifthSemitones / 12.0f);
}

void restoreMixerGains(float synthGain)
{
    // Restore mixer gains respecting the current output mode
    // In split mode: Left = guitar only, Right = synth only
    // In mix mode: Both channels have guitar + synth
    
    if (currentOutputMode == 1) // Split mode
    {
        // Left channel: guitar only (no synth)
        mixerLeft.gain(1, 0.0f);
        mixerLeft.gain(2, 0.0f);
        mixerLeft.gain(3, 0.0f);
        
        // Right channel: synth only
        mixerRight.gain(1, synthGain);
        mixerRight.gain(2, synthGain);
        mixerRight.gain(3, synthGain);
    }
    else // Mix mode
    {
        // Both channels: synth
        mixerLeft.gain(1, synthGain);
        mixerLeft.gain(2, synthGain);
        mixerLeft.gain(3, synthGain);
        mixerRight.gain(1, synthGain);
        mixerRight.gain(2, synthGain);
        mixerRight.gain(3, synthGain);
    }
}

void stopAllOscillators()
{
    myEffect.amplitude(0);
    myEffect1b.amplitude(0);
    myEffect2.amplitude(0);
    myEffect2b.amplitude(0);
    myEffect3.amplitude(0);
    myEffect3b.amplitude(0);

    // Restore mixer gains to normal when stopping (in case arp mode left them muted)
    restoreMixerGains(0.8f);
}

void initSineSound(float tonic, float third, float fifth, float octaveMul, float perVoice)
{
    // Sine wave sound - single oscillator per voice (detuned oscillators silent)
    stopAllOscillators();

    myEffect.begin(WAVEFORM_SINE);
    myEffect2.begin(WAVEFORM_SINE);
    myEffect3.begin(WAVEFORM_SINE);

    myEffect.frequency(tonic * octaveMul);
    myEffect2.frequency(tonic * third * octaveMul);
    myEffect3.frequency(tonic * fifth * octaveMul);

    myEffect.amplitude(perVoice);
    myEffect2.amplitude(perVoice);
    myEffect3.amplitude(perVoice);
    // Detuned oscillators stay at 0 amplitude for pure sine
}

void initOrganSound(float tonic, float third, float fifth, float octaveMul, float perVoice)
{
    // Organ sound - 2 slightly detuned oscillators per voice
    stopAllOscillators();

    myEffect.begin(WAVEFORM_SINE);
    myEffect1b.begin(WAVEFORM_SINE);
    myEffect2.begin(WAVEFORM_SINE);
    myEffect2b.begin(WAVEFORM_SINE);
    myEffect3.begin(WAVEFORM_SINE);
    myEffect3b.begin(WAVEFORM_SINE);

    float ampPerOsc = perVoice / 2.0f; // divide amplitude across 2 oscillators per voice

    // Root voice
    myEffect.frequency(tonic * octaveMul);
    myEffect.amplitude(ampPerOsc);
    myEffect1b.frequency(tonic * octaveMul * organDetune);
    myEffect1b.amplitude(ampPerOsc);

    // Third voice
    myEffect2.frequency(tonic * third * octaveMul);
    myEffect2.amplitude(ampPerOsc);
    myEffect2b.frequency(tonic * third * octaveMul * organDetune);
    myEffect2b.amplitude(ampPerOsc);

    // Fifth voice
    myEffect3.frequency(tonic * fifth * octaveMul);
    myEffect3.amplitude(ampPerOsc);
    myEffect3b.frequency(tonic * fifth * octaveMul * organDetune);
    myEffect3b.amplitude(ampPerOsc);

    // Store base freqs for vibrato
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
    stopAllOscillators();

    myEffect.begin(WAVEFORM_SINE);
    myEffect1b.begin(WAVEFORM_SINE);
    myEffect2.begin(WAVEFORM_SINE);
    myEffect2b.begin(WAVEFORM_SINE);
    myEffect3.begin(WAVEFORM_SINE);
    myEffect3b.begin(WAVEFORM_SINE);

    float mainAmp = perVoice * 0.65f;      // 65% primary
    float companionAmp = perVoice * 0.35f; // 35% companion

    // Root voice
    myEffect.frequency(tonic * octaveMul);
    myEffect.amplitude(mainAmp);
    myEffect1b.frequency(tonic * octaveMul * rhodesDetune);
    myEffect1b.amplitude(companionAmp);

    // Third voice
    myEffect2.frequency(tonic * third * octaveMul);
    myEffect2.amplitude(mainAmp);
    myEffect2b.frequency(tonic * third * octaveMul * rhodesDetune);
    myEffect2b.amplitude(companionAmp);

    // Fifth voice
    myEffect3.frequency(tonic * fifth * octaveMul);
    myEffect3.amplitude(mainAmp);
    myEffect3b.frequency(tonic * fifth * octaveMul * rhodesDetune);
    myEffect3b.amplitude(companionAmp);

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

    float ampPerOsc = perVoice / 2.0f; // equal amplitude for both oscillators

    // Root voice
    myEffect.begin(WAVEFORM_SAWTOOTH);
    myEffect.frequency(tonic * octaveMul);
    myEffect.amplitude(ampPerOsc);
    myEffect1b.begin(WAVEFORM_SAWTOOTH);
    myEffect1b.frequency(tonic * octaveMul * stringsDetune);
    myEffect1b.amplitude(ampPerOsc);

    // Third voice
    myEffect2.begin(WAVEFORM_SAWTOOTH);
    myEffect2.frequency(tonic * third * octaveMul);
    myEffect2.amplitude(ampPerOsc);
    myEffect2b.begin(WAVEFORM_SAWTOOTH);
    myEffect2b.frequency(tonic * third * octaveMul * stringsDetune);
    myEffect2b.amplitude(ampPerOsc);

    // Fifth voice
    myEffect3.begin(WAVEFORM_SAWTOOTH);
    myEffect3.frequency(tonic * fifth * octaveMul);
    myEffect3.amplitude(ampPerOsc);
    myEffect3b.begin(WAVEFORM_SAWTOOTH);
    myEffect3b.frequency(tonic * fifth * octaveMul * stringsDetune);
    myEffect3b.amplitude(ampPerOsc);
}

void startChord(float potNorm, float tonicFreq, int keyNote, int mode)
{
    // choose tonic: passed in or last detected
    float tonic = (tonicFreq > 1.0f) ? tonicFreq : lastDetectedFrequency;
    bool hasValidPitch = (tonic > 0.0f);
    
    if (tonic <= 0.0f)
        tonic = 440.0f; // fallback to A4 for frequency calculation only

    currentChordTonic = tonic;
    // clear suppression when starting chord explicitly
    chordSuppressed = false;
    // cancel any fade in progress
    chordFading = false;

    // compute triad intervals (diatonic third, diatonic fifth)
    const float third = getDiatonicThird(tonic, keyNote, mode);
    const float fifth = getDiatonicFifth(tonic, keyNote, mode);

    // apply octave shift
    float octaveMul = powf(2.0f, (float)currentOctaveShift);

    // If no valid pitch detected yet, start silent (amplitude will be set when pitch is detected)
    float perVoice = hasValidPitch ? (potNorm / 3.0f) : 0.0f;

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
        if (hasValidPitch)
        {
            Serial.println(">>> CHORD START at tonic " + String(tonic) + "Hz vol " + String(potNorm));
        }
        else
        {
            Serial.println(">>> CHORD START (waiting for pitch detection)");
        }
    }
    digitalWrite(LED_BUILTIN, HIGH);

    beepAmp = hasValidPitch ? potNorm : 0.0f;
    chordActive = true;

    // Reset arpeggiator state when starting chord
    arpCurrentStep = 0;

    // Start arp timer if in arp mode (only if we have valid pitch)
    if (currentArpMode == 0 && hasValidPitch)
    {
        startArpTimer();
    }
}

void updateChordTonic(float tonicFreq, int keyNote, int mode)
{
    if (!chordActive || tonicFreq <= 0.0f)
        return;

    // Check if we're transitioning from silent start to having valid pitch
    bool wasWaitingForPitch = (beepAmp <= 0.0f);

    currentChordTonic = tonicFreq;

    // compute triad intervals (diatonic third, diatonic fifth)
    const float third = getDiatonicThird(tonicFreq, keyNote, mode);
    const float fifth = getDiatonicFifth(tonicFreq, keyNote, mode);

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
        myEffect1b.frequency(organBaseRootFreq * organDetune);
        myEffect2.frequency(organBaseThirdFreq);
        myEffect2b.frequency(organBaseThirdFreq * organDetune);
        myEffect3.frequency(organBaseFifthFreq);
        myEffect3b.frequency(organBaseFifthFreq * organDetune);
    }
    else if (currentSynthSound == 2) // Rhodes
    {
        myEffect.frequency(tonicFreq * octaveMul);
        myEffect1b.frequency(tonicFreq * octaveMul * rhodesDetune);
        myEffect2.frequency(tonicFreq * third * octaveMul);
        myEffect2b.frequency(tonicFreq * third * octaveMul * rhodesDetune);
        myEffect3.frequency(tonicFreq * fifth * octaveMul);
        myEffect3b.frequency(tonicFreq * fifth * octaveMul * rhodesDetune);
    }
    else if (currentSynthSound == 3) // Strings
    {
        myEffect.frequency(tonicFreq * octaveMul);
        myEffect1b.frequency(tonicFreq * octaveMul * stringsDetune);
        myEffect2.frequency(tonicFreq * third * octaveMul);
        myEffect2b.frequency(tonicFreq * third * octaveMul * stringsDetune);
        myEffect3.frequency(tonicFreq * fifth * octaveMul);
        myEffect3b.frequency(tonicFreq * fifth * octaveMul * stringsDetune);
    }
    else // Sine
    {
        myEffect.frequency(tonicFreq * octaveMul);
        myEffect2.frequency(tonicFreq * third * octaveMul);
        myEffect3.frequency(tonicFreq * fifth * octaveMul);
    }

    // If we were waiting for pitch detection and now have it, start arpeggiator if needed
    if (wasWaitingForPitch)
    {
        Serial.println(">>> PITCH DETECTED, chord now active at " + String(tonicFreq) + "Hz");
        // Arpeggiator will be started by updateArpeggiator() in main loop if in arp mode
    }
    else
    {
        Serial.println(">>> CHORD UPDATE tonic " + String(tonicFreq) + "Hz");
    }
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

    // Apply vibrato to each voice and their detuned companions
    myEffect.frequency(organBaseRootFreq * mult);
    myEffect1b.frequency(organBaseRootFreq * mult * organDetune);
    myEffect2.frequency(organBaseThirdFreq * mult);
    myEffect2b.frequency(organBaseThirdFreq * mult * organDetune);
    myEffect3.frequency(organBaseFifthFreq * mult);
    myEffect3b.frequency(organBaseFifthFreq * mult * organDetune);
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
    {
        // Decay complete: silence oscillators
        stopAllOscillators();
        rhodesDecaying = false;
        chordActive = false;
        chordSuppressed = true;
        beepAmp = 0.0f;
        Serial.println(">>> RHODES DECAY COMPLETE");
    }
    else
    {
        // Apply linear decay curve
        float t = (float)elapsed / (float)rhodesDecayDurationMs; // 0..1
        float curAmp = rhodesDecayStartAmp * (1.0f - t);

        float perVoice = curAmp / 3.0f;
        float mainAmp = perVoice * 0.65f;
        float companionAmp = perVoice * 0.35f;

        myEffect.amplitude(mainAmp);
        myEffect1b.amplitude(companionAmp);
        myEffect2.amplitude(mainAmp);
        myEffect2b.amplitude(companionAmp);
        myEffect3.amplitude(mainAmp);
        myEffect3b.amplitude(companionAmp);

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
        if (!chordFading)
        {
            // Apply volume based on current sound
            if (currentSynthSound == 1) // Organ
            {
                float ampPerOsc = potNorm / 6.0f; // 6 total oscillators
                myEffect.amplitude(ampPerOsc);
                myEffect1b.amplitude(ampPerOsc);
                myEffect2.amplitude(ampPerOsc);
                myEffect2b.amplitude(ampPerOsc);
                myEffect3.amplitude(ampPerOsc);
                myEffect3b.amplitude(ampPerOsc);
            }
            else if (currentSynthSound == 2) // Rhodes
            {
                float mainAmp = potNorm / 3.0f * 0.65f;
                float companionAmp = potNorm / 3.0f * 0.35f;
                myEffect.amplitude(mainAmp);
                myEffect1b.amplitude(companionAmp);
                myEffect2.amplitude(mainAmp);
                myEffect2b.amplitude(companionAmp);
                myEffect3.amplitude(mainAmp);
                myEffect3b.amplitude(companionAmp);
            }
            else if (currentSynthSound == 3) // Strings
            {
                float ampPerOsc = potNorm / 6.0f; // 6 total oscillators
                myEffect.amplitude(ampPerOsc);
                myEffect1b.amplitude(ampPerOsc);
                myEffect2.amplitude(ampPerOsc);
                myEffect2b.amplitude(ampPerOsc);
                myEffect3.amplitude(ampPerOsc);
                myEffect3b.amplitude(ampPerOsc);
            }
            else // Sine
            {
                float perVoice = potNorm / 3.0f;
                myEffect.amplitude(perVoice);
                myEffect2.amplitude(perVoice);
                myEffect3.amplitude(perVoice);
            }
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
            // Fade complete
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
                float ampPerOsc = curAmp / 6.0f;
                myEffect.amplitude(ampPerOsc);
                myEffect1b.amplitude(ampPerOsc);
                myEffect2.amplitude(ampPerOsc);
                myEffect2b.amplitude(ampPerOsc);
                myEffect3.amplitude(ampPerOsc);
                myEffect3b.amplitude(ampPerOsc);
            }
            else if (currentSynthSound == 2) // Rhodes
            {
                float perVoice = curAmp / 3.0f;
                float mainAmp = perVoice * 0.65f;
                float companionAmp = perVoice * 0.35f;
                myEffect.amplitude(mainAmp);
                myEffect1b.amplitude(companionAmp);
                myEffect2.amplitude(mainAmp);
                myEffect2b.amplitude(companionAmp);
                myEffect3.amplitude(mainAmp);
                myEffect3b.amplitude(companionAmp);
            }
            else if (currentSynthSound == 3) // Strings
            {
                float ampPerOsc = curAmp / 6.0f;
                myEffect.amplitude(ampPerOsc);
                myEffect1b.amplitude(ampPerOsc);
                myEffect2.amplitude(ampPerOsc);
                myEffect2b.amplitude(ampPerOsc);
                myEffect3.amplitude(ampPerOsc);
                myEffect3b.amplitude(ampPerOsc);
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
    AudioMemory(64); // Reduced from 128 since we have fewer objects
    Serial.println("Audio memory allocated");

    // Initialize audio shield
    if (audioShield.enable())
    {
        Serial.println("Audio Shield enabled");
        audioShieldEnabled = true;

        audioShield.inputSelect(AUDIO_INPUT_LINEIN);
        audioShield.lineInLevel(10);
        Serial.println("Audio input configured: LINE IN, level 10");
    }
    else
    {
        Serial.println("Audio Shield FAILED");
        audioShieldEnabled = false;
    }

    audioShield.volume(0.5);
    Serial.println("Init Beep @ 0.5 volume");

    // Initialize all 6 oscillators
    myEffect.begin(WAVEFORM_SINE);
    myEffect.frequency(1000);
    myEffect.amplitude(0);

    myEffect1b.begin(WAVEFORM_SINE);
    myEffect1b.frequency(1000);
    myEffect1b.amplitude(0);

    myEffect2.begin(WAVEFORM_SINE);
    myEffect2.frequency(1000);
    myEffect2.amplitude(0);

    myEffect2b.begin(WAVEFORM_SINE);
    myEffect2b.frequency(1000);
    myEffect2b.amplitude(0);

    myEffect3.begin(WAVEFORM_SINE);
    myEffect3.frequency(1000);
    myEffect3.amplitude(0);

    myEffect3b.begin(WAVEFORM_SINE);
    myEffect3b.frequency(1000);
    myEffect3b.amplitude(0);

    Serial.println("Waveforms initialized (6 oscillators total)");

    // Configure mixers
#define BOOST_INPUT_GAIN false
    float inputGain = BOOST_INPUT_GAIN ? 1.5 : 1.0;
    float synthGain = 0.2;

    mixerLeft.gain(0, inputGain);  // input left
    mixerLeft.gain(1, synthGain);  // synth root
    mixerLeft.gain(2, synthGain);  // synth 3rd
    mixerLeft.gain(3, synthGain);  // synth 5th

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

    // Configure synth mixer (for reverb input)
    synthMix.gain(0, 1.0f);
    synthMix.gain(1, 1.0f);
    synthMix.gain(2, 1.0f);
    synthMix.gain(3, 0.0f); // unused

    Serial.println("Synth mixer configured");

    // Apply output mode (Mix vs Split) from NVRAM
    applyOutputMode();

    // Startup beep
    Serial.println("Playing startup beep 100ms @ 0.7");
    myEffect.frequency(1000);
    myEffect.amplitude(0.7);
    delay(100);
    myEffect.amplitude(0);
    Serial.println("Startup beep complete");
}

// Arpeggiator timer ISR
void arpTimerISR()
{
    if (!chordActive || chordFading)
        return;

    // Mute all synth voices first
    // Note: In split mode, only right channel has synth (left has guitar)
    mixerLeft.gain(1, 0.0f);
    mixerLeft.gain(2, 0.0f);
    mixerLeft.gain(3, 0.0f);
    mixerRight.gain(1, 0.0f);
    mixerRight.gain(2, 0.0f);
    mixerRight.gain(3, 0.0f);

    // Unmute only the current step voice
    float synthGain = 0.8f;
    
    // In split mode, synth is only on right channel
    // In mix mode, synth is on both channels
    switch (arpCurrentStep)
    {
    case 0: // Root voice
        if (currentOutputMode == 0) // Mix mode
            mixerLeft.gain(1, synthGain);
        mixerRight.gain(1, synthGain);
        break;
    case 1: // Third voice
        if (currentOutputMode == 0) // Mix mode
            mixerLeft.gain(2, synthGain);
        mixerRight.gain(2, synthGain);
        break;
    case 2: // Fifth voice
        if (currentOutputMode == 0) // Mix mode
            mixerLeft.gain(3, synthGain);
        mixerRight.gain(3, synthGain);
        break;
    }

    // Advance to next step
    arpCurrentStep++;
    if (arpCurrentStep > 2)
    {
        arpCurrentStep = 0;
    }
}

void startArpTimer()
{
    if (arpTimerActive)
        return;

    unsigned long intervalUs = arpStepDurationMs * 1000;
    arpTimer.begin(arpTimerISR, intervalUs);
    arpTimerActive = true;

    // Immediately apply the first step
    arpTimerISR();

    Serial.println("Arp timer started");
}

void stopArpTimer()
{
    if (!arpTimerActive)
        return;

    arpTimer.end();
    arpTimerActive = false;

    // Restore normal voice levels
    restoreMixerGains(0.8f);

    Serial.println("Arp timer stopped");
}

void updateArpTimerInterval()
{
    if (!arpTimerActive)
        return;

    arpTimer.end();
    unsigned long intervalUs = arpStepDurationMs * 1000;
    arpTimer.begin(arpTimerISR, intervalUs);

    Serial.print("Arp timer interval updated to ");
    Serial.print(arpStepDurationMs);
    Serial.println(" ms");
}

void updateArpeggiator()
{
    if (!chordActive || chordFading)
    {
        if (arpTimerActive)
        {
            stopArpTimer();
        }
        return;
    }

    if (currentArpMode == 0) // Arp mode
    {
        if (!arpTimerActive)
        {
            arpCurrentStep = 0;
            startArpTimer();
        }
    }
    else // Poly mode
    {
        if (arpTimerActive)
        {
            stopArpTimer();
        }

        // Ensure all voices are on for poly mode
        restoreMixerGains(0.8f);
    }
}

void applyOutputMode()
{
    // Output modes:
    // 0 = Mix: Guitar + Synth on both L and R (default)
    // 1 = Split: Guitar only on L (no synth), Synth only on R (no guitar)
    
#define BOOST_INPUT_GAIN false
    float inputGain = BOOST_INPUT_GAIN ? 1.5 : 1.0;
    float synthGain = 0.2;
    
    if (currentOutputMode == 1) // Split mode
    {
        // Left channel: guitar only (no synth)
        mixerLeft.gain(0, inputGain);  // guitar input
        mixerLeft.gain(1, 0.0f);       // synth root - off
        mixerLeft.gain(2, 0.0f);       // synth 3rd - off
        mixerLeft.gain(3, 0.0f);       // synth 5th - off
        
        // Right channel: synth only (no guitar)
        mixerRight.gain(0, 0.0f);       // guitar input - off
        mixerRight.gain(1, synthGain);  // synth root
        mixerRight.gain(2, synthGain);  // synth 3rd
        mixerRight.gain(3, synthGain);  // synth 5th
        
        Serial.println("Output mode: SPLIT (L=guitar, R=synth)");
    }
    else // Mix mode (default)
    {
        // Both channels: guitar + synth mixed together
        mixerLeft.gain(0, inputGain);  // guitar input
        mixerLeft.gain(1, synthGain);  // synth root
        mixerLeft.gain(2, synthGain);  // synth 3rd
        mixerLeft.gain(3, synthGain);  // synth 5th
        
        mixerRight.gain(0, inputGain); // guitar input
        mixerRight.gain(1, synthGain); // synth root
        mixerRight.gain(2, synthGain); // synth 3rd
        mixerRight.gain(3, synthGain); // synth 5th
        
        Serial.println("Output mode: MIX (L+R=guitar+synth)");
    }
}
