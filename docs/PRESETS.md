# Amplitron Presets Guide

Welcome to the Amplitron example presets! This collection is designed to demonstrate the versatility and power of the Amplitron DSP engine right out of the box. 

You can load these presets directly from the "Presets" menu in the GUI.

## Included Presets

### `01 Sparkling Clean`
* **Style:** Lush, shimmering clean tone
* **Signal Chain:** Compressor -> Amp Sim (Clean American) -> Chorus -> Delay -> Reverb
* **Use Case:** Perfect for arpeggiated chords, indie dream pop, and 80s ballads. Uses the Fender Twin emulation along with deep chorus and a spacious hall-style reverb.

### `02 Classic Rock Crunch`
* **Style:** Warm, mid-focused driven rock tone
* **Signal Chain:** Overdrive -> Amp Sim (British Crunch) -> Equalizer -> Cabinet (2x12) -> Reverb
* **Use Case:** Excellent for classic rock rhythm playing or blues solos. Focuses heavily on the Marshall JCM800 emulation pushed by an analog-style overdrive.

### `03 Modern Metal Lead`
* **Style:** High-gain, tight-tracking lead tone
* **Signal Chain:** Noise Gate -> Distortion -> Amp Sim (High Gain Modern) -> Equalizer (Mid-scoop) -> Cabinet (4x12) -> Delay (Lead echoes)
* **Use Case:** Fast alternate picking, sweep picking, and heavy breakdowns. Uses the Mesa Boogie emulation and hard-clipping distortion.

### `04 Ambient Swells`
* **Style:** Spatially massive, washed-out tone
* **Signal Chain:** Compressor (High sustain) -> Amp Sim (Jazz Warm) -> Chorus -> Multi-stage Delay -> Reverb (Max decay)
* **Use Case:** Ambient soundscapes, volume swells, and cinematic textures. Focuses on the pristine solid-state response of the Roland JC-120 emulation.

## Adapting Presets to Your Guitar

The provided presets sound great, but their response depends heavily on your specific guitar's electronics. Here's how to tweak them for the best results:

* **Noise Gate Threshold:** If you're using single-coil pickups, you may hear a 60-cycle hum on the high-gain patches (`03 Modern Metal Lead`). Try lowering the `Threshold` knob on the Noise Gate until the hum disappears while you aren't playing.
* **Input Gain/Amp Gain:** If your guitar uses high-output active pickups (like EMGs), the clean presets might distort slightly. You can counter this by slightly lowering the `Input Gain` in the main UI, or lowering the `Gain` knob directly on the `Amp Sim` effect block.
* **EQ Balancing:** Different woods and pickup styles impart different EQ curves. Use the `Equalizer` effect block to shape the sound. If a tone sounds muddy on your neck pickup, try cutting `Bass` or slightly boosting `Presence`.
