#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include <Audio.h>

// Audio objects - Simplified: 2 oscillators per voice (primary + detuned)
extern AudioSynthWaveform myEffect;   // root voice primary
extern AudioSynthWaveform myEffect1b; // root voice detuned
extern AudioSynthWaveform myEffect2;  // third voice primary
extern AudioSynthWaveform myEffect2b; // third voice detuned
extern AudioSynthWaveform myEffect3;  // fifth voice primary
extern AudioSynthWaveform myEffect3b; // fifth voice detuned

extern AudioInputI2S audioInput;
extern AudioOutputI2S audioOutput;
extern AudioMixer4 mixerLeft;
extern AudioMixer4 mixerRight;
extern AudioAnalyzePeak peak1;
extern AudioEffectFreeverb reverb;
extern AudioMixer4 wetDryLeft;
extern AudioMixer4 wetDryRight;
extern AudioMixer4 synthMix; // combines synth oscillators for reverb
extern AudioControlSGTL5000 audioShield;

// Audio state
extern bool audioShieldEnabled;
extern float reverbWet;
extern bool chordActive;
extern bool chordSuppressed;
extern float currentChordTonic;
extern bool chordFading;
extern unsigned long chordFadeStartMs;
extern unsigned long chordFadeDurationMs;
extern float chordFadeStartAmp;
extern float beepAmp;
// Organ vibrato control
extern bool organVibratoEnabled;
extern float organVibratoRate;  // Hz
extern float organVibratoDepth; // fractional depth (e.g., 0.015 = ±1.5%)
// Rhodes decay control
extern bool rhodesDecaying;
extern unsigned long rhodesDecayStartMs;
extern unsigned long rhodesDecayDurationMs;
extern float rhodesDecayStartAmp;
// Arpeggiator control
extern volatile int currentArpMode;              // 0=Arp, 1=Poly
extern volatile int arpCurrentStep;              // 0=root, 1=third, 2=fifth
extern volatile unsigned long arpStepDurationMs; // 125ms for eighth notes at 120 BPM (updated by tempo)
extern volatile bool arpTimerActive;             // True when arp timer is running
extern float globalTempoBPM;                     // Global tempo for arpeggiator

// Audio functions
void setupAudio();
void setReverbWet(float wet);
// mode: 0=Major, 1=Minor, 2=Fixed Major, 3=Fixed Minor
float getDiatonicThird(float noteFreq, int keyNote, int mode);
void stopAllOscillators();
void initSineSound(float tonic, float third, float fifth, float octaveMul, float perVoice);
void initOrganSound(float tonic, float third, float fifth, float octaveMul, float perVoice);
void initRhodesSound(float tonic, float third, float fifth, float octaveMul, float perVoice);
void initStringsSound(float tonic, float third, float fifth, float octaveMul, float perVoice);
void startChord(float potNorm, float tonicFreq, int keyNote, int mode);
void updateChordTonic(float tonicFreq, int keyNote, int mode);
void stopChord();
void updateChordVolume(float potNorm);
void updateChordFade();
void updateVibrato();
void startRhodesDecay();
void updateRhodesDecay();
void updateArpeggiator();
void startArpTimer();
void stopArpTimer();
void updateArpTimerInterval();
void applyOutputMode();

#endif // AUDIO_H
