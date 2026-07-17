#include "distroy_dsp.h"
#include <math.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------
 * Pedal type metadata.
 *
 * KNOB MODE DESIGN NOTE: the project spec says knobs default to a
 * "wet/dry amount" parameter, with "some distortions" using Gain
 * instead. I've assigned WET_DRY to the pedals that are commonly used
 * as blended boosts/texture in modern pedalboards (Boss OD, Tubescreamer,
 * Sansamp, Rat, Geiger Counter), and GAIN to the classic "always fully
 * driven" pedals where drive amount itself is the defining character
 * (Fuzz, Metal Zone, Big Muff). This split wasn't fully specified in the
 * request, so it's my interpretation -- easy to rebalance later if a
 * different split is wanted.
 * ------------------------------------------------------------------- */
static const DistroyTypeInfo kTypeInfo[DISTROY_TYPE_COUNT] = {
    [DISTROY_BOSS_OD]      = { "Boss OD",       "OD",     DISTROY_KNOB_WET_DRY },
    [DISTROY_FUZZ]         = { "Fuzz",          "FUZZ",   DISTROY_KNOB_GAIN },
    [DISTROY_METAL]        = { "Metal",         "METAL",  DISTROY_KNOB_GAIN },
    [DISTROY_TUBESCREAMER] = { "Tubescreamer",  "TS9",    DISTROY_KNOB_WET_DRY },
    [DISTROY_BIG_MUFF]     = { "Big Muff",      "MUFF",   DISTROY_KNOB_GAIN },
    [DISTROY_SANSAMP]      = { "Sansamp",       "SANS",   DISTROY_KNOB_WET_DRY },
    [DISTROY_RAT]          = { "Rat",           "RAT",    DISTROY_KNOB_WET_DRY },
    [DISTROY_GEIGER_COUNTER] = { "Geiger Counter", "GEIGER", DISTROY_KNOB_WET_DRY },
};

const DistroyTypeInfo* distroy_type_info(DistroyType type) {
    if (type < 0 || type >= DISTROY_TYPE_COUNT) return &kTypeInfo[0];
    return &kTypeInfo[type];
}

const char* distroy_knob_mode_label(DistroyKnobMode mode) {
    return mode == DISTROY_KNOB_GAIN ? "GAIN" : "MIX";
}

/* Fixed internal drive used for WET_DRY-mode pedals, where the knob
 * controls blend rather than drive amount. */
#define NOMINAL_DRIVE 0.6

/* ---------------------------------------------------------------------
 * Filter helpers
 * ------------------------------------------------------------------- */

void onepole_set_lowpass(OnePole *f, double cutoff_hz, double sample_rate) {
    double x = exp(-2.0 * M_PI * cutoff_hz / sample_rate);
    f->b0 = 1.0 - x;
    f->b1 = 0.0;
    f->a1 = -x;
}

void onepole_set_highpass(OnePole *f, double cutoff_hz, double sample_rate) {
    double x = exp(-2.0 * M_PI * cutoff_hz / sample_rate);
    f->b0 = (1.0 + x) / 2.0;
    f->b1 = -(1.0 + x) / 2.0;
    f->a1 = -x;
}

double onepole_process(OnePole *f, double x) {
    double y = f->b0 * x + f->b1 * f->x1 - f->a1 * f->y1;
    f->x1 = x;
    f->y1 = y;
    return y;
}

void biquad_set_peaking(Biquad *f, double freq_hz, double q, double gain_db, double sample_rate) {
    double A = pow(10.0, gain_db / 40.0);
    double w0 = 2.0 * M_PI * freq_hz / sample_rate;
    double alpha = sin(w0) / (2.0 * q);
    double cosw0 = cos(w0);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * cosw0;
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha / A;

    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
}

double biquad_process(Biquad *f, double x) {
    double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

/* ---------------------------------------------------------------------
 * Waveshaping primitives
 * ------------------------------------------------------------------- */

static double clampd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/* Symmetric soft clip, normalized so output doesn't blow past unity as
 * gain increases. */
static double soft_clip(double x, double gain) {
    if (gain < 1e-6) return x;
    double norm = tanh(gain);
    if (norm < 1e-9) return x;
    return tanh(gain * x) / norm;
}

/* Asymmetric soft clip -- different curve above/below zero. Used for
 * germanium-fuzz and diode-asymmetry character. DC offset this
 * introduces is removed downstream by each block's dc_block filter. */
static double asym_soft_clip(double x, double pos_k, double neg_k) {
    if (x >= 0.0) {
        double norm = tanh(pos_k);
        return norm > 1e-9 ? tanh(pos_k * x) / norm : x;
    } else {
        double norm = tanh(neg_k);
        return norm > 1e-9 ? tanh(neg_k * x) / norm : x;
    }
}

/* Hard clip -- flatter "top" than tanh, closer to op-amp/diode clipping
 * character (Rat, Metal Zone). */
static double hard_clip(double x, double gain, double threshold) {
    double y = clampd(gain * x, -threshold, threshold);
    return y / threshold;
}

/* Coarse quantization -- used sparingly for Geiger Counter's
 * unstable/glitchy character at high drive. */
static double quantize(double x, double levels) {
    if (levels < 2.0) return x;
    return round(x * levels) / levels;
}

/* ---------------------------------------------------------------------
 * Block (single pedal slot)
 * ------------------------------------------------------------------- */

void distroy_block_init(DistroyBlock *b, DistroyType type, double sample_rate) {
    b->sample_rate = sample_rate;
    b->knob = 0.5;
    b->dc_block = (OnePole){0};
    onepole_set_highpass(&b->dc_block, 15.0, sample_rate);
    b->color_lp = (OnePole){0};
    b->color_hs = (OnePole){0};
    b->color_peak = (Biquad){0};
    distroy_block_set_type(b, type);
}

void distroy_block_set_type(DistroyBlock *b, DistroyType type) {
    b->type = type;
    double sr = b->sample_rate;

    switch (type) {
        case DISTROY_BOSS_OD:
            onepole_set_highpass(&b->color_hs, 2000.0, sr); /* presence lift helper */
            break;
        case DISTROY_FUZZ:
            onepole_set_highpass(&b->color_hs, 150.0, sr); /* bass-thinning helper */
            break;
        case DISTROY_METAL:
            biquad_set_peaking(&b->color_peak, 600.0, 1.2, -7.0, sr); /* scooped mids */
            break;
        case DISTROY_TUBESCREAMER:
            biquad_set_peaking(&b->color_peak, 720.0, 0.7, 6.0, sr); /* mid hump, pre-clip */
            break;
        case DISTROY_BIG_MUFF:
            biquad_set_peaking(&b->color_peak, 500.0, 1.0, -3.0, sr); /* mild scoop */
            break;
        case DISTROY_SANSAMP:
            onepole_set_lowpass(&b->color_lp, 400.0, sr); /* warmth-boost helper */
            break;
        case DISTROY_RAT:
            onepole_set_lowpass(&b->color_lp, 5000.0, sr); /* "Filter" darkening, retuned per-sample below */
            break;
        case DISTROY_GEIGER_COUNTER:
            /* no fixed filter -- character comes from asym clip + quantize */
            break;
        default:
            break;
    }
}

/* Per-type characteristic processing at a given drive amount (0.0-1.0).
 * Returns the "wet" (fully processed) signal; mixing with dry (for
 * WET_DRY-mode types) happens in distroy_block_process(). */
static double type_process(DistroyBlock *b, double x, double drive) {
    switch (b->type) {
        case DISTROY_BOSS_OD: {
            double gain = 1.0 + drive * 9.0;
            double y = soft_clip(x, gain);
            /* presence lift: blend a little high-passed signal back in */
            double hp = onepole_process(&b->color_hs, y);
            return y + 0.15 * hp;
        }
        case DISTROY_FUZZ: {
            double g = 3.0 + drive * 27.0;
            double y = asym_soft_clip(g * x, 3.0 + drive * 2.0, 1.5 + drive * 1.0);
            /* thin the bass a bit for classic fuzz character */
            double hp = onepole_process(&b->color_hs, y);
            return y * 0.6 + hp * 0.4;
        }
        case DISTROY_METAL: {
            double gain = 5.0 + drive * 35.0;
            double y = hard_clip(x, gain, 1.0);
            y = hard_clip(y, 2.0, 1.0); /* second cascaded stage */
            return biquad_process(&b->color_peak, y);
        }
        case DISTROY_TUBESCREAMER: {
            double pre = biquad_process(&b->color_peak, x); /* mid hump before clip */
            double g = 1.0 + drive * 6.0;
            return asym_soft_clip(g * pre, 2.0 + drive * 3.0, 1.5 + drive * 2.0);
        }
        case DISTROY_BIG_MUFF: {
            double gain = 3.0 + drive * 20.0;
            double y = hard_clip(x, gain, 1.0);
            y = hard_clip(y, 1.8, 1.0); /* second cascaded stage -- sustain character */
            return biquad_process(&b->color_peak, y);
        }
        case DISTROY_SANSAMP: {
            double gain = 2.0 + drive * 10.0;
            double sat = soft_clip(x, gain);
            double warm = onepole_process(&b->color_lp, x);
            return sat * 0.7 + warm * 0.3; /* amp-in-a-box blend character */
        }
        case DISTROY_RAT: {
            double gain = 5.0 + drive * 40.0;
            double y = hard_clip(x, gain, 1.0);
            /* darken more as drive increases -- Rat's Filter interaction */
            double cutoff = 8000.0 - drive * 6000.0;
            onepole_set_lowpass(&b->color_lp, cutoff, b->sample_rate);
            return onepole_process(&b->color_lp, y);
        }
        case DISTROY_GEIGER_COUNTER: {
            double gain = 8.0 + drive * 60.0;
            double y = asym_soft_clip(gain * x, 4.0 + drive * 4.0, 1.0 + drive * 6.0);
            double levels = 64.0 - drive * 48.0; /* more crunch at higher drive */
            return quantize(y, levels);
        }
        default:
            return x;
    }
}

double distroy_block_process(DistroyBlock *b, double x) {
    const DistroyTypeInfo *info = distroy_type_info(b->type);
    double wet, out;

    if (info->knob_mode == DISTROY_KNOB_GAIN) {
        wet = type_process(b, x, b->knob);
        out = wet;
    } else {
        wet = type_process(b, x, NOMINAL_DRIVE);
        out = x * (1.0 - b->knob) + wet * b->knob;
    }

    return onepole_process(&b->dc_block, out);
}

/* ---------------------------------------------------------------------
 * Chain (8 slots)
 * ------------------------------------------------------------------- */

void distroy_chain_init(DistroyChain *c, double sample_rate) {
    c->sample_rate = sample_rate;
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        distroy_block_init(&c->slots[i], DISTROY_BOSS_OD, sample_rate);
    }
}

static unsigned int xorshift_next(unsigned int *state) {
    unsigned int s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return s;
}

void distroy_chain_randomize(DistroyChain *c, unsigned int seed) {
    unsigned int state = seed;
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        DistroyType t = (DistroyType)(xorshift_next(&state) % DISTROY_TYPE_COUNT);
        distroy_block_set_type(&c->slots[i], t);
    }
}

void distroy_chain_randomize_all(DistroyChain *c, unsigned int seed) {
    unsigned int state = seed;
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        DistroyType t = (DistroyType)(xorshift_next(&state) % DISTROY_TYPE_COUNT);
        distroy_block_set_type(&c->slots[i], t);
        /* xorshift_next returns a full-range unsigned int -- scale to
         * 0.0-1.0 */
        c->slots[i].knob = (double)xorshift_next(&state) / (double)UINT32_MAX;
    }
}

double distroy_chain_process(DistroyChain *c, double x) {
    double y = x;
    /* Right-to-left signal flow: slot 7 processes first, slot 0 last. */
    for (int i = DISTROY_NUM_SLOTS - 1; i >= 0; i--) {
        y = distroy_block_process(&c->slots[i], y);
    }
    return y;
}
