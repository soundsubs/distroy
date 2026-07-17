#ifndef DISTROY_DSP_H
#define DISTROY_DSP_H

/* DISTROY -- an 8-slot serial chain of modeled distortion/overdrive
 * pedals for Schwung (Ableton Move). Signal flows right to left across
 * the 8 knob-mapped slots (slot index 7 processes first, slot index 0
 * processes last -- see distroy_chain_process()).
 *
 * Each pedal TYPE is a fixed, characteristic waveshaper plus a fixed
 * (non-knob-controlled) coloration filter modeling that pedal's tonal
 * signature -- e.g. Tubescreamer's mid hump, Metal Zone's scooped mids,
 * Rat's darkening low-pass. Per the project spec, each slot exposes
 * exactly ONE knob-controlled parameter: either GAIN (drive amount) or
 * WET_DRY (blend), depending on which mode that pedal type is assigned
 * -- see DISTROY_KNOB_MODE below and distroy_type_info().
 *
 * KNOWN SIMPLIFICATION: these are characteristic circuit-topology
 * approximations (soft/hard clipping curves + coloration filters tuned
 * by ear/reference to each pedal's known character), not
 * component-level SPICE-accurate models. WMD Geiger Counter in
 * particular is modeled only for its aggressive fuzz/clipping
 * character -- its sequencer/gate/pattern features are out of scope
 * for a knob-controlled audio effect and are not modeled here.
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
    DISTROY_TYPE_COUNT
} DistroyType;

typedef enum {
    DISTROY_KNOB_GAIN = 0,
    DISTROY_KNOB_WET_DRY
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

/* A single pedal slot: type + knob value (0.0-1.0) + its internal filter
 * state. */
typedef struct {
    DistroyType type;
    double knob;        /* 0.0-1.0, meaning depends on type's knob_mode */
    OnePole dc_block;
    OnePole color_lp;    /* used by types with a lowpass coloration (Rat) */
    OnePole color_hs;    /* used by types with a highshelf/presence lift */
    Biquad color_peak;   /* used by types with a peaking/notch coloration */
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
/* Same as above, but also randomizes each slot's knob value (0.0-1.0).
 * Used for the full "randomize everything" behavior (new chain +
 * new knob values), both on instantiation and on-demand via the
 * RANDOMIZE menu action. */
void distroy_chain_randomize_all(DistroyChain *c, unsigned int seed);
double distroy_chain_process(DistroyChain *c, double x);

/* Mode label for display, e.g. "GAIN" or "MIX". */
const char* distroy_knob_mode_label(DistroyKnobMode mode);

#endif /* DISTROY_DSP_H */
