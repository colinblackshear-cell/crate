# Kaleidobox Trigger (DIY drum trigger box)

Up to 8 piezo drum triggers (stick-on triggers, e-drum pads, or bare piezo discs on 1/4" jacks)
into an Arduino Pro Micro / Leonardo, out as USB MIDI notes with velocity. No driver.

- Firmware: `KaleidoboxTrigger/KaleidoboxTrigger.ino` (needs the MIDIUSB library; board "Arduino
  Leonardo"). Peak detection, velocity curve, retrigger mask that still lets rolls through,
  crosstalk rejection, and a TUNING mode that prints every hit to the Serial Monitor.
- Per input: jack tip -> input line; 1 MOhm and a 5.1 V zener (stripe to the line) from the line to
  ground; 1 kOhm from the line to the pin. Jack sleeve to ground.
- Inputs 1-8 -> Pro Micro pins A0, A1, A2, A3, 4, 6, 8, 9. Notes 36-43 on channel 10 (pads 1-8 in
  Kaleidobox before anything is learned).

In Kaleidobox: Settings > Audio & MIDI > MIDI controllers, kind "Drum triggers" (guessed from the
Arduino's name), then "Hit to learn" and hit each drum in turn. Each drum can play any pad (MPC or
drum machine, whichever the pads play) or press a loop layer's Rec.
