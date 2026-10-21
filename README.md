# M5 Mod Wheel

An application for the M5StickS3 that acts as a USB or Bluetooth MIDI mod wheel or pitch wheel.

## USB MIDI

Connect the M5StickS3 with a data-capable cable. The display shows `USB` when connected.

## Bluetooth MIDI

Press the side button to start Bluetooth pairing. The display shows `PAIR`.
Pair with the device named `M5 Mod Wheel`. The display then shows `CONN`.

On macOS, BLE MIDI do not appear in System Settings > Bluetooth like headphones.
See [Set up Bluetooth MIDI devices in Audio MIDI Setup on Mac](https://support.apple.com/en-euro/guide/audio-midi-setup/ams33f013765/mac).

## Controls

- Press the front button to switch between mod-wheel and pitch-bend modes.
  Each press also captures the current orientation as the zero pose.
- Tilt up and down to change the MIDI value.

## MIDI output

- Mod-wheel mode sends Control Change 1 on MIDI channel 1. The zero pose maps to 64; tilting in either direction moves the value toward 0 or 127.
- Pitch-bend mode sends standard 14-bit pitch-bend values on MIDI channel 1, centered at 8192 in the 0–16383 range. The zero pose is the center; tilting reaches the bend limits.
