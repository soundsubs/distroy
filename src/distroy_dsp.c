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
    [DISTROY_MOOG_LADDER]  = { "Moog Ladder",   "MOOG",   DISTROY_KNOB_CUTOFF },
    [DISTROY_KORG_MS20]    = { "Korg MS-20",    "MS20",   DISTROY_KNOB_CUTOFF },
};

const DistroyTypeInfo* distroy_type_info(DistroyType type) {
    if (type < 0 || type >= DISTROY_TYPE_COUNT) return &kTypeInfo[0];
    return &kTypeInfo[type];
}

const char* distroy_knob_mode_label(DistroyKnobMode mode) {
    switch (mode) {
        case DISTROY_KNOB_GAIN: return "GAIN";
        case DISTROY_KNOB_CUTOFF: return "CUTOFF";
        default: return "MIX";
    }
}

/* Forward declaration -- defined below in the waveshaping primitives
 * section, but needed earlier by the Moog/Korg35 filter setters. */
static double clampd(double x, double lo, double hi);

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

void tilteq_init(TiltEQ *t, double center_hz, double sample_rate) {
    t->tone = 0.5;
    onepole_set_lowpass(&t->lowshelf, center_hz, sample_rate);
    onepole_set_highpass(&t->highshelf, center_hz, sample_rate);
}

double tilteq_process(TiltEQ *t, double x) {
    /* Classic tilt EQ: sum the low-passed and high-passed signal with
     * complementary gains that shift with "tone". At tone=0.5 both
     * gains are 1.0 (flat). Range chosen to be characterful without
     * being extreme (+-4dB-ish). */
    double lo = onepole_process(&t->lowshelf, x);
    double hi = onepole_process(&t->highshelf, x);
    double tilt = (t->tone - 0.5) * 2.0; /* -1.0 .. 1.0 */
    double lo_gain = 1.0 - tilt * 0.6;
    double hi_gain = 1.0 + tilt * 0.6;
    return lo * lo_gain + hi * hi_gain;
}

/* ---------------------------------------------------------------------
 * Moog ladder filter -- Stilson/Smith discrete approximation.
 * ------------------------------------------------------------------- */

void moog_ladder_init(MoogLadder *f, double sample_rate) {
    *f = (MoogLadder){0};
    f->sample_rate = sample_rate;
    f->drive = 1.0;
    moog_ladder_set(f, 1000.0, 0.0, 0.0);
}

void moog_ladder_set(MoogLadder *f, double cutoff_hz, double resonance01, double drive01) {
    double fc = clampd(cutoff_hz / (f->sample_rate * 0.5), 0.0001, 0.99);
    f->p = fc * (1.8 - 0.8 * fc);
    f->k = 2.0 * sin(fc * M_PI * 0.5) - 1.0;
    double t1 = (1.0 - f->p) * 1.386249;
    double t2 = 12.0 + t1 * t1;
    /* resonance01 0-1 -> scaled so it gets characterful without
     * crossing the classic self-oscillation threshold (~4.0 for this
     * formula) -- 3.5 leaves headroom given the cubic soft-clip alone
     * isn't a strong enough damper right at the boundary (verified via
     * make test: 4.2 diverged to inf/-nan at cutoff=8kHz, resonance=0.5
     * default -- see also the hard clamp below as a second safety net). */
    f->resonance = resonance01 * 3.5 * (t2 + 6.0 * t1) / (t2 - 6.0 * t1);
    f->drive = 1.0 + drive01 * 11.0; /* 1x - 12x input pre-gain */
}

double moog_ladder_process(MoogLadder *f, double x) {
    x *= f->drive;
    x = tanh(x); /* input saturation stage -- the explicit "Drive" character */

    double input = x - f->resonance * f->stage[3];
    f->stage[0] = input * f->p + f->delay[0] * f->p - f->k * f->stage[0];
    f->stage[1] = f->stage[0] * f->p + f->delay[1] * f->p - f->k * f->stage[1];
    f->stage[2] = f->stage[1] * f->p + f->delay[2] * f->p - f->k * f->stage[2];
    f->stage[3] = f->stage[2] * f->p + f->delay[3] * f->p - f->k * f->stage[3];
    /* cubic soft-clip on the resonant node -- models the ladder's own
     * inherent saturation, prevents runaway self-oscillation blowup */
    f->stage[3] -= (f->stage[3] * f->stage[3] * f->stage[3]) / 6.0;

    /* Hard safety clamp: the cubic term above is a soft damper, not a
     * hard limit -- near the resonance/cutoff combination that
     * approaches self-oscillation it can still diverge over many
     * samples. This is a standard second safety net in production
     * ladder filter implementations; the clamp range is well outside
     * normal operating levels so it doesn't audibly affect typical use. */
    for (int i = 0; i < 4; i++) {
        f->stage[i] = clampd(f->stage[i], -8.0, 8.0);
    }

    f->delay[0] = input;
    f->delay[1] = f->stage[0];
    f->delay[2] = f->stage[1];
    f->delay[3] = f->stage[2];

    return f->stage[3];
}

/* ---------------------------------------------------------------------
 * Korg-style resonant filter pair (MS-20 character)
 * ------------------------------------------------------------------- */

void korg35lp_init(Korg35LP *f, double sample_rate) {
    *f = (Korg35LP){0};
    f->sample_rate = sample_rate;
    f->drive = 1.0;
    korg35lp_set(f, 1000.0, 0.0, 0.0);
}

void korg35lp_set(Korg35LP *f, double cutoff_hz, double resonance01, double drive01) {
    double fc = clampd(cutoff_hz / (f->sample_rate * 0.5), 0.0001, 0.99);
    f->p = fc * (1.8 - 0.8 * fc);
    f->k = 2.0 * sin(fc * M_PI * 0.5) - 1.0;
    double t1 = (1.0 - f->p) * 1.386249;
    double t2 = 12.0 + t1 * t1;
    /* Only 2 poles instead of 4 -- self-oscillation threshold is
     * different from the Moog ladder. Same conservative headroom
     * reasoning as MoogLadder (see its comment) applied here too. */
    f->resonance = resonance01 * 2.6 * (t2 + 6.0 * t1) / (t2 - 6.0 * t1);
    f->drive = 1.0 + drive01 * 9.0;
}

double korg35lp_process(Korg35LP *f, double x) {
    x *= f->drive;
    x = tanh(x);

    double input = x - f->resonance * f->stage[1];
    f->stage[0] = input * f->p + f->delay[0] * f->p - f->k * f->stage[0];
    f->stage[1] = f->stage[0] * f->p + f->delay[1] * f->p - f->k * f->stage[1];
    /* saturate the resonant node -- this is the "distorts on its own"
     * self-saturating character the MS-20 is known for */
    f->stage[1] -= (f->stage[1] * f->stage[1] * f->stage[1]) / 6.0;

    /* Hard safety clamp -- same reasoning as MoogLadder's, second
     * safety net beyond the soft cubic damper. */
    f->stage[0] = clampd(f->stage[0], -8.0, 8.0);
    f->stage[1] = clampd(f->stage[1], -8.0, 8.0);

    f->delay[0] = input;
    f->delay[1] = f->stage[0];

    return f->stage[1];
}

void korg35hp_init(Korg35HP *f, double sample_rate) {
    korg35lp_init(&f->core, sample_rate);
}

void korg35hp_set(Korg35HP *f, double cutoff_hz, double resonance01, double drive01) {
    korg35lp_set(&f->core, cutoff_hz, resonance01, drive01);
}

double korg35hp_process(Korg35HP *f, double x) {
    /* Resonant highpass derived as input minus its own independently
     * resonant/saturating lowpass core -- see header comment for why
     * this isn't a literal transcription of the real analog HPF. */
    double lp = korg35lp_process(&f->core, x);
    return x - lp;
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
    b->sub_drive = 0.6;
    b->sub_tone = 0.5;
    b->sub_level = 0.5;
    b->dc_block = (OnePole){0};
    onepole_set_highpass(&b->dc_block, 15.0, sample_rate);
    b->color_lp = (OnePole){0};
    b->color_hs = (OnePole){0};
    b->color_peak = (Biquad){0};
    b->tone_stage = (TiltEQ){0};
    distroy_block_set_type(b, type);
}

/* Tilt EQ center frequency per pedal -- picked to suit each pedal's
 * typical tonal range (a fuzz's tilt sits lower than a treble-forward
 * pedal like Rat/Tubescreamer). */
static double tilt_center_hz(DistroyType type) {
    switch (type) {
        case DISTROY_BOSS_OD:      return 1000.0;
        case DISTROY_FUZZ:         return 700.0;
        case DISTROY_METAL:        return 900.0;
        case DISTROY_TUBESCREAMER: return 1200.0;
        case DISTROY_BIG_MUFF:     return 800.0;
        case DISTROY_SANSAMP:      return 1000.0;
        case DISTROY_RAT:          return 1500.0;
        case DISTROY_GEIGER_COUNTER: return 900.0;
        default:                   return 1000.0;
    }
}

void distroy_block_set_type(DistroyBlock *b, DistroyType type) {
    b->type = type;
    double sr = b->sample_rate;

    tilteq_init(&b->tone_stage, tilt_center_hz(type), sr);
    b->tone_stage.tone = b->sub_tone;

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
        case DISTROY_MOOG_LADDER:
            moog_ladder_init(&b->moog, sr);
            break;
        case DISTROY_KORG_MS20:
            korg35hp_init(&b->korg_hp, sr);
            korg35lp_init(&b->korg_lp, sr);
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
        case DISTROY_MOOG_LADDER: {
            /* For CUTOFF-mode types, the "drive" argument here is
             * actually the primary knob value (0.0-1.0), log-mapped to
             * cutoff Hz. sub_drive/sub_tone are repurposed as the
             * filter's own Drive/Resonance (not the usual meaning). */
            double cutoff_hz = 80.0 * pow(8000.0 / 80.0, drive);
            moog_ladder_set(&b->moog, cutoff_hz, b->sub_tone, b->sub_drive);
            return moog_ladder_process(&b->moog, x);
        }
        case DISTROY_KORG_MS20: {
            double cutoff_hz = 80.0 * pow(8000.0 / 80.0, drive);
            /* HPF corner tracks proportionally below the main cutoff,
             * giving the classic MS-20 "sweeping narrow band" character
             * as the single knob moves, rather than a fixed HP corner. */
            double hp_cutoff = clampd(cutoff_hz * 0.15, 40.0, 2000.0);
            korg35hp_set(&b->korg_hp, hp_cutoff, b->sub_tone, b->sub_drive);
            korg35lp_set(&b->korg_lp, cutoff_hz, b->sub_tone, b->sub_drive);
            double y = korg35hp_process(&b->korg_hp, x);
            y = korg35lp_process(&b->korg_lp, y);
            return y;
        }
        default:
            return x;
    }
}

double distroy_block_process(DistroyBlock *b, double x) {
    const DistroyTypeInfo *info = distroy_type_info(b->type);
    double wet, out;
    int is_filter_type = (info->knob_mode == DISTROY_KNOB_CUTOFF);

    /* Filter types (Moog/Korg) repurpose sub_tone as Resonance, not
     * TiltEQ tone -- keep the tone stage neutral/flat for them so it
     * doesn't double up with the filter's own resonance character. */
    b->tone_stage.tone = is_filter_type ? 0.5 : b->sub_tone;

    if (info->knob_mode == DISTROY_KNOB_WET_DRY) {
        wet = type_process(b, x, b->sub_drive);
        out = x * (1.0 - b->knob) + wet * b->knob;
    } else {
        /* GAIN and CUTOFF modes both pass the knob straight through and
         * are fully wet (no dry blend) -- see type_process for how
         * CUTOFF-mode types (Moog/Korg) interpret this argument as
         * cutoff frequency rather than drive/gain. */
        wet = type_process(b, x, b->knob);
        out = wet;
    }

    out = tilteq_process(&b->tone_stage, out);
    out = onepole_process(&b->dc_block, out);

    /* Level trim: sub_level 0.0-1.0 maps to roughly 0.7x-1.3x (+-3dB-ish),
     * tasteful range so it colors output level without wrecking gain
     * staging through the rest of the chain. */
    double level_gain = 0.7 + b->sub_level * 0.6;
    return out * level_gain;
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
        c->slots[i].sub_drive = (double)xorshift_next(&state) / (double)UINT32_MAX;
        c->slots[i].sub_tone = (double)xorshift_next(&state) / (double)UINT32_MAX;
        c->slots[i].sub_level = (double)xorshift_next(&state) / (double)UINT32_MAX;
        c->slots[i].tone_stage.tone = c->slots[i].sub_tone;
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
