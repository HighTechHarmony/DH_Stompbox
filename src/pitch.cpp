#include "pitch.h"

// Pitch detection object
AudioAnalyzeNoteFrequency noteDetect;

// keep track of last detected tonic frequency
float lastDetectedFrequency = 0.0f;

// Static state for pitch detection
static float sampledFrequency = 0.0f; // frequency sampled while FS1 held
static float freqBuf[3] = {0, 0, 0};  // median filter buffer
static int freqBufIdx = 0;

void setupPitchDetection()
{
    // Initialize pitch detector
    noteDetect.begin(0.10); // threshold typical 0.15 (0.0 = very sensitive, 1.0 = very picky)
    Serial.println("Pitch detector initialized");
}

void updatePitchDetection(float &frequency, float &probability, const char *&noteName)
{
    frequency = 0.0;
    probability = 0.0;
    noteName = "---";

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
            if (newNorm < 200.0f)
                newNorm = newNorm * 2.0f;
            else if (newNorm > 950.0f)
                newNorm = newNorm / 2.0f;
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
        }

        // Simple note name lookup (A4 = 440 Hz)
        if (sampledFrequency > 0.0f)
        {
            // Calculate note from frequency: n = 12 * log2(f/440) + 69 (MIDI note number)
            float n = 12.0 * log2f(sampledFrequency / 440.0) + 69.0;
            int noteNum = (int)(n + 0.5) % 12;
            if (noteNum < 0)
                noteNum += 12;
            const char *noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            noteName = noteNames[noteNum];
        }
    }
}
