# M5 Mod Wheel

An application for the M5StickS3 that acts as a Bluetooth MIDI mod wheel or pitch wheel.

## Controls

- Press button 2 to start Bluetooth MIDI advertising. Pair with the device named `M5 Mod Wheel`; it remains discoverable until connected and resumes advertising after a disconnect.
- Press button 1 to switch between mod-wheel and pitch-bend modes. Each press also captures the current orientation as the zero pose.
- Tilt along the X axis to change the MIDI value. The value reaches its minimum or maximum at 45 degrees from the captured pose and is clamped beyond that range.

The display shows the selected mode, Bluetooth connection state, current wheel value, and tilt from the zero pose.

## MIDI output

- Mod-wheel mode sends Control Change 1 on MIDI channel 1. The zero pose maps to 64; tilting in either direction moves the value toward 0 or 127.
- Pitch-bend mode sends standard 14-bit pitch-bend values on MIDI channel 1, centered at 8192 in the 0–16383 range. The zero pose is the center; tilting reaches the bend limits.
