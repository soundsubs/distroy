#ifndef DISTROY_DSP_H
#define DISTROY_DSP_H

/* DISTROY -- an 8-slot serial chain of modeled distortion/overdrive
 * pedals, filters, and other effects for Schwung (Ableton Move). Signal
 * flows right to left across the 8 knob-mapped slots (slot index 7
 * processes first, slot index 0 processes last -- see
 * distroy_chain_process()).
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
    DISTROY_TYPE_COUNT
} DistroyType;

typedef enum {
    DISTROY_KNOB_GAIN = 0,
    DISTROY_KNOB_WET_DRY,
    DISTROY_KNOB_CUTOFF,
    DISTROY_KNOB_SENS,     /* auto-wah envelope sensitivity/depth */
    DISTROY_KNOB_SIZE      /* speaker cabinet size sweep */
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
    SimpleNoise noise;        /* used only by DISTROY_TAPE */
    double sample_rate;
} DistroyBlock;

void distroy_block_init(DistroyBlock *b, DistroyType type, double sample_rate);
void distroy_block_set_type(DistroyBlock *b, DistroyType type);
double distroy_block_process(DistroyBlock *b, double x);

/* The full 8-slot chain. slots[7] processes first, slots[0] processes
 * last (right-to-left signal flow per the project spec). */
#define DISTROY_NUM_SLOTS 8

typedef struct {
    DistroyBlock slots[DISTROY_NUM_SLOTS];
    double sample_rate;
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
