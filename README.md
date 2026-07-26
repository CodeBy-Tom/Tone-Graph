# Tone Graph

Route per-app Windows audio through a simple node graph (Input → effects → Output).

Effects: Gain, EQ, Compressor, Pan, High-pass, Low-pass, Limiter, Gate, Waveform.

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
