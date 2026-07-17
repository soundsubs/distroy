#include <stdio.h>
#include <math.h>
#include "../src/distroy_dsp.h"

static int check_finite_sweep(const char *label, DistroyType type, DistroyKnobMode mode) {
    DistroyBlock b;
    distroy_block_init(&b, type, 44100.0);
    int ok = 1;
    double peak = 0.0;

    for (int knob_step = 0; knob_step <= 10; knob_step++) {
        b.knob = knob_step / 10.0;
        double phase = 0.0;
        double freq = 220.0;
        for (int i = 0; i < 4410; i++) { /* 0.1s */
            double x = sin(phase) * 0.8;
            phase += 2.0 * M_PI * freq / 44100.0;
            double y = distroy_block_process(&b, x);
            if (!isfinite(y)) {
                printf("[%s] NOT FINITE at knob=%.1f sample=%d value=%f\n", label, b.knob, i, y);
                ok = 0;
            }
            if (fabs(y) > peak) peak = fabs(y);
        }
    }
    printf("[%s] mode=%s finite=%s peak=%.4f\n",
           label, mode == DISTROY_KNOB_GAIN ? "GAIN" : "WET_DRY",
           ok ? "yes" : "NO", peak);
    return ok;
}

int main(void) {
    int all_ok = 1;

    printf("=== Per-type sanity sweep ===\n");
    for (int t = 0; t < DISTROY_TYPE_COUNT; t++) {
        const DistroyTypeInfo *info = distroy_type_info((DistroyType)t);
        if (!check_finite_sweep(info->name, (DistroyType)t, info->knob_mode)) {
            all_ok = 0;
        }
    }

    printf("\n=== Full chain (random assignment) sanity ===\n");
    DistroyChain chain;
    distroy_chain_init(&chain, 44100.0);
    distroy_chain_randomize(&chain, 12345);

    printf("Randomized chain assignment (slot 7 -> slot 0, right to left):\n");
    for (int i = DISTROY_NUM_SLOTS - 1; i >= 0; i--) {
        const DistroyTypeInfo *info = distroy_type_info(chain.slots[i].type);
        printf("  slot %d: %s (%s)\n", i, info->name,
               info->knob_mode == DISTROY_KNOB_GAIN ? "Gain" : "Wet/Dry");
    }

    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
        chain.slots[i].knob = 0.6;
    }

    int chain_ok = 1;
    double peak = 0.0;
    double phase = 0.0;
    double freq = 220.0;
    for (int i = 0; i < 44100; i++) { /* 1s */
        double x = sin(phase) * 0.5;
        phase += 2.0 * M_PI * freq / 44100.0;
        double y = distroy_chain_process(&chain, x);
        if (!isfinite(y)) {
            printf("Chain output NOT FINITE at sample %d: %f\n", i, y);
            chain_ok = 0;
        }
        if (fabs(y) > peak) peak = fabs(y);
    }
    printf("Full chain: finite=%s peak=%.4f\n", chain_ok ? "yes" : "NO", peak);
    if (!chain_ok) all_ok = 0;

    printf("\n=== Randomization distribution check (1000 seeds) ===\n");
    int type_counts[DISTROY_TYPE_COUNT] = {0};
    for (unsigned int seed = 1; seed <= 1000; seed++) {
        DistroyChain c2;
        distroy_chain_init(&c2, 44100.0);
        distroy_chain_randomize(&c2, seed);
        for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
            type_counts[c2.slots[i].type]++;
        }
    }
    for (int t = 0; t < DISTROY_TYPE_COUNT; t++) {
        printf("  %-14s: %d\n", distroy_type_info((DistroyType)t)->name, type_counts[t]);
    }

    printf("\n%s\n", all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return all_ok ? 0 : 1;
}
