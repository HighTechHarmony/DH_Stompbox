#include "sdcard.h"
#include <Audio.h>

// ---------------------------------------------------------------------------
// SD-card state
// ---------------------------------------------------------------------------
bool sdCardAvailable = false;
char sdCurrentPath[256] = "/";
SDEntry sdEntries[SD_MAX_ENTRIES];
int sdEntryCount = 0;
int sdBrowseIndex = 0;
int sdBrowseViewportStart = 0;

// Selected sample state
bool sampleSelected = false;
char selectedSamplePath[256] = "";
bool sampleLoopMode = false; // parsed from filename: 'L' = loop, 'S' (or unknown) = single-shot

// Note: AudioPlaySdWav samplePlayer is defined in audio.cpp
// (must be in the same TU as AudioConnections for proper init order)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool sdAtRoot()
{
    return (strcmp(sdCurrentPath, "/") == 0);
}

int sdTotalVisibleCount()
{
    // ".." row only when not at root, plus all entries, plus "^" to exit
    int count = sdEntryCount + 1; // entries + "^"
    if (!sdAtRoot())
        count += 1; // ".." row
    return count;
}

void abbreviateName(const char *fullName, char *abbrev, int maxLen, bool isDir)
{
    // maxLen includes the null terminator.
    // Directories get a trailing '/' so their usable chars are one fewer.
    int usable = maxLen - 1; // chars we can show (excl. null)
    if (isDir && usable > 1)
        usable -= 1; // reserve room for trailing '/'

    int len = (int)strlen(fullName);

    if (len <= usable)
    {
        // Fits as-is
        strcpy(abbrev, fullName);
    }
    else
    {
        // Truncate: first (usable-1) chars + '~'
        int keep = usable - 1;
        if (keep < 0)
            keep = 0;
        strncpy(abbrev, fullName, keep);
        abbrev[keep] = '~';
        abbrev[keep + 1] = '\0';
    }

    // Append '/' for directories
    if (isDir)
    {
        int cur = (int)strlen(abbrev);
        if (cur < maxLen - 1)
        {
            abbrev[cur] = '/';
            abbrev[cur + 1] = '\0';
        }
    }
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

bool initSDCard()
{
    sdCardAvailable = SD.begin(SD_CS_PIN);
    if (sdCardAvailable)
    {
        Serial.println("SD card initialised OK");
        scanDirectory("/");
    }
    else
    {
        Serial.println("SD card init FAILED");
    }
    return sdCardAvailable;
}

// ---------------------------------------------------------------------------
// Directory scanning
// ---------------------------------------------------------------------------

bool scanDirectory(const char *path)
{
    // AudioPlaySdWav::update() runs in the audio ISR and reads the SD card over
    // the same SPI bus.  The SD library is not reentrant, so any concurrent
    // main-loop SD access while a sample is playing corrupts SPI state and
    // hard-freezes the MCU.  Stopping the player first is safe: once stopped,
    // update() generates silence without touching the SPI bus.
    if (samplePlayer.isPlaying())
        samplePlayer.stop();

    sdEntryCount = 0;
    sdBrowseIndex = 0;
    sdBrowseViewportStart = 0;

    strncpy(sdCurrentPath, path, sizeof(sdCurrentPath) - 1);
    sdCurrentPath[sizeof(sdCurrentPath) - 1] = '\0';

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory())
    {
        Serial.print("SD: cannot open directory ");
        Serial.println(path);
        return false;
    }

    while (true)
    {
        File entry = dir.openNextFile();
        if (!entry)
            break;
        if (sdEntryCount >= SD_MAX_ENTRIES)
            break;

        const char *name = entry.name();

        // Skip hidden files/dirs (names starting with '.')
        if (name[0] == '.')
        {
            entry.close();
            continue;
        }

        strncpy(sdEntries[sdEntryCount].name, name, SD_NAME_MAX_LEN - 1);
        sdEntries[sdEntryCount].name[SD_NAME_MAX_LEN - 1] = '\0';
        sdEntries[sdEntryCount].isDirectory = entry.isDirectory();
        abbreviateName(name, sdEntries[sdEntryCount].displayName,
                       SD_DISPLAY_MAX_LEN, entry.isDirectory());

        entry.close();
        sdEntryCount++;
    }
    dir.close();

    // Sort: directories first, then files; alphabetical within each group
    for (int i = 0; i < sdEntryCount - 1; i++)
    {
        for (int j = 0; j < sdEntryCount - i - 1; j++)
        {
            bool doSwap = false;
            if (sdEntries[j].isDirectory == sdEntries[j + 1].isDirectory)
            {
                doSwap = (strcasecmp(sdEntries[j].name, sdEntries[j + 1].name) > 0);
            }
            else
            {
                doSwap = !sdEntries[j].isDirectory; // directories first
            }
            if (doSwap)
            {
                SDEntry temp = sdEntries[j];
                sdEntries[j] = sdEntries[j + 1];
                sdEntries[j + 1] = temp;
            }
        }
    }

    Serial.print("SD: scanned ");
    Serial.print(path);
    Serial.print(" -> ");
    Serial.print(sdEntryCount);
    Serial.println(" entries");

    return true;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void sdNavigateUp()
{
    if (sdAtRoot())
        return; // nothing to do

    // Strip the last path component
    char *lastSlash = strrchr(sdCurrentPath, '/');
    if (lastSlash == sdCurrentPath)
    {
        // One level deep – go back to root
        strcpy(sdCurrentPath, "/");
    }
    else if (lastSlash)
    {
        *lastSlash = '\0';
    }

    scanDirectory(sdCurrentPath);
}

void sdNavigateInto(int index)
{
    if (index < 0 || index >= sdEntryCount)
        return;
    if (!sdEntries[index].isDirectory)
        return; // can only descend into directories

    char newPath[256];
    if (sdAtRoot())
    {
        snprintf(newPath, sizeof(newPath), "/%s", sdEntries[index].name);
    }
    else
    {
        snprintf(newPath, sizeof(newPath), "%s/%s", sdCurrentPath, sdEntries[index].name);
    }

    scanDirectory(newPath);
}

// ---------------------------------------------------------------------------
// File selection & playback
// ---------------------------------------------------------------------------

void sdSelectFile(int entryIndex)
{
    if (entryIndex < 0 || entryIndex >= sdEntryCount)
        return;
    if (sdEntries[entryIndex].isDirectory)
        return; // only files

    // Build full path
    if (sdAtRoot())
    {
        snprintf(selectedSamplePath, sizeof(selectedSamplePath),
                 "/%s", sdEntries[entryIndex].name);
    }
    else
    {
        snprintf(selectedSamplePath, sizeof(selectedSamplePath),
                 "%s/%s", sdCurrentPath, sdEntries[entryIndex].name);
    }

    sampleSelected = true;

    // Parse loop/single flag from filename convention: NNX-*.wav
    // where NN = 2-digit slot, X = 'L' (loop) or 'S' (single-shot).
    // Check character at index 2 of the bare filename.
    const char *bare = sdEntries[entryIndex].name;
    sampleLoopMode = (strlen(bare) > 2 && (bare[2] == 'L' || bare[2] == 'l'));

    Serial.print("SD: selected sample ");
    Serial.print(selectedSamplePath);
    Serial.println(sampleLoopMode ? " [LOOP]" : " [SINGLE]");
}

void startSamplePlayback()
{
    if (!sampleSelected || !sdCardAvailable)
        return;

    // Stop any current playback first (allows instant re-trigger)
    if (samplePlayer.isPlaying())
    {
        samplePlayer.stop();
    }

    samplePlayer.play(selectedSamplePath);

    Serial.print("SD: playing ");
    Serial.println(selectedSamplePath);
}

void stopSamplePlayback()
{
    if (samplePlayer.isPlaying())
    {
        samplePlayer.stop();
        Serial.println("SD: sample stopped");
    }
}

bool isSamplePlaying()
{
    return samplePlayer.isPlaying();
}
