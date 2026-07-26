# Tone Graph

Route per-app Windows audio through a simple node graph (Input → effects → Output).

## Features

- Per-app audio routing on Windows (via VB-Cable + WASAPI)
- Node graph: wire Input → effects → Output, then Run
- Live meters on Input (pre-FX) and Output (post-FX)
- Effects update live while streaming
- **Nodes**
  - Input / Output
  - Gain
  - EQ (curve with draggable bands)
  - Compressor
  - Limiter
  - Gate
  - Pan
  - High-pass / Low-pass
  - Waveform
  - Split L/R / Merge L/R (process left and right separately)

## Needs

- Windows
- [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) (used to park/capture app audio)
- Visual Studio 2019+ with C++ tools (for `cl` / `vcvars64`)

## Build

```bat
build.bat
```

That makes `ToneGraph.exe`.

## Use

1. Run `ToneGraph.exe`
2. Add Input / Output (and optional effects)
3. Wire them together, pick an app and an output device
4. Hit **Run**

## Note

Because of how VB-Cable works, if Tone Graph closes abruptly (crash, Task Manager, etc.) an app may stay parked on the cable. Go to Windows **Settings → System → Sound** (or the volume mixer) and set that app’s output back to your default device.
