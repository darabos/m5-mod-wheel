# M5 Mod Wheel

An application for the M5StickS3 that acts as a USB or Bluetooth MIDI mod wheel or pitch wheel.

## USB MIDI

Connect the M5StickS3 to a USB host with a data-capable cable. It appears as `M5 Mod Wheel` and uses USB MIDI whenever the host is connected. USB MIDI takes priority over Bluetooth automatically; disconnect USB to use Bluetooth MIDI.

## Controls

- Press button 2 to start Bluetooth MIDI advertising. Pair with the device named `M5 Mod Wheel`; it remains discoverable until connected and resumes advertising after a disconnect. Bluetooth is used when USB MIDI is not connected.
- Press button 1 to switch between mod-wheel and pitch-bend modes. Each press also captures the current orientation as the zero pose.
- Tilt along the X axis to change the MIDI value. The value reaches its minimum or maximum at 45 degrees from the captured pose and is clamped beyond that range.

The landscape display shows a large light-purple `M` for mod wheel or light-green `P` for pitch bend on the left. Below it, `USB` indicates USB MIDI, `PAIR` indicates Bluetooth advertising, and `CONN` indicates a Bluetooth connection; otherwise the label is blank. USB status takes priority when both transports are connected.

A wide horizontal bar on the right matches the mode color. It is vertically centered at the zero pose, moves up as the wheel value increases and down as it decreases, and stops at the limits. Frames are drawn into an off-screen buffer before being displayed to avoid flickering.

Framework logging is limited to warnings and errors (`CORE_DEBUG_LEVEL=2`). Verbose BLE logging prints each MIDI notification and blocks on serial output, slowing both motion sampling and display refresh.

## MIDI output

- Mod-wheel mode sends Control Change 1 on MIDI channel 1. The zero pose maps to 64; tilting in either direction moves the value toward 0 or 127.
- Pitch-bend mode sends standard 14-bit pitch-bend values on MIDI channel 1, centered at 8192 in the 0–16383 range. The zero pose is the center; tilting reaches the bend limits.

## Bluetooth MIDI on macOS

On macOS, BLE MIDI devices usually do not appear in System Settings > Bluetooth like headphones.
Open Applications > Utilities > Audio MIDI Setup, choose Window > Show MIDI Studio,
then open Bluetooth Configuration and connect to “M5 Mod Wheel” while button 2 is advertising.

Apple’s guide: [Set up Bluetooth MIDI devices on Mac](https://support.apple.com/guide/audio-midi-setup/connect-bluetooth-midi-devices-ams6d6bc021a/mac).
