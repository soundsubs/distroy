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
 * On instantiation, all 8 slots are randomly assigned a pedal type
 * (see distroy_chain_randomize() in distroy_dsp.c) -- both channels'
 * chains are randomized with the SAME seed so left/right always agree
 * on which pedal occupies which slot (only filter state differs
 * per-channel, never pedal identity).
 *
 * KNOWN OPEN QUESTIONS (need on-device testing, same as EMAX_FX's
 * on_midi investigation):
 *   - "Touching a knob shows the pedal's name" -- module.json's per-
 *     param "label" field is static at declare-time, but which pedal
 *     occupies a slot is randomized at runtime. get_param() currently
 *     returns just the plain numeric value (safe default matching
 *     standard knob-arc display). Whether Schwung has any mechanism
 *     for a plugin to override a param's displayed name/label
 *     dynamically is unconfirmed.
 *   - "Shift+Touch to pick a different pedal for that slot" -- no
 *     confirmed API for a distinct shift+touch gesture reaching
 *     third-party audio_fx plugins. All 8 knobs are already spoken for
 *     (one per slot), so this would need a secondary access mechanism
 *     we haven't identified yet.
 * Both are left as future work pending real-hardware testing, same
 * approach as EMAX_FX.
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
} DistroyInstance;

static double clampd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static void* create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json; /* not parsed -- see EMAX_FX's equivalent known simplification */

    DistroyInstance *inst = (DistroyInstance*)calloc(1, sizeof(DistroyInstance));
    if (!inst) return NULL;

    inst->sample_rate = 44100.0;
    distroy_chain_init(&inst->left, inst->sample_rate);
    distroy_chain_init(&inst->right, inst->sample_rate);

    /* Random 8-pedal chain on instantiation, per spec. Same seed for
     * both channels so they agree on pedal identity per slot. */
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)inst;
    distroy_chain_randomize(&inst->left, seed);
    distroy_chain_randomize(&inst->right, seed);

    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        inst->left.slots[i].knob = 0.5;
        inst->right.slots[i].knob = 0.5;
    }

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

    int slot = parse_slot_key(key);
    if (slot < 0) return;

    double v = clampd(atof(val), 0.0, 1.0);
    inst->left.slots[slot].knob = v;
    inst->right.slots[slot].knob = v;
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    DistroyInstance *inst = (DistroyInstance*)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    int slot = parse_slot_key(key);
    if (slot < 0) return -1;

    /* Plain numeric value -- see header comment re: the open question
     * about dynamically showing the pedal name on knob touch. */
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
