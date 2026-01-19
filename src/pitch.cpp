#include "pitch.h"
#include "NVRAM.h"
#include "audio.h"
#include "config.h"

// Pitch detection object
AudioAnalyzeNoteFrequency noteDetect;

// keep track of last detected tonic frequency
float lastDetectedFrequency = 0.0f;

// Static state for pitch detection
static float sampledFrequency = 0.0f; // frequency sampled while FS1 held
static float freqBuf[3] = {0, 0, 0};  // median filter buffer
static int freqBufIdx = 0;

// add near top of file (file-scope)
AudioConnection *patchPitchPtr = nullptr;

void setupPitchDetection()
{
    // Initialize pitch detector with threshold (tunable via config)
    // threshold: 0.0 = very sensitive, 1.0 = very picky
    noteDetect.begin(NOTE_DETECT_THRESHOLD);
    Serial.print("Pitch detector initialized with threshold ");
    Serial.println(NOTE_DETECT_THRESHOLD);

    // create the connection at runtime so initialization order is safe
    if (!patchPitchPtr)
    {
        // audioInput is declared in audio.h as extern; include audio.h if needed
        patchPitchPtr = new AudioConnection(audioInput, 0, noteDetect, 0);
        Serial.println("patchPitch connection created");
    }
}

void updatePitchDetection(float &frequency, float &probability, const char *&noteName, bool currentInstrumentIsBass)
{
    static unsigned long lastDebugMs = 0;
    static int availableCount = 0;
    static int notAvailableCount = 0;
    float lowMedianFreq = 50.0f;

    frequency = 0.0;
    probability = 0.0;
    noteName = "---";

    if (noteDetect.available())
    {
        availableCount++;
        frequency = noteDetect.read();
        probability = noteDetect.probability();

        // Debug: log raw readings periodically
        unsigned long now = millis();
        if (now - lastDebugMs > 2000)
        {
            Serial.print("Pitch detect - available: ");
            Serial.print(availableCount);
            Serial.print(", not available: ");
            Serial.print(notAvailableCount);
            Serial.print(", last freq: ");
            Serial.print(frequency);
            Serial.print(" Hz, prob: ");
            Serial.println(probability);
            lastDebugMs = now;
            availableCount = 0;
            notAvailableCount = 0;
        }

        // Add raw frequency to median filter buffer
        freqBuf[freqBufIdx] = frequency;
        freqBufIdx = (freqBufIdx + 1) % 3;

        // Copy and sort for median calculation
        float sorted[3];
        memcpy(sorted, freqBuf, sizeof(sorted));
        for (int i = 0; i < 3; i++)
            for (int j = i + 1; j < 3; j++)
                if (sorted[j] < sorted[i])
                {
                    float temp = sorted[i];
                    sorted[i] = sorted[j];
                    sorted[j] = temp;
                }

        float medianFreq = sorted[1]; // middle value after sorting (index 1 of 3 elements)

        // Use probability as a smoothing weight (rather than gating on a threshold).
        // Normalize the new median into the same octave range as `sampledFrequency`
        if (currentInstrumentIsBass)
        {
            lowMedianFreq = 25.0f;
        }
        else
        {
            lowMedianFreq = 50.0f;
        }

        if (medianFreq > lowMedianFreq && medianFreq < 2000.0)
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
    else
    {
        notAvailableCount++;
    }
}

void resetPitchDetection()
{
    // Clear median filter buffer
    for (int i = 0; i < 3; i++)
    {
        freqBuf[i] = 0.0f;
    }
    freqBufIdx = 0;

    // Reset last detected frequency so chord doesn't use stale data
    lastDetectedFrequency = 0.0f;

    Serial.println("Pitch detection reset");
}
