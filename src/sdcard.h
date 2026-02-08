#ifndef SDCARD_H
#define SDCARD_H

#include <Arduino.h>
#include <SD.h>

// SD card on the Audio Shield uses SPI chip-select pin 10
#define SD_CS_PIN 10

// Limits for directory listing
#define SD_MAX_ENTRIES 64
#define SD_NAME_MAX_LEN 64
#define SD_DISPLAY_MAX_LEN 9 // 8 visible chars + null terminator

struct SDEntry
{
    char name[SD_NAME_MAX_LEN];           // full file/directory name
    char displayName[SD_DISPLAY_MAX_LEN]; // abbreviated name for OLED
    bool isDirectory;
};

// SD card state
extern bool sdCardAvailable;
extern char sdCurrentPath[256];
extern SDEntry sdEntries[SD_MAX_ENTRIES];
extern int sdEntryCount;
extern int sdBrowseIndex;
extern int sdBrowseViewportStart;

// Initialise the SD card; returns true on success
bool initSDCard();

// Scan directory at |path| and populate sdEntries[].
// Resets sdBrowseIndex and sdBrowseViewportStart.
bool scanDirectory(const char *path);

// Navigate to parent directory (no-op if already at root)
void sdNavigateUp();

// Navigate into the directory at the given sdEntries[] index
void sdNavigateInto(int index);

// Abbreviate a name so it fits within |maxLen| chars (including null).
// Directories get a trailing '/' in the display name.
void abbreviateName(const char *fullName, char *abbrev, int maxLen, bool isDir);

// Return total number of visible rows in the SD browse list.
// This includes the ".." row (when not at root) and the "^" row.
int sdTotalVisibleCount();

// Return true when sdCurrentPath is "/"
bool sdAtRoot();

#endif // SDCARD_H
