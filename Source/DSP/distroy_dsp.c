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
    [DISTROY_FUZZ]         = { "Fuzz",          "FUZZ",   DISTROY_KNOB_WET_DRY },
    [DISTROY_METAL]        = { "Metal",         "METAL",  DISTROY_KNOB_WET_DRY },
    [DISTROY_TUBESCREAMER] = { "Tubescreamer",  "TS9",    DISTROY_KNOB_WET_DRY },
    [DISTROY_BIG_MUFF]     = { "Big Muff",      "MUFF",   DISTROY_KNOB_WET_DRY },
    [DISTROY_SANSAMP]      = { "Sansamp",       "SANS",   DISTROY_KNOB_WET_DRY },
    [DISTROY_RAT]          = { "Rat",           "RAT",    DISTROY_KNOB_WET_DRY },
    [DISTROY_GEIGER_COUNTER] = { "Geiger Counter", "GEIGER", DISTROY_KNOB_WET_DRY },
    [DISTROY_MOOG_LADDER]  = { "Moog Ladder",   "MOOG",   DISTROY_KNOB_CUTOFF },
    [DISTROY_KORG_MS20]    = { "Korg MS-20",    "MS20",   DISTROY_KNOB_CUTOFF },
    [DISTROY_MUTRON]       = { "Mu-Tron",       "MUTRON", DISTROY_KNOB_SENS },
    [DISTROY_CRYBABY]      = { "Cry Baby 535Q", "CRYB",   DISTROY_KNOB_SENS },
    [DISTROY_JENSEN]       = { "Jensen",        "JENSEN", DISTROY_KNOB_WET_DRY },
    [DISTROY_LUNDAHL]      = { "Lundahl",       "LUND",   DISTROY_KNOB_WET_DRY },
    [DISTROY_LOFI]         = { "LoFi",          "LOFI",   DISTROY_KNOB_WET_DRY },
    [DISTROY_FZ1W]         = { "Boss FZ-1W",    "FZ1W",   DISTROY_KNOB_WET_DRY },
    [DISTROY_CLIP]         = { "Clip",          "CLIP",   DISTROY_KNOB_WET_DRY },
    [DISTROY_REKT]         = { "Rekt",          "REKT",   DISTROY_KNOB_WET_DRY },
    [DISTROY_WHAM]         = { "Wham",          "WHAM",   DISTROY_KNOB_WET_DRY },
    [DISTROY_TAPE]         = { "Tape",          "TAPE",   DISTROY_KNOB_WET_DRY },
    [DISTROY_SPKR]         = { "Speaker",       "SPKR",   DISTROY_KNOB_SIZE },
};

const DistroyTypeInfo* distroy_type_info(DistroyType type) {
    if (type < 0 || type >= DISTROY_TYPE_COUNT) return &kTypeInfo[0];
    return &kTypeInfo[type];
}

const char* distroy_knob_mode_label(DistroyKnobMode mode) {
    switch (mode) {
        case DISTROY_KNOB_GAIN: return "GAIN";
        case DISTROY_KNOB_CUTOFF: return "CUTOFF";
        case DISTROY_KNOB_SENS: return "SENS";
        case DISTROY_KNOB_SIZE: return "SIZE";
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

void biquad_set_bandpass(Biquad *f, double freq_hz, double q, double sample_rate) {
    double w0 = 2.0 * M_PI * freq_hz / sample_rate;
    double alpha = sin(w0) / (2.0 * q);
    double cosw0 = cos(w0);

    double b0 = alpha;
    double b1 = 0.0;
    double b2 = -alpha;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha;

    f->b0 = b0 / a0;
    f->b1 = b1 / a0;
    f->b2 = b2 / a0;
    f->a1 = a1 / a0;
    f->a2 = a2 / a0;
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
 * Envelope follower (auto-wah)
 * ------------------------------------------------------------------- */

void envfollow_init(EnvelopeFollower *e, double attack_ms, double release_ms, double sample_rate) {
    e->envelope = 0.0;
    e->attack_coeff = exp(-1.0 / (0.001 * attack_ms * sample_rate));
    e->release_coeff = exp(-1.0 / (0.001 * release_ms * sample_rate));
}

double envfollow_process(EnvelopeFollower *e, double x) {
    double rectified = fabs(x);
    double coeff = (rectified > e->envelope) ? e->attack_coeff : e->release_coeff;
    e->envelope = coeff * e->envelope + (1.0 - coeff) * rectified;
    return e->envelope;
}

/* ---------------------------------------------------------------------
 * Pitch shifter (WHAM) -- see header comment for the algorithm summary.
 * ------------------------------------------------------------------- */

void pitchshift_init(PitchShifter *ps) {
    for (int i = 0; i < PITCHSHIFT_BUFFER_SIZE; i++) ps->buffer[i] = 0.0;
    ps->write_pos = 0;
    ps->read_offset1 = 0.0;
    ps->read_offset2 = PITCHSHIFT_WINDOW * 0.5; /* permanently half a window apart */
    ps->ratio = 1.0;
}

void pitchshift_set_semitones(PitchShifter *ps, double semitones) {
    ps->ratio = pow(2.0, semitones / 12.0);
}

static double pitchshift_read_interp(PitchShifter *ps, double pos) {
    double floor_pos = floor(pos);
    int i0 = (int)floor_pos;
    double frac = pos - floor_pos;
    int idx0 = ((i0 % PITCHSHIFT_BUFFER_SIZE) + PITCHSHIFT_BUFFER_SIZE) % PITCHSHIFT_BUFFER_SIZE;
    int idx1 = (idx0 + 1) % PITCHSHIFT_BUFFER_SIZE;
    return ps->buffer[idx0] * (1.0 - frac) + ps->buffer[idx1] * frac;
}

double pitchshift_process(PitchShifter *ps, double x) {
    ps->buffer[ps->write_pos] = x;

    /* Advance both taps relative to the write head by (1 - ratio) per
     * sample -- ratio>1 (pitch up) makes the read heads fall behind
     * more slowly (read faster through history); ratio<1 (pitch down)
     * makes them fall behind faster (read slower through history). */
    ps->read_offset1 += (1.0 - ps->ratio);
    ps->read_offset2 += (1.0 - ps->ratio);

    if (ps->read_offset1 < 0.0) ps->read_offset1 += PITCHSHIFT_WINDOW;
    if (ps->read_offset1 >= PITCHSHIFT_WINDOW) ps->read_offset1 -= PITCHSHIFT_WINDOW;
    if (ps->read_offset2 < 0.0) ps->read_offset2 += PITCHSHIFT_WINDOW;
    if (ps->read_offset2 >= PITCHSHIFT_WINDOW) ps->read_offset2 -= PITCHSHIFT_WINDOW;

    double pos1 = (double)ps->write_pos - ps->read_offset1;
    double pos2 = (double)ps->write_pos - ps->read_offset2;

    double s1 = pitchshift_read_interp(ps, pos1);
    double s2 = pitchshift_read_interp(ps, pos2);

    /* Triangular crossfade: each tap's gain peaks at the center of its
     * window and fades to 0 at the edges, where the OTHER tap is at
     * its own peak -- classic complementary 2-tap granular crossfade. */
    double w1 = 1.0 - fabs((ps->read_offset1 / PITCHSHIFT_WINDOW) * 2.0 - 1.0);
    double w2 = 1.0 - fabs((ps->read_offset2 / PITCHSHIFT_WINDOW) * 2.0 - 1.0);

    double out = s1 * w1 + s2 * w2;

    ps->write_pos = (ps->write_pos + 1) % PITCHSHIFT_BUFFER_SIZE;
    return out;
}

/* ---------------------------------------------------------------------
 * Decimator + bit quantizer (LOFI)
 * ------------------------------------------------------------------- */

void decimator_init(SimpleDecimator *d) {
    d->held_value = 0.0;
    d->phase = 0.0;
}

double decimator_process(SimpleDecimator *d, double x, double target_hz, double host_sample_rate) {
    d->phase += target_hz / host_sample_rate;
    if (d->phase >= 1.0) {
        d->phase -= 1.0;
        d->held_value = x;
    }
    return d->held_value;
}

double quantize_bits(double x, int bits) {
    if (bits >= 16) return x; /* not reachable given LOFI caps at 15, safety net */
    double levels = pow(2.0, (double)bits - 1); /* signed range, symmetric around 0 */
    if (levels < 1.0) levels = 1.0;
    return round(x * levels) / levels;
}

/* ---------------------------------------------------------------------
 * Noise generator (TAPE hiss)
 * ------------------------------------------------------------------- */

void noise_init(SimpleNoise *n, unsigned int seed) {
    n->state = seed != 0 ? seed : 1;
}

double noise_next(SimpleNoise *n) {
    unsigned int s = n->state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    n->state = s;
    return ((double)s / (double)UINT32_MAX) * 2.0 - 1.0;
}

/* ---------------------------------------------------------------------
 * Brickwall limiter (stereo-linked, look-ahead) -- see header comment.
 * ------------------------------------------------------------------- */

void brickwall_limiter_init(BrickwallLimiter *lim, double ceiling_db, double release_ms, double sample_rate) {
    for (int i = 0; i < LIMITER_LOOKAHEAD_SAMPLES; i++) {
        lim->delay_l[i] = 0.0;
        lim->delay_r[i] = 0.0;
        lim->peak_window[i] = 0.0;
    }
    lim->write_pos = 0;
    lim->gain = 1.0;
    lim->ceiling = pow(10.0, ceiling_db / 20.0);
    lim->release_coeff = exp(-1.0 / (0.001 * release_ms * sample_rate));
}

void brickwall_limiter_process(BrickwallLimiter *lim, double *l, double *r) {
    double in_l = *l;
    double in_r = *r;
    double peak_now = fabs(in_l);
    if (fabs(in_r) > peak_now) peak_now = fabs(in_r);

    lim->delay_l[lim->write_pos] = in_l;
    lim->delay_r[lim->write_pos] = in_r;
    lim->peak_window[lim->write_pos] = peak_now;

    /* True-peak-ish lookahead: scan the WHOLE window for its max, not
     * just the instantaneous sample, so a sharp transient anywhere in
     * the next ~2.9ms is already accounted for before it reaches the
     * output. O(128) per sample is trivial next to the filter chains
     * already running per-sample elsewhere in this project. */
    double max_peak = 0.0;
    for (int i = 0; i < LIMITER_LOOKAHEAD_SAMPLES; i++) {
        if (lim->peak_window[i] > max_peak) max_peak = lim->peak_window[i];
    }

    double target_gain = (max_peak > lim->ceiling) ? (lim->ceiling / max_peak) : 1.0;

    if (target_gain < lim->gain) {
        /* Instant attack -- safe specifically because the lookahead
         * already "saw this peak coming" before it reaches the output
         * (the delayed sample it'll be applied to hasn't been output
         * yet). A no-lookahead limiter couldn't safely do this. */
        lim->gain = target_gain;
    } else {
        lim->gain = lim->release_coeff * lim->gain + (1.0 - lim->release_coeff) * target_gain;
    }

    int read_pos = (lim->write_pos + 1) % LIMITER_LOOKAHEAD_SAMPLES;
    double out_l = lim->delay_l[read_pos] * lim->gain;
    double out_r = lim->delay_r[read_pos] * lim->gain;

    lim->write_pos = (lim->write_pos + 1) % LIMITER_LOOKAHEAD_SAMPLES;

    /* Final hard clamp -- the actual safety guarantee regardless of
     * anything above. This is what makes it a real "brickwall": output
     * NEVER exceeds ceiling, full stop, even in some edge case the
     * smoothed gain path didn't fully catch. */
    out_l = clampd(out_l, -lim->ceiling, lim->ceiling);
    out_r = clampd(out_r, -lim->ceiling, lim->ceiling);

    *l = out_l;
    *r = out_r;
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

/* Maps a 0.0-1.0 random value to a WHAM pitch-shift amount in
 * semitones, weighted per the project spec: never 0, mostly +-12
 * (70% combined), sometimes another characterful interval (30%,
 * spread across octave/4th/5th/2nd shifts up and down). */
static double decode_wham_semitone(double u) {
    if (u < 0.35) return 12.0;
    if (u < 0.70) return -12.0;
    static const double others[] = { -24.0, -7.0, -5.0, -2.0, 2.0, 5.0, 7.0, 24.0 };
    double remainder = (u - 0.70) / 0.30;
    int idx = (int)(remainder * 8.0);
    if (idx > 7) idx = 7;
    if (idx < 0) idx = 0;
    return others[idx];
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
    b->env = (EnvelopeFollower){0};
    b->wah_filter = (Biquad){0};
    pitchshift_init(&b->pitch);
    decimator_init(&b->decim);
    noise_init(&b->noise, (unsigned int)(uintptr_t)b ^ 0x9e3779b9u);
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
        case DISTROY_MUTRON:       return 1000.0;
        case DISTROY_CRYBABY:      return 1000.0;
        case DISTROY_JENSEN:       return 1500.0;
        case DISTROY_LUNDAHL:      return 1000.0;
        case DISTROY_FZ1W:         return 1000.0;
        case DISTROY_CLIP:         return 1000.0;
        case DISTROY_REKT:         return 900.0;
        case DISTROY_WHAM:         return 1000.0;
        case DISTROY_TAPE:         return 1500.0;
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
        case DISTROY_MUTRON:
            /* smoother/rounder auto-wah -- wider Q */
            envfollow_init(&b->env, 6.0, 120.0, sr);
            break;
        case DISTROY_CRYBABY:
            /* narrower/more vocal auto-wah -- higher Q, snappier envelope */
            envfollow_init(&b->env, 3.0, 90.0, sr);
            break;
        case DISTROY_JENSEN:
            onepole_set_lowpass(&b->color_lp, 12000.0, sr); /* bright, extended top */
            biquad_set_peaking(&b->color_peak, 80.0, 0.9, 2.0, sr); /* clean low-end lift */
            break;
        case DISTROY_LUNDAHL:
            onepole_set_lowpass(&b->color_lp, 8000.0, sr); /* darker top */
            biquad_set_peaking(&b->color_peak, 120.0, 0.9, 3.0, sr); /* more colored low-mid */
            break;
        case DISTROY_TAPE:
            onepole_set_lowpass(&b->color_lp, 9000.0, sr); /* tape HF softening */
            break;
        case DISTROY_FZ1W:
            biquad_set_peaking(&b->color_peak, 1000.0, 1.0, 2.0, sr); /* tighter presence than vintage Fuzz */
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
        case DISTROY_MUTRON: {
            /* SENS-mode: "drive" here is the knob value = envelope
             * sensitivity/depth. Smoother/rounder sweep than Cry Baby
             * (wider Q, wider but gentler frequency range). */
            double env = envfollow_process(&b->env, x);
            double sens = drive;
            double freq = 300.0 + clampd(env * sens * 6.0, 0.0, 1.0) * 1500.0;
            biquad_set_bandpass(&b->wah_filter, freq, 3.0, b->sample_rate);
            return biquad_process(&b->wah_filter, x);
        }
        case DISTROY_CRYBABY: {
            /* Narrower Q, snappier envelope, more vocal/aggressive sweep
             * than Mu-Tron. */
            double env = envfollow_process(&b->env, x);
            double sens = drive;
            double freq = 400.0 + clampd(env * sens * 6.0, 0.0, 1.0) * 1800.0;
            biquad_set_bandpass(&b->wah_filter, freq, 5.0, b->sample_rate);
            return biquad_process(&b->wah_filter, x);
        }
        case DISTROY_JENSEN: {
            double gain = 1.5 + drive * 3.5;
            double y = asym_soft_clip(gain * x, 3.0, 2.7); /* gentle, fairly symmetric */
            y = onepole_process(&b->color_lp, y); /* bright/extended top */
            return biquad_process(&b->color_peak, y); /* clean low-end lift */
        }
        case DISTROY_LUNDAHL: {
            double gain = 1.5 + drive * 3.0;
            double y = asym_soft_clip(gain * x, 2.5, 3.3); /* more asymmetric/colored */
            y = onepole_process(&b->color_lp, y); /* darker top */
            return biquad_process(&b->color_peak, y); /* more colored low-mid */
        }
        case DISTROY_LOFI: {
            /* sub_drive/sub_tone repurposed as bit-depth/sample-rate
             * encodes (see header comment) rather than Drive/Tone. */
            int bits = 1 + (int)(b->sub_drive * 14.0); /* 1-15, never 16 */
            double target_hz = 100.0 + b->sub_tone * 9900.0; /* 100-10000 Hz, never 44100 */
            double y = decimator_process(&b->decim, x, target_hz, b->sample_rate);
            return quantize_bits(y, bits);
        }
        case DISTROY_FZ1W: {
            double gain = 4.0 + drive * 20.0;
            double y = asym_soft_clip(gain * x, 3.5 + drive * 1.5, 3.0 + drive * 1.5); /* tighter/more symmetric than Fuzz */
            return biquad_process(&b->color_peak, y);
        }
        case DISTROY_CLIP: {
            double gain = 3.0 + drive * 40.0;
            return hard_clip(x, gain, 1.0);
        }
        case DISTROY_REKT: {
            double gain = 3.0 + drive * 40.0;
            double y = hard_clip(x, gain, 1.0);
            /* Full-wave rectify -- the resulting DC offset is removed by
             * the universal dc_block downstream, leaving just the
             * harsh, pitched-up-sounding buzz character. */
            return fabs(y);
        }
        case DISTROY_WHAM: {
            double semitone = decode_wham_semitone(b->sub_drive);
            pitchshift_set_semitones(&b->pitch, semitone);
            return pitchshift_process(&b->pitch, x);
        }
        case DISTROY_TAPE: {
            double gain = 1.3 + drive * 1.8;
            double y = soft_clip(x, gain);
            y = onepole_process(&b->color_lp, y);
            double hiss = noise_next(&b->noise) * 0.0025;
            return y + hiss;
        }
        case DISTROY_SPKR: {
            /* SIZE-mode: "drive" here is the knob value, 0=impossibly
             * small (cell-phone-speaker tinny) to 1=impossibly large
             * (2-foot-woofer boomy). Reuses color_hs/color_lp/
             * color_peak (already-existing generic fields) as the
             * HPF/LPF/resonance-bump stages, no new struct fields
             * needed. Range deliberately extreme per spec ("impossibly
             * small... to impossibly large"), not a realistic speaker
             * range -- earlier version (80-300Hz HP / 3.5-6kHz LP) was
             * too subtle to hear clearly. */
            double size = drive;
            double hp_cutoff = 900.0 - size * 880.0;   /* 900Hz (phone) -> 20Hz (huge woofer) */
            double lp_cutoff = 5000.0 - size * 3700.0; /* 5000Hz (tinny) -> 1300Hz (dark/boomy) */
            double peak_freq = 2200.0 - size * 2140.0; /* 2200Hz (tinny peak) -> 60Hz (boom peak) */
            double peak_q = 2.5 - size * 1.3;          /* sharper tinny resonance -> looser boomy resonance */
            double peak_gain = 4.0 + size * 2.0;
            onepole_set_highpass(&b->color_hs, hp_cutoff, b->sample_rate);
            onepole_set_lowpass(&b->color_lp, lp_cutoff, b->sample_rate);
            biquad_set_peaking(&b->color_peak, peak_freq, peak_q, peak_gain, b->sample_rate);
            double y = onepole_process(&b->color_hs, x);
            y = onepole_process(&b->color_lp, y);
            return biquad_process(&b->color_peak, y);
        }
        default:
            return x;
    }
}

double distroy_block_process(DistroyBlock *b, double x) {
    const DistroyTypeInfo *info = distroy_type_info(b->type);
    double wet, out;
    /* These types repurpose sub_tone for something other than TiltEQ
     * tone (Moog/Korg: Resonance, LOFI: sample-rate encode) -- keep
     * the tone stage neutral/flat for them so it doesn't double up.
     * NOTE: this is NOT simply "all CUTOFF-mode types" -- SPKR is also
     * CUTOFF mode but keeps normal Tone/TiltEQ, since its sub_tone
     * still means Tone for that type. */
    int skip_tilt = (b->type == DISTROY_MOOG_LADDER || b->type == DISTROY_KORG_MS20
                      || b->type == DISTROY_LOFI);

    b->tone_stage.tone = skip_tilt ? 0.5 : b->sub_tone;

    if (info->knob_mode == DISTROY_KNOB_WET_DRY) {
        wet = type_process(b, x, b->sub_drive);
        out = x * (1.0 - b->knob) + wet * b->knob;
    } else {
        /* GAIN, CUTOFF, SENS, and SIZE modes all pass the knob straight
         * through and are fully wet (no dry blend) -- see type_process
         * for how each mode interprets this argument (drive, cutoff
         * frequency, or envelope sensitivity respectively). */
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

/* Shuffles all DISTROY_TYPE_COUNT type indices (Fisher-Yates) into
 * `out`, writing the first `count` entries. Guarantees no duplicate
 * pedal type among the slots -- requires DISTROY_TYPE_COUNT >= count
 * (10 >= 8, currently true; if more slots than types ever existed this
 * would need to wrap/repeat, not attempted here since it's not the
 * current situation). */
static void shuffle_distinct_types(DistroyType *out, int count, unsigned int *state) {
    DistroyType pool[DISTROY_TYPE_COUNT];
    for (int i = 0; i < DISTROY_TYPE_COUNT; i++) pool[i] = (DistroyType)i;
    for (int i = DISTROY_TYPE_COUNT - 1; i > 0; i--) {
        unsigned int j = xorshift_next(state) % (unsigned int)(i + 1);
        DistroyType tmp = pool[i];
        pool[i] = pool[j];
        pool[j] = tmp;
    }
    for (int i = 0; i < count && i < DISTROY_TYPE_COUNT; i++) {
        out[i] = pool[i];
    }
}

void distroy_chain_randomize(DistroyChain *c, unsigned int seed) {
    unsigned int state = seed;
    DistroyType types[DISTROY_NUM_SLOTS];
    shuffle_distinct_types(types, DISTROY_NUM_SLOTS, &state);
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        distroy_block_set_type(&c->slots[i], types[i]);
    }
}

void distroy_chain_randomize_all(DistroyChain *c, unsigned int seed) {
    unsigned int state = seed;
    DistroyType types[DISTROY_NUM_SLOTS];
    shuffle_distinct_types(types, DISTROY_NUM_SLOTS, &state);

    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        DistroyType t = types[i];
        distroy_block_set_type(&c->slots[i], t);
        /* xorshift_next returns a full-range unsigned int -- scale to
         * 0.0-1.0 */
        c->slots[i].knob = (double)xorshift_next(&state) / (double)UINT32_MAX;
        c->slots[i].sub_drive = (double)xorshift_next(&state) / (double)UINT32_MAX;

        double tone_rand = (double)xorshift_next(&state) / (double)UINT32_MAX;
        if (t == DISTROY_MOOG_LADDER || t == DISTROY_KORG_MS20) {
            /* SAFETY: resonance (repurposed sub_tone) is no longer
             * randomized at all for these two types -- always 0. Even
             * a 25% cap (v0.4.2) still howled loudly enough in some
             * randomized chains to risk hurting ears/speakers. Live
             * resonance control is planned for a future submenu (see
             * README's open questions) where the user can dial it in
             * deliberately; randomization just leaves it off. */
            tone_rand = 0.0;
        }
        c->slots[i].sub_tone = tone_rand;

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
