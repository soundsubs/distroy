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
           label, distroy_knob_mode_label(mode),
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
               distroy_knob_mode_label(info->knob_mode));
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

    printf("\n=== Sub-parameter (Drive/Tone/Level) extreme-value stress test ===\n");
    int subparam_ok = 1;
    double subparam_vals[] = {0.0, 1.0};
    for (int t = 0; t < DISTROY_TYPE_COUNT; t++) {
        const DistroyTypeInfo *info = distroy_type_info((DistroyType)t);
        for (int di = 0; di < 2; di++) {
            for (int ti = 0; ti < 2; ti++) {
                for (int li = 0; li < 2; li++) {
                    DistroyBlock b;
                    distroy_block_init(&b, (DistroyType)t, 44100.0);
                    b.knob = 0.7;
                    b.sub_drive = subparam_vals[di];
                    b.sub_tone = subparam_vals[ti];
                    b.sub_level = subparam_vals[li];
                    double phase = 0.0;
                    for (int i = 0; i < 4410; i++) {
                        double x = sin(phase) * 0.8;
                        phase += 2.0 * M_PI * 220.0 / 44100.0;
                        double y = distroy_block_process(&b, x);
                        if (!isfinite(y)) {
                            printf("[%s] NOT FINITE: drive=%.0f tone=%.0f level=%.0f sample=%d\n",
                                   info->name, subparam_vals[di], subparam_vals[ti], subparam_vals[li], i);
                            subparam_ok = 0;
                        }
                    }
                }
            }
        }
    }
    printf("Sub-parameter stress test (8 corners x 8 types): finite=%s\n", subparam_ok ? "yes" : "NO");
    if (!subparam_ok) all_ok = 0;

    printf("\n=== Moog/Korg worst-case test: max cutoff + max resonance together ===\n");
    int filter_worst_ok = 1;
    DistroyType filter_types[] = {DISTROY_MOOG_LADDER, DISTROY_KORG_MS20};
    for (int ti = 0; ti < 2; ti++) {
        DistroyBlock b;
        distroy_block_init(&b, filter_types[ti], 44100.0);
        b.knob = 1.0;        /* max cutoff */
        b.sub_tone = 1.0;    /* max resonance */
        b.sub_drive = 1.0;   /* max drive too, for good measure */
        double phase = 0.0;
        for (int i = 0; i < 44100; i++) { /* 1s, plenty of time to diverge if unstable */
            double x = sin(phase) * 0.9;
            phase += 2.0 * M_PI * 220.0 / 44100.0;
            double y = distroy_block_process(&b, x);
            if (!isfinite(y)) {
                printf("[%s] NOT FINITE at max cutoff+resonance+drive, sample %d: %f\n",
                       distroy_type_info(filter_types[ti])->name, i, y);
                filter_worst_ok = 0;
                break;
            }
        }
    }
    printf("Moog/Korg worst-case: finite=%s\n", filter_worst_ok ? "yes" : "NO");
    if (!filter_worst_ok) all_ok = 0;

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

    printf("\n=== No-duplicate-types check (1000 seeds) ===\n");
    int dup_found = 0;
    for (unsigned int seed = 1; seed <= 1000; seed++) {
        DistroyChain c3;
        distroy_chain_init(&c3, 44100.0);
        distroy_chain_randomize_all(&c3, seed);
        int seen[DISTROY_TYPE_COUNT] = {0};
        for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
            if (seen[c3.slots[i].type]) {
                printf("DUPLICATE type %s found in chain seed=%u\n",
                       distroy_type_info(c3.slots[i].type)->name, seed);
                dup_found = 1;
            }
            seen[c3.slots[i].type] = 1;
        }
    }
    printf("No-duplicate-types check: %s\n", dup_found ? "FAILED (duplicates found)" : "PASSED (no duplicates in 1000 chains)");
    if (dup_found) all_ok = 0;

    printf("\n=== Moog/Korg resonance cap check (1000 seeds) ===\n");
    int cap_violated = 0;
    for (unsigned int seed = 1; seed <= 1000; seed++) {
        DistroyChain c4;
        distroy_chain_init(&c4, 44100.0);
        distroy_chain_randomize_all(&c4, seed);
        for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
            DistroyType t = c4.slots[i].type;
            if ((t == DISTROY_MOOG_LADDER || t == DISTROY_KORG_MS20) && c4.slots[i].sub_tone > 0.25) {
                printf("Resonance cap violated: %s sub_tone=%.4f seed=%u\n",
                       distroy_type_info(t)->name, c4.slots[i].sub_tone, seed);
                cap_violated = 1;
            }
        }
    }
    printf("Moog/Korg resonance cap check: %s\n", cap_violated ? "FAILED" : "PASSED (never exceeded 25%% in 1000 chains)");
    if (cap_violated) all_ok = 0;

    printf("\n%s\n", all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return all_ok ? 0 : 1;
}
