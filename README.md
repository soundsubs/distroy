# DISTROY

An 8-slot chain of modeled distortion/overdrive pedals for Ableton Move,
built as a Schwung `audio_fx` module. On load, all 8 slots are randomly
assigned a pedal from the roster; signal flows **right to left** across
the 8 knobs (knob 8 processes first, knob 1 processes last).

## Pedal roster

| Pedal | Knob controls | Character |
|---|---|---|
| Boss OD | Wet/Dry | Symmetric soft clip + presence lift |
| Fuzz | Gain | Asymmetric germanium-style clip, thinned bass |
| Metal | Gain | Cascaded hard clip + scooped mids |
| Tubescreamer | Wet/Dry | Mid-hump pre-emphasis + asymmetric soft clip |
| Big Muff | Gain | Cascaded hard clip (sustain-heavy) + mild scoop |
| Sansamp | Wet/Dry | Blended clean + saturated signal, warmth boost |
| Rat | Wet/Dry | Hard clip + drive-linked darkening low-pass |
| Geiger Counter | Wet/Dry | Aggressive asymmetric clip + quantization grit |

**Knob mode design note:** the spec calls for knobs to default to a
"wet/dry amount," with some pedals using Gain instead. I split this as:
Gain for the classic "always fully driven" pedals where drive amount
itself is the character (Fuzz, Metal, Big Muff), Wet/Dry for pedals
more commonly used as blended boosts/texture (the other five). This
split wasn't fully specified in the request — easy to rebalance if a
different split is wanted.

**Modeling note:** these are characteristic circuit-topology
approximations (soft/hard clipping curves + coloration filters tuned to
each pedal's known character), not component-level SPICE-accurate
models. WMD Geiger Counter in particular is modeled only for its
aggressive fuzz/clipping character — its sequencer/gate/pattern features
are out of scope for a knob-controlled audio effect.

## Status

- **DSP core**: implemented and tested (`make test`) — finite output
  across a full knob sweep for all 8 pedal types individually and the
  full randomized chain, no NaN/Inf. Randomization distribution verified
  even across 1000 seeds (~992-1005 per type out of 8000 assignments).
- **Architecture**: built directly on lessons from the EMAX_FX project —
  a dlopen()'d `audio_fx` shared library (`DISTROY.so`) implementing
  `audio_fx_api_v2_t`, `module.json` using the `ui_hierarchy` schema
  (confirmed working via `charlesvestal/schwung-ducker`'s real,
  installed `module.json`, not the possibly-outdated `chain_params`
  schema from `docs/MODULES.md`).
- **No MIDI dependency**: unlike EMAX_FX, DISTROY is pure audio
  processing driven by knobs only — this sidesteps the `on_midi`
  mystery we never fully resolved on EMAX_FX (notes never reached that
  plugin's `on_midi()` despite matching a working reference module's
  schema).
- **Not yet tested on real Move hardware.**

## Open questions (need on-device testing)

1. **"Touching a knob (without turning) shows the pedal's name."**
   `module.json`'s per-param `label` is static at declare-time, but
   which pedal occupies a slot is randomized at runtime, so it can't be
   baked in. `get_param()` currently returns just the plain numeric
   value (safe default that should keep the knob's fill-arc display
   working correctly). Whether Schwung has a way for a plugin to
   dynamically override a param's displayed name is unconfirmed —
   needs real-device testing.
2. **"Shift+Touch a knob to pick a different pedal for that slot."**
   No confirmed API for a distinct shift+touch gesture reaching
   third-party `audio_fx` plugins. All 8 knobs are already assigned
   (one per slot), so this would need a secondary access mechanism not
   yet identified. Possibly related to the "Swap Module" / preset-picker
   behavior noticed on EMAX_FX (a host-level UI mechanism, not something
   plugins directly implement) — worth investigating together once this
   is running on real hardware.

Both are flagged rather than guessed at blind, same approach that worked
for narrowing down EMAX_FX's issues.

## Install

**Option A — Schwung Manager "Install Custom Module" (recommended):**
```sh
make test               # sanity check
./scripts/build.sh       # ARM64 cross-compile via Docker -> DISTROY.so
./scripts/package.sh     # produces DISTROY-module.tar.gz
```
Then create a GitHub Release tagged to match `release.json`'s version,
upload `DISTROY-module.tar.gz` as a release asset, and in Schwung
Manager (`move.local:7700/modules`) choose "Install Custom Module" and
give it this repo's URL.

**Option B — direct SSH deploy (bypasses the Module Store UI):**
```sh
./scripts/build.sh
./scripts/install.sh     # scp's DISTROY.so + module.json to ableton@move.local
```

## Build

```sh
make test           # native sanity check of the DSP core only
./scripts/build.sh    # ARM64 cross-compile via Docker -> build-arm64/DISTROY.so
./scripts/package.sh  # -> DISTROY-module.tar.gz
./scripts/install.sh  # deploy to move.local over SSH
```
