# DH_Stompbox

Compact Teensy-based stompbox synthesizer + pitch-tracking chord generator.

## Overview

DH_Stompbox reads instrument input, tracks pitch, and drives a 3-voice synth (root/3rd/5th) with UI, menu/settings persistence, arpeggiator, Rhodes/Organ/Strings voices, FS (footswitch) volume control and tap-tempo.

## Hardware

This was built on a Teensy 4.1 with the Rev.D Audioshield. It has the following supporting hardware/peripherals:

- (1) 0.96" SPI Display
- (2) Momentary SPST footswitches
- (1) 20K Linear Taper Potentiometer
- (1) Rotary encoder with press
- (3) 1/4" TS Audio Jacks
- (1) 3D Printed case (optional)

### Pinout

Update pins in [src/config.h](src/config.h) as needed for your hardware.

#### Summary

- Board: Teensy 4.1
- Notes: All digital inputs should use hardware or software pull-ups as appropriate. Analog inputs use the Teensy A0..A11 pins.

#### Current pin mapping

- OLED (I2C)
  - SDA: pin 18 (Wire / SDA)
  - SCL: pin 19 (Wire / SCL)

- Rotary encoder
  - ENC_A: D2
  - ENC_B: D3
  - ENC_BTN (push): D4

- Footswitches
  - FS1 (latching / momentary): D0
  - FS2: D6

- Potentiometer
  - VOLUME_POT: A0

- LEDs / status
  - STATUS_LED: D13 (on-board or external)

- Audio I/O
  - Recommended: use the Teensy Audio Shield or a supported I2S codec. Follow the shield/codec wiring instructions.
  - If directly wiring I2S/I2C codec, put pin assignments in [src/config.h](src/config.h) and document them here.

- SPI / SD (if used)
  - MOSI: D11
  - MISO: D12
  - SCK: D13
  - CS: D10 (example; change if using SD card or other SPI devices)

#### Analog inputs (examples)

- A0 — VOLUME_POT
- A1 — (spare)
- A2 — (spare)

#### Power & Ground

- 3.3V: use Teensy 3.3V pin for 3.3V peripherals (OLED, logic-level devices)
- GND: common ground between Teensy and all peripherals

#### Safety & tips

- Use debouncing for footswitches (hardware or software).
- Protect analog inputs with input resistor + clamp diodes if connecting external signals.
- Double-check Audio Shield wiring if used — follow PJRC Audio Shield docs.

#### Customization

Put final, confirmed pin numbers into [src/config.h](src/config.h).

## Quick Links

- Firmware entry: [src/main.cpp](src/main.cpp)
- Audio core: [src/audio.cpp](src/audio.cpp) — see [`audio.startChord`](src/audio.cpp), [`audio.setupAudio`](src/audio.cpp), [`audio.applyOutputMode`](src/audio.cpp)
- Pitch detection: [src/pitch.cpp](src/pitch.cpp) — see [`updatePitchDetection`](src/pitch.cpp), [`setupPitchDetection`](src/pitch.cpp)
- Menu/UI: [src/menu.cpp](src/menu.cpp) — see [`handleMenuButton`](src/menu.cpp), [`handleMenuEncoder`](src/menu.cpp)
- Display renderer: [src/display.cpp](src/display.cpp) — see [`renderHomeScreen`](src/display.cpp), [`renderMenuScreen`](src/display.cpp)
- Persistent settings: [src/NVRAM.cpp](src/NVRAM.cpp) — see [`saveNVRAM`](src/NVRAM.cpp), [`loadNVRAM`](src/NVRAM.cpp)
- Input handling: [src/input.cpp](src/input.cpp)
- Hardware test mode: [src/test.cpp](src/test.cpp)
- Build config: [platformio.ini](platformio.ini)
- Project config: [src/config.h](src/config.h)

## Features

- Real-time pitch-to-chord tracking (auto tonic update) — controlled in [src/pitch.cpp](src/pitch.cpp)
- Multiple synth sounds: Sine, Organ, Rhodes, Strings — voice inits in [src/audio.cpp](src/audio.cpp)
- Arpeggiator with timer-driven steps — see [`startArpTimer`](src/audio.cpp) and [`updateArpTimerInterval`](src/audio.cpp)
- FS volume mode (dual footswitch), tap-tempo, and Rhodes decay behavior — handled in [src/main.cpp](src/main.cpp) and [src/audio.cpp](src/audio.cpp)
- Persistent settings (key, mode, octave, synth sound, arp, output, stop mode) in EEPROM via [src/NVRAM.cpp](src/NVRAM.cpp)

## Build & Flash

Requirements: PlatformIO, Teensy board support.

Build:

```sh
pio run
```

Upload (connected Teensy):

```sh
pio run -t upload
```

## Usage

- **Encoder**: Navigate menu and change values — menu handling in [src/menu.cpp](src/menu.cpp)
- **FS1 / FS2**: Play/stop, tap tempo, and entry to FS volume control — see [src/main.cpp](src/main.cpp)
- **Pot**: Volume control (overrides FS volume mode)
- **Test mode**: Hold FS1 at boot for hardware diagnostics — see [src/test.cpp](src/test.cpp)

## Configuration

Tweak compile-time behavior in [src/config.h](src/config.h) (e.g., `NOTE_DETECT_THRESHOLD`, FS timeouts).

## File Layout

- [src/main.cpp](src/main.cpp) — Main loop, input orchestration, UI state
- [src/audio.cpp](src/audio.cpp) / [src/audio.h](src/audio.h) — Synth voices, mixers, arp, vibrato, fade/decay logic
- [src/pitch.cpp](src/pitch.cpp) / [src/pitch.h](src/pitch.h) — Pitch detection and smoothing
- [src/menu.cpp](src/menu.cpp) / [src/display.cpp](src/display.cpp) — UI and OLED rendering
- [src/NVRAM.cpp](src/NVRAM.cpp) / [src/NVRAM.h](src/NVRAM.h) — EEPROM persistence
- [src/input.cpp](src/input.cpp) / [src/input.h](src/input.h) — Encoder/footswitch/pot handling
- [src/test.cpp](src/test.cpp) / [src/test.h](src/test.h) — Hardware diagnostics

## Contributing

Open issues / PRs. Keep changes small and hardware-tested where applicable.

## License

MIT
