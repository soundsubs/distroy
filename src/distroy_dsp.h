#ifndef DISTROY_DSP_H
#define DISTROY_DSP_H

/* DISTROY -- an 8-slot serial chain of modeled distortion/overdrive
 * pedals AND resonant filters for Schwung (Ableton Move). Signal flows
 * right to left across the 8 knob-mapped slots (slot index 7 processes
 * first, slot index 0 processes last -- see distroy_chain_process()).
 *
 * Each pedal/filter TYPE is a fixed, characteristic waveshaper/filter
 * plus a fixed (non-knob-controlled) coloration modeling that unit's
 * tonal signature. Per the project spec, each slot exposes exactly ONE
 * knob-controlled parameter: GAIN (drive amount), WET_DRY (blend), or
 * for the two filter types, CUTOFF (filter frequency) -- see
 * DISTROY_KNOB_MODE below and distroy_type_info().
 *
 * KNOWN SIMPLIFICATION: these are characteristic circuit-topology
 * approximations (soft/hard clipping curves + coloration filters tuned
 * by ear/reference to each pedal's known character), not
 * component-level SPICE-accurate models. WMD Geiger Counter in
 * particular is modeled only for its aggressive fuzz/clipping
 * character -- its sequencer/gate/pattern features are out of scope
 * for a knob-controlled audio effect and are not modeled here. The
 * Moog ladder filter uses the well-known Stilson/Smith discrete
 * approximation (see MoogLadder below); the Korg-style filter pair is
 * a characterful approximation of the MS-20's resonant/self-saturating
 * HPF->LPF behavior, not a literal transcription of its analog
 * topology (see Korg35HP/Korg35LP below).
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
    DISTROY_TYPE_COUNT
} DistroyType;

typedef enum {
    DISTROY_KNOB_GAIN = 0,
    DISTROY_KNOB_WET_DRY,
    DISTROY_KNOB_CUTOFF
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

/* A single pedal slot: type + primary knob (0.0-1.0, meaning depends on
 * type's knob_mode) + per-pedal sub-parameters (Drive/Tone/Level) +
 * internal filter state.
 *
 * SUB-PARAMETERS (Drive, Tone, Level): every real pedal has more than
 * one control -- a Tubescreamer has Overdrive/Tone/Level, a Big Muff
 * has Sustain/Tone/Volume, etc. DISTROY only has one physical knob per
 * slot (already spoken for by the primary GAIN/WET_DRY control), so
 * these sub-parameters are randomized once per chain load (see
 * distroy_chain_randomize_all()) rather than knob-controlled -- live
 * submenu editing of these is a separate, not-yet-implemented UI
 * question (see README).
 *
 * For GAIN-mode types, the primary knob IS the drive amount, so
 * sub_drive is unused (harmless to still randomize). For WET_DRY-mode
 * types, sub_drive replaces what used to be a fixed NOMINAL_DRIVE
 * constant -- now genuinely randomized per load, giving real variety
 * even among same-type pedals across different sessions. */
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
