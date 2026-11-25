# audio/: Sound

> **What this is:** the list of pages in `docs/audio/`, one line each. Sound: audio, our own WAV decoder, the voice server.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [Audio](audio.md): Sound in OkaySpace comes from **AudioSource** components, mixed by the engine into stereo output at the device's rate.
- [Our own WAV decoder (`okay::wav`)](own_wav_decoder.md): Status: **the only WAV reader since 2026-11-23** (`SDL_LoadWAV`, the engine's older WAV path and the `OKAY_OWN_WAV` switch were deleted). Part of `docs/status/own_code_roadmap.md`.
- [Voice server](voice-server.md): Voice calls (friend calls, group calls and server voice rooms) go through the OkaySpace **relay**: every caller connects *out* to it over UDP and it forwards audio between the people in the same call.
