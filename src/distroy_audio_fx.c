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
 * DISPLAY/VALUE HISTORY: tried three different get_param() formats to
 * show the pedal's name on-screen -- name-first text, number-first
 * text, and abbrev+percent under module.json type "mode". Each
 * appeared to work in isolated testing at some point, but knob turning
 * kept regressing to "frozen" on real hardware across sessions --
 * including AFTER reverting to the "confirmed reliable" plain-numeric
 * + type "float" combo in v0.4.2, which was reported still frozen.
 * Since guessing at get_param() format changes hasn't reliably solved
 * this, v0.4.3 adds real file-based diagnostic logging (see dbg_log()
 * below, same approach that resolved EMAX_FX's on_midi mystery) to get
 * actual evidence of what set_param() receives during a knob turn,
 * rather than continuing to guess blind.
 *
 * On instantiation, AND whenever the "randomize" menu action fires,
 * all 8 slots get a new random pedal type AND new random knob/sub
 * values (distroy_chain_randomize_all()) -- both channels share the
 * same seed so left/right always agree on pedal identity and values.
 *
 * RANDOMIZE MENU ACTION (confirmed working): exposed as a 9th param
 * ("randomize", enum ["-", "Go!"]) that is NOT listed in module.json's
 * "knobs" array (which only lists the 8 slot params) -- confirmed on
 * real hardware that params outside "knobs" surface in the module's
 * own settings menu (jog-wheel navigable) rather than getting a
 * physical knob, and selecting "Go!" correctly re-randomizes the chain.
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

/* Diagnostic logging: tries TWO locations, to directly test the theory
 * below. A prior attempt (still in git history) wrote only to the
 * module's own install directory
 * (/data/UserData/schwung/modules/audio_fx/DISTROY/debug.log), same
 * pattern that worked for EMAX_FX -- but produced zero output here
 * despite create_instance() definitely running (the DSP audibly
 * processes audio, RANDOMIZE works). Working theory: module install
 * directories may now be read-only, silently failing fopen() (which
 * our code already guards against -- no crash, just no file). Trying
 * /data/UserData/Scratch/ as a second, plausibly-always-writable
 * location. If Scratch has content but the module dir doesn't, that
 * confirms the read-only theory. Retrieve via:
 *   http://move.local:7700/files?path=/data/UserData/Scratch
 *   http://move.local:7700/files?path=/data/UserData/schwung/modules/audio_fx/DISTROY
 * Only set_param() calls and one-time lifecycle events are logged (not
 * get_param(), which is likely polled far more often for display
 * refresh and would flood the log). */
#define DEBUG_LOG_PATH_SCRATCH "/data/UserData/Scratch/distroy_debug.log"
#define DEBUG_LOG_PATH_MODULE "/data/UserData/schwung/modules/audio_fx/DISTROY/debug.log"

static void dbg_log(const char *msg) {
    FILE *f1 = fopen(DEBUG_LOG_PATH_SCRATCH, "a");
    if (f1) {
        fprintf(f1, "%s\n", msg);
        fclose(f1);
    }
    FILE *f2 = fopen(DEBUG_LOG_PATH_MODULE, "a");
    if (f2) {
        fprintf(f2, "%s\n", msg);
        fclose(f2);
    }
}

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

    dbg_log("[DISTROY] create_instance called");

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

/* Parses "slotN" -> array index N-1 (0-7), or -1 if the key doesn't
 * match. Keys are 1-based ("slot1".."slot8") to match on-screen slot
 * numbering; internal array indices stay 0-based. */
static int parse_slot_key(const char *key) {
    if (strncmp(key, "slot", 4) != 0) return -1;
    int n = atoi(key + 4);
    if (n < 1 || n > DISTROY_NUM_SLOTS) return -1;
    return n - 1;
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
    if (slot < 0) {
        char dbgbuf[128];
        snprintf(dbgbuf, sizeof(dbgbuf), "[DISTROY] set_param: UNRECOGNIZED key=\"%s\" val=\"%s\"", key, val);
        dbg_log(dbgbuf);
        return;
    }

    double v = clampd(atof(val), 0.0, 1.0);
    inst->left.slots[slot].knob = v;
    inst->right.slots[slot].knob = v;

    char dbgbuf[128];
    snprintf(dbgbuf, sizeof(dbgbuf), "[DISTROY] set_param: key=\"%s\" val=\"%s\" -> stored knob=%.4f (slot %d)",
             key, val, v, slot + 1);
    dbg_log(dbgbuf);
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    DistroyInstance *inst = (DistroyInstance*)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "randomize") == 0) {
        return snprintf(buf, (size_t)buf_len, "-");
    }

    int slot = parse_slot_key(key);
    if (slot < 0) return -1;

    /* REVERTED (v0.4.2): back to plain numeric, type "float" in
     * module.json. We tried three approaches to show the pedal name in
     * this string (name-first text, number-first text, and this
     * abbrev+percent format under type "mode") -- all eventually
     * correlated with knobs freezing/not responding to turns on real
     * hardware, even though some tested fine in the moment. Given
     * repeated regressions in this exact spot, going back to the one
     * combination that's been reliably solid (float + plain numeric,
     * confirmed working in v0.2.2) rather than risk it again. Dynamic
     * name display remains unsolved -- see README. */
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
