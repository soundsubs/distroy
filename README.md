# DISTROY

An 8-slot chain of modeled distortion/overdrive pedals for Ableton Move,
built as a Schwung `audio_fx` module. On load, all 8 slots are randomly
assigned a pedal from the roster; signal flows **right to left** across
the 8 knobs (knob 8 processes first, knob 1 processes last).

## Pedal roster

| Pedal | Knob controls | Character |
|---|---|---|
| Boss OD | Wet/Dry | Symmetric soft clip + presence lift |
| Fuzz | Wet/Dry | Asymmetric germanium-style clip, thinned bass |
| Metal | Wet/Dry | Cascaded hard clip + scooped mids |
| Tubescreamer | Wet/Dry | Mid-hump pre-emphasis + asymmetric soft clip |
| Big Muff | Wet/Dry | Cascaded hard clip (sustain-heavy) + mild scoop |
| Sansamp | Wet/Dry | Blended clean + saturated signal, warmth boost |
| Rat | Wet/Dry | Hard clip + drive-linked darkening low-pass |
| Geiger Counter | Wet/Dry | Aggressive asymmetric clip + quantization grit |
| Moog Ladder | Cutoff | 4-pole (24dB/oct) resonant lowpass, self-saturating, with Drive |
| Korg MS-20 | Cutoff | Resonant HPF -> resonant LPF in series, each self-saturating |

**All 8 distortion-type pedals now use Wet/Dry (v0.4.3)** — previously
Fuzz, Metal, and Big Muff used Gain instead. Every knob now defaults to
50% and uniformly means "how much of the effect is blended in," rather
than mixing Gain-mode and Wet/Dry-mode pedals with different knob
semantics. The two filter types (Moog Ladder, Korg MS-20) keep Cutoff,
since Wet/Dry isn't a meaningful concept for a filter's frequency
control.

**Why the filters were added:** a chain of 8 distortion stages tends to
compound gain fast enough that everything collapses into a clipped
square wave by the output. Adding two filter types into the same random
pool means roughly 1 in 5 slots will be a filter instead of another
distortion stage, breaking up the cascade and giving the chain real
tonal variety instead of just "more clipping."

**Moog Ladder** uses the well-known Stilson/Smith discrete
approximation of the classic transistor ladder filter (the reference
model reused across many open-source virtual-analog synths) — 4 cascaded
one-pole stages with resonant feedback, a cubic soft-clip on the
resonant node (the ladder's own inherent saturation), and an explicit
tanh() input drive stage. Resonance can approach self-oscillation at
high settings, same as the real circuit.

**Korg MS-20** models the character of the real unit's resonant
HPF-into-LPF signal path, where each stage distorts on its own and feeds
the other — implemented as two independently resonant, self-saturating
2-pole stages (same ladder-filter DSP philosophy as the Moog, just 2
poles instead of 4) in series. This is a characterful approximation, not
a literal transcription of the real analog HPF/LPF topology (see Known
Simplification below).

**Stability note:** ladder-style resonant filters can genuinely diverge
to infinity near their self-oscillation threshold if not carefully
damped — this actually happened during testing (`make test` caught it:
Moog Ladder produced `inf`/`nan` at max cutoff with default resonance).
Fixed by pulling back resonance headroom from the classic formula's ~4.0
self-oscillation threshold, plus a hard safety clamp on internal filter
state as a second safety net. Verified via a dedicated worst-case test:
max cutoff + max resonance + max drive simultaneously, sustained for a
full second, stays finite.

**Resonance disabled on randomization (v0.4.3):** even a 25% cap
(v0.4.2) still howled loudly enough in some randomized chains to risk
hurting ears/speakers. Resonance is no longer randomized at all for
Moog Ladder or Korg MS-20 — always exactly 0 on chain load. Live
resonance control is planned for a future submenu (see Open Questions)
where it can be dialed in deliberately instead.

**No duplicate pedal types in a chain (v0.4.1):** randomization now
shuffles all 10 types and takes the first 8 (Fisher-Yates), guaranteeing
every slot in a chain gets a distinct pedal — no more "two Boss ODs in
a row." Verified across 1000 randomized chains.

**Knob mode design note:** originally split Gain vs Wet/Dry across the 8
distortion pedals (see git history), but as of v0.4.3 all 8 use Wet/Dry
uniformly — see the pedal roster table above.

**Modeling note:** these are characteristic circuit-topology
approximations (soft/hard clipping curves + coloration filters tuned to
each pedal's known character), not component-level SPICE-accurate
models. WMD Geiger Counter in particular is modeled only for its
aggressive fuzz/clipping character — its sequencer/gate/pattern features
are out of scope for a knob-controlled audio effect.

## Per-pedal sub-parameters (Drive / Tone / Level)

Every real pedal has more than one control — a Tubescreamer has
Overdrive/Tone/Level, a Big Muff has Sustain/Tone/Volume, and so on.
Since only one physical knob is available per slot (already spoken for
by the primary GAIN/WET_DRY control), each pedal now also gets three
**randomized-on-load** sub-parameters instead of fixed constants:

- **Drive** — for WET_DRY-mode pedals, this is the pedal's own internal
  drive amount (previously a fixed constant); for GAIN-mode pedals the
  primary knob already covers this, so it's unused.
- **Tone** — a tilt EQ (bass-boost/treble-cut ↔ bass-cut/treble-boost
  around a per-pedal center frequency), layered on top of that pedal's
  fixed characteristic coloration filter (which still models the
  circuit's inherent voicing).
- **Level** — output trim, ±3dB-ish range.

Every RANDOMIZE (on load or via the menu action) now rolls fresh values
for all three on every slot, so two chains with the same 8 pedals in
the same order can still sound meaningfully different.

**Not yet solved: live editing of these via a submenu.** The spec asks
for these to be adjustable through a sub-menu when a pedal is selected.
That requires a UI mechanism I haven't confirmed exists — see
`docs/MODULES.md`'s "Dynamic Definition (get_param)" and mentions of
nested `ui_hierarchy` levels (Osirus/Virus's module description
mentions "scrollable categories," suggesting multi-level navigation is
possible for *some* modules), but I don't have a confirmed working
example the way Ducker's `module.json` gave us for the base `ui_hierarchy`
schema. For now, these three are randomized but not live-editable.
Verified via a dedicated stress test (`make test`): all 8 pedal types ×
8 corner-case combinations of Drive/Tone/Level at their extremes (0.0
and 1.0) produce finite output.

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

## Open questions (need on-device testing / real API docs)

1. **"Touching a knob (without turning) shows the pedal's name."**
   **Confirmed NOT achievable via `get_param()`'s returned string** —
   tested three approaches on real hardware, all consistent: the host
   parses the leading number as the literal stored value (not our
   declared 0.0-1.0 range as a scale factor) and re-formats/displays
   that number independently, always discarding any appended text,
   regardless of param `type` (`float` vs `mode`) or number/text
   ordering. Dynamically showing the pedal's name — and by extension,
   live-editing the Drive/Tone/Level sub-parameters via a submenu —
   needs a different, unconfirmed API surface. Worth asking on the
   Schwung Discord.
2. **Knob turning intermittently not registering.** Reported multiple
   times across versions, including after reverting to the
   configuration that tested fine in prior sessions (`type: "float"` +
   plain numeric `get_param()`), suggesting this isn't purely a
   get_param format issue. v0.4.3 adds file-based diagnostic logging
   (`set_param()` calls, same technique that resolved EMAX_FX's
   `on_midi` mystery) to gather real evidence instead of guessing at
   more format changes. Retrieve `debug.log` via SSH or the Schwung
   Manager Files browser at
   `move.local:7700/files?path=/data/UserData/schwung/modules/audio_fx/DISTROY`
   after turning a few knobs, to see whether `set_param()` is even
   being called and with what values.
2. **"Shift+Touch a knob to pick a different pedal for that slot."**
   No confirmed API for a distinct shift+touch gesture reaching
   third-party `audio_fx` plugins. All 8 knobs are already assigned
   (one per slot), so this would need a secondary access mechanism not
   yet identified. Possibly related to the "Swap Module" / preset-picker
   behavior noticed on EMAX_FX (a host-level UI mechanism, not something
   plugins directly implement).
3. **RANDOMIZE menu action — confirmed working.** Exposed as a 9th
   param not listed in `module.json`'s `"knobs"` array; it shows up in
   the module's own jog-wheel-navigable menu and correctly re-rolls the
   whole chain. This is genuinely useful evidence for #1/#2 above: params
   outside `"knobs"` DO surface somewhere in the UI — the open question
   is whether that same surface can host a *submenu of live-editable
   sub-parameters* per slot, which would solve the Drive/Tone/Level
   editing question too.

Flagged rather than guessed at blind, same approach that worked
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
