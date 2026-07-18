# DISTROY

An 8-slot chain of modeled distortion pedals, filters, and other effects
for Ableton Move, built as a Schwung `audio_fx` module. On load, all 8
slots are randomly assigned a pedal from the roster (21 types as of
v0.5.0); signal flows **right to left** across the 8 knobs (knob 8
processes first, knob 1 processes last).

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
| Mu-Tron | Sens | Envelope-following auto-wah, smooth/rounded (wide Q) |
| Cry Baby 535Q | Sens | Envelope-following auto-wah, narrow/vocal (high Q, snappier) |
| Jensen | Wet/Dry | Transformer saturation, bright/extended top end |
| Lundahl | Wet/Dry | Transformer saturation, darker/more colored low-mid |
| LoFi | Wet/Dry | Random bit-depth (1-15) + sample-rate (100-10000Hz) crush |
| Boss FZ-1W | Wet/Dry | Tighter, more symmetric silicon fuzz than vintage Fuzz |
| Clip | Wet/Dry | Bare hard clipper, no coloration |
| Rekt | Wet/Dry | Hard clip + full-wave rectify -- harsh, pitched-up buzz |
| Wham | Wet/Dry | Pitch shifter, weighted toward +-12 semitones, never 0 |
| Tape | Wet/Dry | Tape saturation + subtle hiss + HF rolloff |
| Speaker | Size | Speaker cabinet size (v0.5.1: impossibly small/tinny <-> impossibly large/boomy) |
| Noiz | Mix (capped 66%) | White/pink/red noise generator, dry signal always at least 34% |
| Tube | Wet/Dry | Vintage Russian tube saturation, asymmetric grit + warm rolloff |
| Cable Fault | Mix (severity) | Broken 1/4" cable/jack sim -- random crackle/cutout, never fully silent |

**All distortion-type pedals use Wet/Dry (v0.4.3+)** — every knob
defaults to 50% and uniformly means "how much of the effect is blended
in." Filters (Moog Ladder, Korg MS-20, Speaker) use Cutoff since Wet/Dry
isn't meaningful for a frequency sweep. Auto-wah types (Mu-Tron, Cry
Baby) use Sens (envelope sensitivity/depth) since there's no expression
pedal input to model a manually-swept wah.

**On the new additions (v0.5.0):**
- **Auto-wah (Mu-Tron, Cry Baby):** real hardware is normally swept by
  an expression pedal; without one, these model the classic
  envelope-follower "auto-wah" behavior instead — the filter sweeps in
  response to input signal level, not a foot pedal.
- **LoFi:** bit depth and sample rate are randomized (not knob-controlled)
  per the spec, verified to never land on 16-bit or 44100Hz across 2000
  randomized chains.
- **Wham:** transposition amount is randomized (not knob-controlled),
  weighted toward +-12 semitones (~70% combined) with other
  characterful intervals filling the remaining ~30%, verified to never
  land on 0 semitones across 2000 randomized chains. Uses a time-domain
  granular pitch-shifting technique (dual-tap crossfade delay line), not
  FFT-based — some grain artifacts are expected, not unlike a real
  Whammy pedal's own character.
- **Speaker:** models the frequency-response coloration of speaker size
  (highpass/lowpass corner + resonant cone-frequency bump, all
  size-linked to its own dedicated Size knob mode) rather than a full
  impulse-response cabinet simulation. **v0.5.1:** the original range
  (80-300Hz HP / 3.5-6kHz LP) was too subtle to hear clearly — widened
  to a deliberately extreme "impossibly small (cell-phone-tinny) to
  impossibly large (2-foot-woofer-boomy)" sweep (900Hz-20Hz HP,
  5000Hz-1300Hz LP, plus a resonant peak that moves from a sharp 2.2kHz
  "tinny" bump down to a loose 60Hz "boom" bump), per direct listening
  feedback. Also got its own `SIZE` knob-mode label (previously
  generic `CUTOFF`, shared with the actual filters).

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

## Filter/knob tuning pass (v0.9.0)

- **Polivoks resonance capped at 40%** — even its intentionally growly
  resonance was howling too much above that, per direct feedback.
- **Oberheim SEM's knob now inversely couples cutoff and resonance** —
  at knob=0, cutoff is near its minimum and resonance is at a randomized
  ceiling (always 50-100%); at knob=1, cutoff is at its maximum and
  resonance is 0. One knob sweeps both simultaneously in opposite
  directions, matching how the real SEM's filter character shifts.
- **LoFi's knob now directly controls sample rate** (increasing with the
  knob, max sample rate at full turn) instead of being a wet/dry blend
  — bit depth still randomizes independently. New `RATE` knob-mode label.
  This freed LoFi's `sub_tone` back up for normal Tone/TiltEQ duty.
- **Auto-wah envelope level now exposed** via
  `distroy_block_get_envelope_level()` for UI visualization (used by the
  VST3 to show incoming-signal activity near Mu-Tron/Cry Baby's knob —
  no equivalent visualization exists in the Move version's constrained
  UI, this is purely a new accessor for other consumers of the DSP core).

## Three more new types + tweaks (v0.8.0)

- **Oberheim SEM** — state-variable filter (Chamberlin topology), resonance
  safely up to 100% without reaching true self-oscillation ("doesn't
  fully resonate" per spec) — verified via a dedicated worst-case test,
  same rigor as Moog/Korg's, but without needing their "always 0 on
  randomize" safety rule since this topology stays controlled even at
  max resonance.
- **Polivoks** — same 2-pole ladder-style resonant filter structure as
  Korg MS-20, but with a hard clip (not a cubic soft-clip) on the
  resonant node, giving the distinctly gritty/"growly" heavily distorted
  character the real Russian synth's filter is known for.
- **Octafuzz** — Fulltone Octafuzz-style octave pedal, direction (up or
  down) chosen randomly per load. Up uses the classic Octavia-style
  full-wave-rectification technique; down reuses the WHAM pitch shifter
  fixed at -12 semitones plus some grit.
- **Noiz's cap lowered 66% → 50%** — even 66% got too loud once amplified
  downstream in a chain.
- **Tube improved** — gentler/rounder saturation curve plus explicit
  even-harmonic generation (a classic `x*|x|` technique) for genuine
  added warmth, not just clipping.
- **Cable Fault gets 60Hz hum** — randomized 0-10% mains hum mixed in
  continuously, simulating electrical interference/ground loop.

## Signal direction default changed (v0.7.0)

Previously right-to-left only (slot 7 processes first). Now defaults to
**left-to-right** (slot 0 processes first, slot 7 last) with an
underlying toggle in the shared DSP core (`DistroyChain.reverse`).
Move has no UI control to expose this toggle, so it always runs in the
new left-to-right default — this is a **behavior change** from earlier
versions for anyone already using this on their Move. The DISTROYBOY
VST3 (which has a proper UI) gets a clickable arrow to reverse it.

## Three new types (v0.7.0)

- **Noiz** — white/pink/red noise generator (colour chosen randomly on
  load). Pink uses Paul Kellet's well-known "economy" 3-pole
  approximation; red/brown is a simple leaky-integrator lowpass of
  white noise. The knob controls level, but is deliberately **capped at
  66% wet** even at full turn — verified via a dedicated test that the
  blend ratio never exceeds 0.66, so the dry signal is never fully
  interrupted.
- **Tube** — vintage Russian tube character: asymmetric soft saturation
  (differing positive/negative clip curves, modeling a "grittier" bias
  than a cleaner Western-tube model), warm high-frequency rolloff, and
  a very subtle noise floor.
- **Cable Fault** — simulates a broken 1/4" cable/jack: a small state
  machine randomly triggers brief crackle bursts or partial cutouts,
  plus a subtle constant noise floor even in its "normal" state (a
  genuinely bad cable has some character even when "working"). The knob
  controls how often/severely it glitches. **Cutouts are never full
  silence** — cutout level is randomized but always bounded well above
  zero (5-20% of signal always survives), verified via a dedicated test
  feeding 5 seconds of constant signal and confirming `cutoutLevel`
  never hits exactly 0.



A stereo-linked, look-ahead brickwall limiter now runs on the final
chain output, replacing what used to be a bare hard clamp to the int16
range. Design: delays the signal by ~2.9ms (128 samples @44.1kHz) while
scanning that same window for its true peak, so gain reduction can be
computed and smoothed *before* a peak actually reaches the output —
attack is effectively instant (safe specifically because the lookahead
already "saw the peak coming"), release is a smooth 80ms ramp back
toward unity. A final hard clamp to the -1dBFS ceiling remains as an
absolute backstop regardless of what the smoothed path does — this is
the actual safety guarantee, with the lookahead/smoothing on top purely
for transparency (so it doesn't sound like harsh clipping every time it
engages). Verified via a dedicated worst-case test: Metal into Rekt at
max drive, fed a deliberately overdriven 1.5x-amplitude input for a
full second — output never exceeded the ceiling, stayed finite
throughout.

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
   **CONFIRMED CLOSED as of v0.4.6 — not achievable via `get_param()`.**
   Tested exhaustively across many versions: name-first text,
   number-first text with incorrect scaling, and number-first text with
   *correct* scaling (v0.4.5, ruling out the double-multiply theory
   specifically). In every case the host reads only the leading
   parseable number and unconditionally discards everything after it —
   turning worked correctly under the correctly-scaled attempt, but the
   name still never appeared. Not retrying this again. Static `"Slot
   N"` labels are the final state. Dynamically showing the pedal's name
   — and by extension, live-editing the Drive/Tone/Level sub-parameters
   via a submenu — would need a different, unconfirmed API surface.
   Worth asking on the Schwung Discord if this is still wanted.
2. **Knob turning — RESOLVED, cause uncertain.** Was intermittently not
   registering across several versions, including after reverting to a
   configuration that had tested fine previously — suggesting it wasn't
   purely a `get_param` format issue. Started working reliably as of
   v0.4.4, whose only actual code change was adding diagnostic logging
   (unrelated to `set_param`/`get_param` logic). Best guess: repeated
   Uninstall→reinstall cycles eventually cleared stale cached UI state
   on the device itself (left over from earlier experiments with
   `type: "mode"`), rather than anything in v0.4.4's code specifically.
   Not fully confirmed, but turning has been solid since.
3. **"Shift+Touch a knob to pick a different pedal for that slot."**
   No confirmed API for a distinct shift+touch gesture reaching
   third-party `audio_fx` plugins. All 8 knobs are already assigned
   (one per slot), so this would need a secondary access mechanism not
   yet identified. Possibly related to the "Swap Module" / preset-picker
   behavior noticed on EMAX_FX (a host-level UI mechanism, not something
   plugins directly implement).
4. **RANDOMIZE menu action — confirmed working.** Exposed as a 9th
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
