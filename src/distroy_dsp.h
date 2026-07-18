#ifndef DISTROY_DSP_H
#define DISTROY_DSP_H

/* DISTROY -- an 8-slot serial chain of modeled distortion/overdrive
 * pedals, filters, and other effects for Schwung (Ableton Move) and the
 * DISTROYBOY VST3. Signal direction is toggleable: LEFT TO RIGHT by
 * default (slot 0 processes first, slot 7 processes last), reversible
 * to right-to-left -- see DistroyChain's `reverse` field and
 * distroy_chain_process(). Slot-to-knob mapping is unaffected by
 * direction (knob 1 is always slot 0); only processing order changes.
 *
 * Each pedal/effect TYPE is a fixed, characteristic waveshaper/filter
 * plus a fixed (non-knob-controlled) coloration modeling that unit's
 * tonal signature. Per the project spec, each slot exposes exactly ONE
 * knob-controlled parameter: GAIN (drive amount), WET_DRY (blend),
 * CUTOFF (filter frequency), or SENS (auto-wah envelope sensitivity)
 * -- see DISTROY_KNOB_MODE below and distroy_type_info().
 *
 * KNOWN SIMPLIFICATION: these are characteristic circuit-topology
 * approximations (soft/hard clipping curves + coloration filters tuned
 * by ear/reference to each unit's known character), not
 * component-level SPICE-accurate models. WMD Geiger Counter in
 * particular is modeled only for its aggressive fuzz/clipping
 * character -- its sequencer/gate/pattern features are out of scope
 * for a knob-controlled audio effect and are not modeled here. The
 * Moog ladder filter uses the well-known Stilson/Smith discrete
 * approximation (see MoogLadder below); the Korg-style filter pair is
 * a characterful approximation of the MS-20's resonant/self-saturating
 * HPF->LPF behavior. The Mu-Tron/Cry Baby types model the classic
 * envelope-follower auto-wah behavior rather than a foot-pedal-swept
 * wah, since there's no expression pedal input in this architecture.
 * WHAM's pitch shifter is a time-domain granular dual-tap technique,
 * not FFT-based, so some grain artifacts are expected at extreme
 * ratios (not unlike a real Whammy pedal's own character).
 */

#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


typedef enum {
    DISTROY_BOSS_OD = 0,
    DISTROY_FUZZ,
    DISTROY_METAL,
    DISTROY_TUBESCREAMER,
    DISTROY_BIG_MUFF,
    DISTROY_SANSAMP,
    DISTROY_RAT,
    DISTROY_GEIGER_COUNTER,
    DISTROY_MOOG_LADDER,   /* 4-pole resonant lowpass with input drive */
    DISTROY_KORG_MS20,     /* resonant HPF -> resonant LPF, each self-saturating */
    DISTROY_MUTRON,        /* envelope-following auto-wah, smooth/rounded character */
    DISTROY_CRYBABY,       /* envelope-following auto-wah, narrower/more vocal character */
    DISTROY_JENSEN,        /* transformer saturation, bright/extended */
    DISTROY_LUNDAHL,       /* transformer saturation, darker/more colored */
    DISTROY_LOFI,          /* random bit depth (1-15) + sample rate (100-10000Hz) crush */
    DISTROY_FZ1W,          /* Boss FZ-1W Waza Craft -- tighter, more symmetric fuzz */
    DISTROY_CLIP,          /* bare hard clipper */
    DISTROY_REKT,          /* hard clip + full-wave rectify */
    DISTROY_WHAM,          /* pitch shifter, weighted toward +-12 semitones */
    DISTROY_TAPE,          /* tape saturation + hiss + HF rolloff */
    DISTROY_SPKR,          /* speaker cabinet size emulation */
    DISTROY_NOIZ,          /* white/pink/red noise generator, capped at 66% level */
    DISTROY_TUBE,          /* vintage Russian vacuum tube saturation */
    DISTROY_CABL,          /* broken 1/4" cable/jack simulation -- random crackle/cutout */
    DISTROY_SEM,           /* Oberheim SEM-style state-variable filter, controlled high resonance */
    DISTROY_POLIVOKS,      /* Polivoks-style growly, heavily distorted resonant filter */
    DISTROY_OCTAVE,        /* Fulltone Octafuzz-style octave pedal, up or down (random per load) */
    DISTROY_TYPE_COUNT
} DistroyType;

typedef enum {
    DISTROY_KNOB_GAIN = 0,
    DISTROY_KNOB_WET_DRY,
    DISTROY_KNOB_CUTOFF,
    DISTROY_KNOB_SENS,     /* auto-wah envelope sensitivity/depth */
    DISTROY_KNOB_SIZE,      /* speaker cabinet size sweep */
    DISTROY_KNOB_RATE       /* LoFi sample-rate sweep */
} DistroyKnobMode;

typedef struct {
    const char *name;          /* display name, e.g. "Boss OD" */
    const char *abbrev;        /* short display abbreviation, e.g. "OD" */
    DistroyKnobMode knob_mode; /* which single parameter the knob controls */
} DistroyTypeInfo;

/* Static metadata table -- name + knob mode per pedal type. */
const DistroyTypeInfo* distroy_type_info(DistroyType type);

/* One-pole filter state (used for DC blocking and simple low/high shelf
 * coloration). */
typedef struct {
    double b0, b1, a1;
    double x1, y1;
} OnePole;

void onepole_set_lowpass(OnePole *f, double cutoff_hz, double sample_rate);
void onepole_set_highpass(OnePole *f, double cutoff_hz, double sample_rate);
double onepole_process(OnePole *f, double x);

/* Biquad filter state (used for peaking/notch coloration -- Tubescreamer
 * mid hump, Metal Zone scoop). RBJ cookbook coefficients. */
typedef struct {
    double b0, b1, b2, a1, a2;
    double x1, x2, y1, y2;
} Biquad;

void biquad_set_peaking(Biquad *f, double freq_hz, double q, double gain_db, double sample_rate);
/* Constant-skirt-gain bandpass (RBJ cookbook) -- used by the auto-wah
 * types, recomputed per-sample as the envelope follower sweeps the
 * center frequency (same "retune every call" pattern as Rat's
 * drive-linked lowpass). */
void biquad_set_bandpass(Biquad *f, double freq_hz, double q, double sample_rate);
double biquad_process(Biquad *f, double x);

/* Tilt EQ -- a single "Tone" control that blends between bass-boost/
 * treble-cut and bass-cut/treble-boost around a center frequency, via
 * two complementary shelving filters summed. Common in real pedal/
 * preamp tone stacks (Klon-style, many boost/OD circuits) -- used here
 * as each pedal's Tone parameter, layered on top of that pedal's own
 * fixed characteristic coloration filter (which stays fixed, modeling
 * the circuit's inherent voicing; Tone is the adjustable control on
 * top of that, same relationship as on a real pedal). */
typedef struct {
    OnePole lowshelf;
    OnePole highshelf;
    double tone; /* 0.0 = bass boost/treble cut, 1.0 = opposite, 0.5 = flat */
} TiltEQ;

void tilteq_init(TiltEQ *t, double center_hz, double sample_rate);
double tilteq_process(TiltEQ *t, double x);

/* Moog-style 4-pole (24dB/oct) resonant lowpass ladder with input
 * drive/saturation. Structure follows the well-known Stilson/Smith
 * discrete approximation of the Moog transistor ladder (the reference
 * model reused across many open-source virtual-analog synths), with a
 * cubic soft-clip nonlinearity on the resonant feedback node (models
 * the ladder's inherent self-saturation) and a tanh() saturation on the
 * input stage for the explicit "Drive" parameter. Resonance can
 * approach self-oscillation at high settings, same as the real circuit. */
typedef struct {
    double stage[4];
    double delay[4];
    double p, k;       /* derived filter coefficients */
    double resonance;   /* compensated resonance amount */
    double drive;        /* input pre-gain, 1.0 = unity */
    double sample_rate;
} MoogLadder;

void moog_ladder_init(MoogLadder *f, double sample_rate);
/* resonance01/drive01: 0.0-1.0 knob-style inputs, internally scaled. */
void moog_ladder_set(MoogLadder *f, double cutoff_hz, double resonance01, double drive01);
double moog_ladder_process(MoogLadder *f, double x);

/* Resonant 2-pole lowpass building block shared by the Korg-style
 * filter pair below -- same ladder-filter DSP philosophy as MoogLadder
 * (cascaded one-pole stages + saturating resonant feedback) but with 2
 * stages instead of 4, giving a 12dB/oct slope closer to the real
 * MS-20's individual HPF/LPF stages than a full Moog ladder would. */
typedef struct {
    double stage[2];
    double delay[2];
    double p, k;
    double resonance;
    double drive;
    double sample_rate;
} Korg35LP;

void korg35lp_init(Korg35LP *f, double sample_rate);
void korg35lp_set(Korg35LP *f, double cutoff_hz, double resonance01, double drive01);
double korg35lp_process(Korg35LP *f, double x);

/* Resonant highpass companion -- derived as input minus its own
 * internal (independently resonant/saturating) lowpass core, a common
 * technique for getting a characterful resonant HP response from a
 * ladder-style core. Not a literal transcription of the real MS-20's
 * analog HPF topology (documented as a simplification, same as every
 * other pedal in this project), but captures the "distorts on its own"
 * self-saturating character the real unit is known for. */
typedef struct {
    Korg35LP core;
} Korg35HP;

void korg35hp_init(Korg35HP *f, double sample_rate);
void korg35hp_set(Korg35HP *f, double cutoff_hz, double resonance01, double drive01);
double korg35hp_process(Korg35HP *f, double x);

/* Oberheim SEM-style state-variable filter (Chamberlin topology, a
 * well-known and inherently stable structure). Unlike the Moog ladder
 * and Korg35 filters above, resonance here is deliberately kept short
 * of true self-oscillation even at 100% -- damping (`q`) is scaled to
 * stay well-controlled, so no "always 0 on randomize" safety rule is
 * needed for this one (verified via a dedicated worst-case test, same
 * rigor as the Moog/Korg ones). Lowpass output only (the topology also
 * gives simultaneous highpass/bandpass, not exposed here). */
typedef struct {
    double low, band;
    double f, q;
} SEMFilter;

void semfilter_init(SEMFilter *f);
void semfilter_set(SEMFilter *f, double cutoff_hz, double resonance01, double sample_rate);
double semfilter_process(SEMFilter *f, double x);

/* Polivoks-style resonant filter -- same 2-pole ladder-style structure
 * as Korg35LP, but with a harder clip (instead of a cubic soft-clip) on
 * the resonant node, giving the distinctly gritty/"growly" heavily
 * distorted character the real Russian synth's filter is known for. */
typedef struct {
    double stage[2];
    double delay[2];
    double p, k;
    double resonance;
    double drive;
} PolivoksFilter;

void polivoks_init(PolivoksFilter *f);
void polivoks_set(PolivoksFilter *f, double cutoff_hz, double resonance01, double drive01, double sample_rate);
double polivoks_process(PolivoksFilter *f, double x);

/* Envelope follower -- fast attack, slower release, standard asymmetric
 * one-pole peak detector. Drives the auto-wah filter sweep (Mu-Tron,
 * Cry Baby 535Q). */
typedef struct {
    double envelope;
    double attack_coeff;
    double release_coeff;
} EnvelopeFollower;

void envfollow_init(EnvelopeFollower *e, double attack_ms, double release_ms, double sample_rate);
double envfollow_process(EnvelopeFollower *e, double x);

/* Time-domain granular pitch shifter -- two read taps into a circular
 * buffer, each advancing at a rate offset from the write rate by the
 * target pitch ratio, permanently kept a half-window apart and
 * triangle-crossfaded so one fades in as the other fades out. This is
 * a well-established simple pitch-shifting technique (time-domain
 * dual-tap granular resampling), not FFT-based -- expect some grain
 * artifacts at extreme ratios, which is honestly in keeping with how a
 * real Whammy-style pedal sounds too. */
#define PITCHSHIFT_BUFFER_SIZE 4096
#define PITCHSHIFT_WINDOW 2048.0
typedef struct {
    double buffer[PITCHSHIFT_BUFFER_SIZE];
    int write_pos;
    double read_offset1;
    double read_offset2;
    double ratio; /* playback rate ratio, 2^(semitones/12) */
} PitchShifter;

void pitchshift_init(PitchShifter *ps);
void pitchshift_set_semitones(PitchShifter *ps, double semitones);
double pitchshift_process(PitchShifter *ps, double x);

/* Sample-and-hold decimator + linear bit-depth quantizer, used by LOFI.
 * Compact standalone version of the same decimation/quantization
 * technique used in the EMAX_FX project (a different Schwung module),
 * simplified since LOFI doesn't need the analog-modeled anti-aliasing/
 * reconstruction filter chain EMAX_FX has -- just the crush character. */
typedef struct {
    double held_value;
    double phase;
} SimpleDecimator;

void decimator_init(SimpleDecimator *d);
double decimator_process(SimpleDecimator *d, double x, double target_hz, double host_sample_rate);
double quantize_bits(double x, int bits);

/* Minimal xorshift noise generator -- used for TAPE's hiss. Same
 * algorithm family as the chain's own randomization RNG, just a
 * separate per-block instance so audio-rate noise doesn't perturb the
 * chain-randomization sequence. */
typedef struct {
    unsigned int state;
} SimpleNoise;

void noise_init(SimpleNoise *n, unsigned int seed);
double noise_next(SimpleNoise *n); /* returns -1.0..1.0 */

/* Three-colour noise generator for NOIZ. White is the raw SimpleNoise
 * above; pink uses Paul Kellet's well-known "economy" 3-pole
 * approximation (a widely reused simple/cheap pink noise filter, not a
 * theoretically exact -3dB/octave filter, but a standard reference
 * implementation for exactly this purpose); red/brown is a simple
 * leaky-integrator (heavily lowpassed white noise), rescaled back up
 * since integration attenuates amplitude a lot. */
typedef enum { NOISE_WHITE = 0, NOISE_PINK, NOISE_RED } NoiseColour;

typedef struct {
    SimpleNoise white;
    double pink_b0, pink_b1, pink_b2;
    double brown_state;
} NoiseGen;

void noisegen_init(NoiseGen *n, unsigned int seed);
double noisegen_process(NoiseGen *n, NoiseColour colour);

/* Broken 1/4" cable/jack simulation for CABL -- a small state machine
 * that randomly triggers brief crackle bursts or partial cutouts at
 * random intervals, plus a subtle constant noise floor even in its
 * "normal" state (a genuinely bad cable has some baseline character
 * even when "working"). IMPORTANT: cutouts are never full silence --
 * cutoutLevel is randomized but bounded well above 0 each time, per
 * spec ("never fully off, just cutting out and filtering part or all
 * of the signal like a real malfunctioning cable would"). */
typedef enum { CABLE_NORMAL = 0, CABLE_CRACKLE, CABLE_CUTOUT } CableState;

typedef struct {
    CableState state;
    int stateSamplesRemaining;
    int eventCountdown;
    double cutoutLevel; /* how much signal survives during a cutout, never 0 */
    double humLevel;      /* 0.0-0.10 (0-10%), randomized once per load -- simulates ground-loop/electrical interference */
    double humPhase;
    SimpleNoise noise;
    unsigned int rngState;
} CableSim;

void cablesim_init(CableSim *c, unsigned int seed);
/* severity01: 0.0-1.0, scales how often/how harsh the glitch events
 * are (the CABL knob's meaning). */
double cablesim_process(CableSim *c, double x, double severity01, double sample_rate);

/* Look-ahead, stereo-linked "brickwall" safety limiter for the chain's
 * final output. Not part of DistroyChain itself (which is per-channel
 * mono) -- this operates on L+R together and belongs in the plugin
 * wrapper (distroy_audio_fx.c / PluginProcessor.cpp), called once per
 * sample after both channels have run through their own chains.
 *
 * Design: delays the signal by a short lookahead window
 * (LIMITER_LOOKAHEAD_SAMPLES, ~2.9ms at 44100Hz) while scanning that
 * same window for its true peak (max abs(L,R) across the whole
 * window, not just the instantaneous sample) -- this means gain
 * reduction can be computed and smoothed toward BEFORE a peak actually
 * reaches the output, avoiding the harsh instant-clip character a
 * naive no-lookahead limiter or hard clamp alone would have (which is
 * what this project relied on previously). Attack is effectively
 * instant (safe specifically because the lookahead already "saw the
 * peak coming"); release is a smooth, program-dependent ramp back
 * toward unity gain. A final hard clamp to the ceiling remains as an
 * absolute backstop regardless of what the smoothed gain path does --
 * this is the actual safety guarantee ("so we don't hurt people"),
 * with the lookahead/smoothing on top purely for transparency. */
#define LIMITER_LOOKAHEAD_SAMPLES 128

typedef struct {
    double delay_l[LIMITER_LOOKAHEAD_SAMPLES];
    double delay_r[LIMITER_LOOKAHEAD_SAMPLES];
    double peak_window[LIMITER_LOOKAHEAD_SAMPLES];
    int write_pos;
    double gain; /* current smoothed gain, <= 1.0 */
    double ceiling; /* linear, e.g. 0.891 for -1dBFS */
    double release_coeff;
} BrickwallLimiter;

void brickwall_limiter_init(BrickwallLimiter *lim, double ceiling_db, double release_ms, double sample_rate);
/* Processes one stereo sample in place. */
void brickwall_limiter_process(BrickwallLimiter *lim, double *l, double *r);

/* Battery-starve simulator -- for the VST3's power icon (9V battery)
 * feature only, not used by the Move version (which has no equivalent
 * UI to expose this). Models a guitar pedal's battery dying: reduced
 * output level, increasing "sag"/compression as headroom drops,
 * increasing amplitude wobble (unstable supply voltage), and increasing
 * noise floor -- all scaling with `amount`. At amount=1.0 this reduces
 * signal to near-total loss (deliberately, per spec -- "almost
 * disconnect power" -- unlike CABL, this one is allowed to approach
 * full silence). Stereo-linked (call once per stereo sample, apply the
 * SAME instance's output to both channels' already-mixed signal, or use
 * two instances with the same `amount` if you want independent L/R
 * noise -- the reference wrapper implementation uses one instance
 * driving both channels for a single "dying battery" character rather
 * than two independently-dying batteries). */
typedef struct {
    double amount;      /* 0.0 = normal, 1.0 = almost dead */
    double lfoPhase;
    SimpleNoise noise;
} PowerStarve;

void powerstarve_init(PowerStarve *ps, unsigned int seed);
void powerstarve_set_amount(PowerStarve *ps, double amount01);
double powerstarve_process(PowerStarve *ps, double x, double sample_rate);

/* A single pedal slot: type + primary knob (0.0-1.0, meaning depends on
 * type's knob_mode) + per-pedal sub-parameters (Drive/Tone/Level) +
 * internal filter state.
 *
 * SUB-PARAMETERS (Drive, Tone, Level): every real pedal has more than
 * one control -- a Tubescreamer has Overdrive/Tone/Level, a Big Muff
 * has Sustain/Tone/Volume, etc. DISTROY only has one physical knob per
 * slot (already spoken for by the primary knob-controlled parameter),
 * so these sub-parameters are randomized once per chain load (see
 * distroy_chain_randomize_all()) rather than knob-controlled -- live
 * submenu editing of these is a separate, not-yet-implemented UI
 * question (see README).
 *
 * For GAIN/CUTOFF/SENS-mode types, the primary knob IS the main
 * control, so sub_drive is unused by the mixing logic (harmless to
 * still randomize). For WET_DRY-mode types, sub_drive replaces what
 * used to be a fixed constant -- genuinely randomized per load.
 *
 * REPURPOSED sub-parameters (type-specific meaning, not Drive/Tone):
 *   Moog Ladder, Korg MS-20: sub_tone = Resonance (always 0 on
 *     randomize -- see distroy_chain_randomize_all(), high resonance
 *     on these can howl badly)
 *   LOFI: sub_drive = bit depth encode (1-15 bits), sub_tone = sample
 *     rate encode (100-10000 Hz) -- see type_process()
 *   WHAM: sub_drive = semitone shift encode (weighted toward +-12,
 *     never 0) -- see type_process() */
typedef struct {
    DistroyType type;
    double knob;        /* 0.0-1.0, meaning depends on type's knob_mode */
    double sub_drive;   /* 0.0-1.0, internal drive for WET_DRY-mode types */
    double sub_tone;     /* 0.0-1.0, feeds TiltEQ */
    double sub_level;    /* 0.0-1.0, mapped to an output trim range */
    OnePole dc_block;
    OnePole color_lp;    /* used by types with a lowpass coloration (Rat) */
    OnePole color_hs;    /* used by types with a highshelf/presence lift */
    Biquad color_peak;   /* used by types with a peaking/notch coloration */
    TiltEQ tone_stage;
    MoogLadder moog;      /* used only by DISTROY_MOOG_LADDER */
    Korg35HP korg_hp;      /* used only by DISTROY_KORG_MS20 */
    Korg35LP korg_lp;      /* used only by DISTROY_KORG_MS20 */
    EnvelopeFollower env;   /* used by DISTROY_MUTRON, DISTROY_CRYBABY */
    Biquad wah_filter;      /* used by DISTROY_MUTRON, DISTROY_CRYBABY */
    PitchShifter pitch;      /* used only by DISTROY_WHAM */
    SimpleDecimator decim;   /* used only by DISTROY_LOFI */
    SimpleNoise noise;        /* used only by DISTROY_TAPE and DISTROY_TUBE */
    NoiseGen noisegen;         /* used only by DISTROY_NOIZ */
    CableSim cable;             /* used only by DISTROY_CABL */
    SEMFilter sem;               /* used only by DISTROY_SEM */
    PolivoksFilter polivoks;      /* used only by DISTROY_POLIVOKS */
    double sample_rate;
} DistroyBlock;

void distroy_block_init(DistroyBlock *b, DistroyType type, double sample_rate);
void distroy_block_set_type(DistroyBlock *b, DistroyType type);
double distroy_block_process(DistroyBlock *b, double x);

/* Current envelope-follower level (0.0-1.0-ish, not hard-clamped) for a
 * block -- meaningful only for auto-wah types (Mu-Tron, Cry Baby),
 * which are the only ones that use the `env` field. For any other
 * type this just returns whatever stale/zero value happens to be in
 * that unused struct field -- callers should check the block's type
 * before treating this as meaningful. Exists purely for UI
 * visualization (e.g. showing the incoming signal that's triggering
 * the auto-wah's sweep), not used anywhere in the DSP signal path
 * itself. */
double distroy_block_get_envelope_level(const DistroyBlock *b);

/* The full 8-slot chain. Signal direction is now toggleable (default
 * LEFT TO RIGHT: slot 0 processes first, slot 7 processes last) -- see
 * the `reverse` field and distroy_chain_process(). Slot-to-knob mapping
 * never changes (knob 1 is always slot 0); only which slot's effect
 * gets applied to the signal FIRST changes with direction. */
#define DISTROY_NUM_SLOTS 8

typedef struct {
    DistroyBlock slots[DISTROY_NUM_SLOTS];
    double sample_rate;
    int reverse; /* 0 = left-to-right (default), 1 = right-to-left */
} DistroyChain;

void distroy_chain_init(DistroyChain *c, double sample_rate);
/* Fills all 8 slots with randomly selected (non-necessarily-distinct)
 * pedal types -- called once on instantiation per the project spec.
 * seed: caller-supplied seed (e.g. derived from time) for reproducible
 * testing. */
void distroy_chain_randomize(DistroyChain *c, unsigned int seed);
/* Same as above, but also randomizes each slot's knob value AND its
 * sub-parameters (Drive/Tone/Level) (0.0-1.0 each). Used for the full
 * "randomize everything" behavior (new chain + new knob + new
 * sub-params), both on instantiation and on-demand via the RANDOMIZE
 * menu action. */
void distroy_chain_randomize_all(DistroyChain *c, unsigned int seed);
double distroy_chain_process(DistroyChain *c, double x);

/* Mode label for display, e.g. "GAIN" or "MIX". */
const char* distroy_knob_mode_label(DistroyKnobMode mode);

#endif /* DISTROY_DSP_H */
