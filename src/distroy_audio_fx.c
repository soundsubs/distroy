/* DISTROY -- 8-slot distortion/overdrive pedal chain, Schwung audio_fx
 * module for Ableton Move.
 *
 * ARCHITECTURE: learned directly from the EMAX_FX project -- this is a
 * dlopen()'d shared library implementing audio_fx_api_v2_t, not a
 * standalone process. No MIDI dependency at all (unlike EMAX_FX's
 * pitch-tracking/envelope features), which sidesteps the on_midi
 * mystery we hit there entirely -- DISTROY is pure audio processing
 * driven by 8 knobs.
 *
 * Knob mapping: knob i (1-8) controls slots[i-1].knob (0.0-1.0),
 * shared identically between left/right channels (independent filter
 * state per channel to avoid stereo-collapse artifacts, same pattern
 * as EMAX_FX's per-channel EmaxVoice).
 *
 * CONFIRMED ON REAL HARDWARE: get_param()'s returned string affects
 * what Schwung displays -- but v0.2.0's "MUFF GAIN 30%" format (name
 * first) broke knob turning entirely, while the display itself worked.
 * Working theory: the host parses this string back into a float (e.g.
 * atof()) to track the value while turning, and a leading non-numeric
 * character makes that parse fail. v0.2.1 puts the number first
 * ("30% MUFF GAIN") to stay parseable while keeping the descriptive
 * text. (The static module.json "label" field was separately shortened
 * to bare slot numbers, since which pedal occupies a slot is
 * randomized at runtime and can't be baked into a static label.)
 *
 * On instantiation, AND whenever the "randomize" menu action fires,
 * all 8 slots get a new random pedal type AND a new random knob value
 * (distroy_chain_randomize_all()) -- both channels share the same seed
 * so left/right always agree on pedal identity and knob value.
 *
 * RANDOMIZE MENU ACTION (experimental): exposed as a 9th param
 * ("randomize", enum ["-", "Go!"]) that is NOT listed in module.json's
 * "knobs" array (which only lists the 8 slot params) -- the working
 * theory, unconfirmed, is that params outside "knobs" surface in the
 * module's own settings menu (jog-wheel navigable) rather than getting
 * a physical knob. This mirrors the "Swap Module" menu item noticed on
 * other modules during EMAX_FX development. Needs on-device
 * confirmation.
 *
 * STILL OPEN (unconfirmed):
 *   - "Shift+Touch to pick a different pedal for that slot" -- no
 *     confirmed API for a distinct shift+touch gesture reaching
 *     third-party audio_fx plugins.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "audio_fx_api_v2.h"
#include "distroy_dsp.h"

typedef struct {
    DistroyChain left;
    DistroyChain right;
    double sample_rate;
    unsigned int randomize_counter; /* mixed into the seed so repeated
                                        triggers within the same second
                                        still produce different results */
} DistroyInstance;

static double clampd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static unsigned int make_seed(DistroyInstance *inst) {
    return (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)inst
           ^ (inst->randomize_counter++ * 2654435761u); /* Knuth multiplicative hash mix */
}

static void randomize_everything(DistroyInstance *inst) {
    unsigned int seed = make_seed(inst);
    distroy_chain_randomize_all(&inst->left, seed);
    distroy_chain_randomize_all(&inst->right, seed);
}

static void* create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json; /* not parsed -- see EMAX_FX's equivalent known simplification */

    DistroyInstance *inst = (DistroyInstance*)calloc(1, sizeof(DistroyInstance));
    if (!inst) return NULL;

    inst->sample_rate = 44100.0;
    inst->randomize_counter = 0;
    distroy_chain_init(&inst->left, inst->sample_rate);
    distroy_chain_init(&inst->right, inst->sample_rate);

    /* Random 8-pedal chain AND random knob values on instantiation. */
    randomize_everything(inst);

    return inst;
}

static void destroy_instance(void *instance) {
    free(instance);
}

static void process_block(void *instance, int16_t *audio_inout, int frames) {
    DistroyInstance *inst = (DistroyInstance*)instance;
    if (!inst) return;

    for (int i = 0; i < frames; i++) {
        double l = audio_inout[2 * i]     / 32768.0;
        double r = audio_inout[2 * i + 1] / 32768.0;

        l = distroy_chain_process(&inst->left, l);
        r = distroy_chain_process(&inst->right, r);

        /* Safety limiter: distortion stages can exceed unity internally
         * (see test_dsp.c peak measurements, up to ~1.4 on Fuzz) --
         * clamp hard here rather than let it wrap/alias on int16 write. */
        double lo = clampd(l * 32767.0, -32768.0, 32767.0);
        double ro = clampd(r * 32767.0, -32768.0, 32767.0);

        audio_inout[2 * i]     = (int16_t)lround(lo);
        audio_inout[2 * i + 1] = (int16_t)lround(ro);
    }
}

/* Parses "slotN" -> N (0-7), or -1 if the key doesn't match. */
static int parse_slot_key(const char *key) {
    if (strncmp(key, "slot", 4) != 0) return -1;
    int n = atoi(key + 4);
    if (n < 0 || n >= DISTROY_NUM_SLOTS) return -1;
    return n;
}

static void set_param(void *instance, const char *key, const char *val) {
    DistroyInstance *inst = (DistroyInstance*)instance;
    if (!inst || !key || !val) return;

    if (strcmp(key, "randomize") == 0) {
        /* Momentary trigger: any selection other than "-" fires it.
         * get_param() always reports back "-" so the UI springs back
         * rather than showing "Go!" as a persisted state. */
        if (strcmp(val, "-") != 0) {
            randomize_everything(inst);
        }
        return;
    }

    int slot = parse_slot_key(key);
    if (slot < 0) return;

    double v = clampd(atof(val), 0.0, 1.0);
    inst->left.slots[slot].knob = v;
    inst->right.slots[slot].knob = v;
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    DistroyInstance *inst = (DistroyInstance*)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "randomize") == 0) {
        return snprintf(buf, (size_t)buf_len, "-");
    }

    int slot = parse_slot_key(key);
    if (slot < 0) return -1;

    /* REVERTED: v0.2.0/0.2.1 tried embedding descriptive text in this
     * string ("MUFF GAIN 30%", then "30% MUFF GAIN"). Both were wrong.
     * Confirmed on real hardware: the host does NOT display this string
     * verbatim -- it parses the leading number as the literal stored
     * parameter value (not respecting our declared 0.0-1.0 range as a
     * scale factor) and re-formats/displays THAT number independently
     * using its own unit/percent rules, discarding any trailing text.
     * "30% MUFF GAIN" got parsed as the raw value 30 (not 0.30), then
     * re-formatted as a huge bogus percentage (2300%/5300%/5800%).
     * Reverting to a plain, correctly-scaled numeric value restores
     * correct knob behavior. Dynamically showing the pedal's name is
     * still an OPEN, unsolved question -- no confirmed mechanism found
     * yet, same status as EMAX_FX's on_midi mystery. Not guessing
     * further here since the last two guesses both caused real
     * regressions. */
    return snprintf(buf, (size_t)buf_len, "%.4f", inst->left.slots[slot].knob);
}

static audio_fx_api_v2_t api = {
    .api_version = 2,
    .create_instance = create_instance,
    .destroy_instance = destroy_instance,
    .process_block = process_block,
    .set_param = set_param,
    .get_param = get_param,
    .on_midi = NULL, /* DISTROY has no MIDI dependency */
};

audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t *host) {
    (void)host;
    return &api;
}
