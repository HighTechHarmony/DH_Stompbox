#ifndef CONFIG_H
#define CONFIG_H

// ----------------------
// Configuration Constants
// ----------------------
#define BOOST_INPUT_GAIN false                         // set to false to use 0.5 gain (no boost)
#define SUPPRESS_CHORD_OUTPUT_DURING_TRANSITIONS false // when true, mute chord until FS1 forced window ends

// OLED Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Timing Constants
#define FS1_MIN_ACTIVATION_MS 500 // Window of time after FS1 press that tracking is on
#define SCREEN_TIMEOUT_MS 5000    // 5 seconds menu timeout

// Pitch detection sensitivity (0.0 = most sensitive, 1.0 = least sensitive)
// Increase this value to require more input volume/clarity before detection engages.
#define NOTE_DETECT_THRESHOLD 0.14f

#endif // CONFIG_H
