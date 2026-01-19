#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include <Audio.h>

// Audio objects
extern AudioSynthWaveform myEffect;
extern AudioSynthWaveform myEffect2;
extern AudioSynthWaveform myEffect3;
// Additional oscillators for organ sound (3 per voice)
extern AudioSynthWaveform myEffectOrg2;
extern AudioSynthWaveform myEffectOrg3;
extern AudioSynthWaveform myEffect2Org2;
extern AudioSynthWaveform myEffect2Org3;
extern AudioSynthWaveform myEffect3Org2;
extern AudioSynthWaveform myEffect3Org3;
// Additional oscillators for Rhodes and Strings (2 per voice)
extern AudioSynthWaveform myEffectRhodes2;
extern AudioSynthWaveform myEffect2Rhodes2;
extern AudioSynthWaveform myEffect3Rhodes2;
extern AudioInputI2S audioInput;
extern AudioOutputI2S audioOutput;
extern AudioMixer4 mixerLeft;
extern AudioMixer4 mixerRight;
extern AudioAnalyzePeak peak1;
extern AudioEffectFreeverb reverb;
extern AudioMixer4 wetDryLeft;
extern AudioMixer4 wetDryRight;
extern AudioMixer4 synthOnlyLeft;
extern AudioMixer4 synthOnlyRight;
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

// Audio functions
void setupAudio();
void setReverbWet(float wet);
float getDiatonicThird(float noteFreq, int keyNote, bool isMajor);
void stopAllOscillators();
void initSineSound(float tonic, float third, float fifth, float octaveMul, float perVoice);
void initOrganSound(float tonic, float third, float fifth, float octaveMul, float perVoice);
void initRhodesSound(float tonic, float third, float fifth, float octaveMul, float perVoice);
void initStringsSound(float tonic, float third, float fifth, float octaveMul, float perVoice);
void startChord(float potNorm, float tonicFreq, int keyNote, bool isMajor);
void updateChordTonic(float tonicFreq, int keyNote, bool isMajor);
void stopChord();
void updateChordVolume(float potNorm);
void updateChordFade();

#endif // AUDIO_H
