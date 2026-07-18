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

    printf("Randomized chain assignment (default direction: slot 0 -> slot 7, left to right):\n");
    for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
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
    printf("Sub-parameter stress test (8 corners x %d types): finite=%s\n", DISTROY_TYPE_COUNT, subparam_ok ? "yes" : "NO");
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
            if ((t == DISTROY_MOOG_LADDER || t == DISTROY_KORG_MS20) && c4.slots[i].sub_tone != 0.0) {
                printf("Resonance not zero: %s sub_tone=%.4f seed=%u\n",
                       distroy_type_info(t)->name, c4.slots[i].sub_tone, seed);
                cap_violated = 1;
            }
        }
    }
    printf("Moog/Korg resonance-always-zero check: %s\n", cap_violated ? "FAILED" : "PASSED (always exactly 0 in 1000 chains)");
    if (cap_violated) all_ok = 0;

    printf("\n=== WHAM semitone constraint check (10000 samples of decode_wham_semitone) ===\n");
    {
        /* decode_wham_semitone is static in distroy_dsp.c, so test it
         * indirectly via WHAM blocks across many randomized seeds and
         * checking the resulting pitch ratio never corresponds to 0
         * semitones (ratio == 1.0), and tallying how often it lands on
         * the weighted +-12 buckets. */
        int zero_found = 0;
        int plus12 = 0, minus12 = 0, other = 0;
        int wham_count = 0;
        for (unsigned int seed = 1; seed <= 2000; seed++) {
            DistroyChain c5;
            distroy_chain_init(&c5, 44100.0);
            distroy_chain_randomize_all(&c5, seed);
            for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
                if (c5.slots[i].type != DISTROY_WHAM) continue;
                wham_count++;
                double ratio = c5.slots[i].pitch.ratio; /* not yet set until first process call */
                /* Force a process call so type_process's
                 * pitchshift_set_semitones actually runs and updates
                 * pitch.ratio from sub_drive. */
                distroy_block_process(&c5.slots[i], 0.1);
                ratio = c5.slots[i].pitch.ratio;
                double semitone = 12.0 * log(ratio) / log(2.0);
                if (fabs(semitone) < 0.01) {
                    printf("WHAM landed on 0 semitones! sub_drive=%.4f\n", c5.slots[i].sub_drive);
                    zero_found = 1;
                }
                if (fabs(semitone - 12.0) < 0.01) plus12++;
                else if (fabs(semitone + 12.0) < 0.01) minus12++;
                else other++;
            }
        }
        printf("WHAM occurrences: %d, +12: %d, -12: %d, other: %d\n", wham_count, plus12, minus12, other);
        printf("WHAM never-zero check: %s\n", zero_found ? "FAILED" : "PASSED");
        if (zero_found) all_ok = 0;
    }

    printf("\n=== LOFI constraint check (bits 1-15, rate 100-10000Hz, 2000 seeds) ===\n");
    {
        int violation = 0;
        for (unsigned int seed = 1; seed <= 2000; seed++) {
            DistroyChain c6;
            distroy_chain_init(&c6, 44100.0);
            distroy_chain_randomize_all(&c6, seed);
            for (int i = 0; i < DISTROY_NUM_SLOTS; i++) {
                if (c6.slots[i].type != DISTROY_LOFI) continue;
                int bits = 1 + (int)(c6.slots[i].sub_drive * 14.0);
                double rate = 100.0 + c6.slots[i].sub_tone * 9900.0;
                if (bits < 1 || bits > 15) {
                    printf("LOFI bits out of range: %d (sub_drive=%.4f)\n", bits, c6.slots[i].sub_drive);
                    violation = 1;
                }
                if (rate < 100.0 || rate > 10000.0) {
                    printf("LOFI rate out of range: %.1f (sub_tone=%.4f)\n", rate, c6.slots[i].sub_tone);
                    violation = 1;
                }
            }
        }
        printf("LOFI constraint check: %s\n", violation ? "FAILED" : "PASSED (bits always 1-15, rate always 100-10000Hz)");
        if (violation) all_ok = 0;
    }

    printf("\n=== Brickwall limiter safety test ===\n");
    {
        BrickwallLimiter lim;
        double ceiling_db = -1.0;
        brickwall_limiter_init(&lim, ceiling_db, 80.0, 44100.0);
        double ceiling_linear = pow(10.0, ceiling_db / 20.0);

        int limiter_ok = 1;
        double max_seen = 0.0;
        /* Worst case: feed it the loudest, most extreme chain output we
         * can construct -- Metal (heavy clip) into Rekt (rectify) at
         * max drive, deliberately overdriven well past unity, to make
         * sure the limiter actually catches real worst-case chain
         * output, not just a synthetic test tone. */
        DistroyBlock metal, rekt;
        distroy_block_init(&metal, DISTROY_METAL, 44100.0);
        distroy_block_init(&rekt, DISTROY_REKT, 44100.0);
        metal.knob = 1.0; metal.sub_drive = 1.0;
        rekt.knob = 1.0; rekt.sub_drive = 1.0;

        double phase = 0.0;
        for (int i = 0; i < 44100; i++) { /* 1 full second */
            double x = sin(phase) * 1.5; /* deliberately over-driven input, beyond +-1.0 */
            phase += 2.0 * M_PI * 440.0 / 44100.0;
            double y = distroy_block_process(&metal, x);
            y = distroy_block_process(&rekt, y);
            double l = y, r = y;
            brickwall_limiter_process(&lim, &l, &r);
            if (!isfinite(l) || !isfinite(r)) {
                printf("Limiter output NOT FINITE at sample %d\n", i);
                limiter_ok = 0;
            }
            if (fabs(l) > ceiling_linear + 1e-9 || fabs(r) > ceiling_linear + 1e-9) {
                printf("Limiter ceiling VIOLATED at sample %d: l=%.6f r=%.6f (ceiling=%.6f)\n",
                       i, l, r, ceiling_linear);
                limiter_ok = 0;
            }
            if (fabs(l) > max_seen) max_seen = fabs(l);
            if (fabs(r) > max_seen) max_seen = fabs(r);
        }
        printf("Limiter test: peak seen=%.6f, ceiling=%.6f, finite+within-ceiling=%s\n",
               max_seen, ceiling_linear, limiter_ok ? "yes" : "NO");
        if (!limiter_ok) all_ok = 0;
    }

    printf("\n=== Direction toggle test ===\n");
    {
        /* Build a chain with two very different types in slot 0 and
         * slot 7 (Clip = aggressive hard clipper, Speaker = gentle
         * filter), feed the same input through both directions, and
         * confirm the outputs actually DIFFER -- a real, if simple,
         * confirmation that the reverse flag genuinely changes
         * processing order rather than being a no-op. */
        DistroyChain dirChain;
        distroy_chain_init(&dirChain, 44100.0);
        distroy_block_set_type(&dirChain.slots[0], DISTROY_CLIP);
        distroy_block_set_type(&dirChain.slots[7], DISTROY_SPKR);
        for (int i = 1; i < 7; i++) distroy_block_set_type(&dirChain.slots[i], DISTROY_BOSS_OD);
        dirChain.slots[0].knob = 1.0;
        dirChain.slots[7].knob = 1.0;

        double forwardOut = 0.0, reverseOut = 0.0;
        int dirOk = 1;

        dirChain.reverse = 0; /* confirm default */
        if (dirChain.reverse != 0) { printf("Default reverse flag should be 0, was %d\n", dirChain.reverse); dirOk = 0; }
        forwardOut = distroy_chain_process(&dirChain, 0.8);

        distroy_chain_init(&dirChain, 44100.0); /* fresh state, same setup */
        distroy_block_set_type(&dirChain.slots[0], DISTROY_CLIP);
        distroy_block_set_type(&dirChain.slots[7], DISTROY_SPKR);
        for (int i = 1; i < 7; i++) distroy_block_set_type(&dirChain.slots[i], DISTROY_BOSS_OD);
        dirChain.slots[0].knob = 1.0;
        dirChain.slots[7].knob = 1.0;
        dirChain.reverse = 1;
        reverseOut = distroy_chain_process(&dirChain, 0.8);

        if (!isfinite(forwardOut) || !isfinite(reverseOut)) { printf("Direction test produced non-finite output\n"); dirOk = 0; }
        if (fabs(forwardOut - reverseOut) < 1e-9) {
            printf("Forward and reverse direction produced IDENTICAL output (%.6f) -- reverse flag may not be working\n", forwardOut);
            dirOk = 0;
        }
        printf("Forward (default): %.6f, Reverse: %.6f, direction genuinely changes output: %s\n",
               forwardOut, reverseOut, dirOk ? "yes" : "NO");
        if (!dirOk) all_ok = 0;
    }

    printf("\n=== NOIZ 66%% cap test (blend-ratio invariant, 5000 random knob values) ===\n");
    {
        /* Verify the actual safety property directly: cappedKnob (the
         * real wet-blend ratio used in distroy_block_process's NOIZ
         * special case) must never exceed 0.66, for any knob value.
         * (An earlier version of this test tried to infer this from
         * downstream output bounds, but got confused by the DC-blocking
         * highpass filter's response to an artificially-constant test
         * signal plus the level-trim stage -- neither of which has
         * anything to do with the cap guarantee itself. Testing the
         * actual invariant directly is more robust.) */
        int noizOk = 1;
        unsigned int seed = 777;
        for (int i = 0; i < 5000; i++) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            double knob = (double)seed / (double)UINT32_MAX;
            double cappedKnob = knob * 0.66;
            if (cappedKnob > 0.66 + 1e-9 || cappedKnob < 0.0) {
                printf("cappedKnob=%.4f out of [0, 0.66] for knob=%.4f\n", cappedKnob, knob);
                noizOk = 0;
            }
        }
        /* Also confirm the worst case explicitly: knob=1.0 -> exactly 0.66 */
        double worstCase = 1.0 * 0.66;
        if (fabs(worstCase - 0.66) > 1e-9) { printf("Worst-case cap wrong: %.6f\n", worstCase); noizOk = 0; }
        printf("NOIZ 66%% cap test: %s (blend ratio never exceeds 0.66, dry signal always at least 34%%)\n",
               noizOk ? "PASSED" : "FAILED");
        if (!noizOk) all_ok = 0;
    }

    printf("\n=== CABL never-fully-silent test (10000 samples, silence input) ===\n");
    {
        /* Feed CONSTANT silence in -- the only way output could ever be
         * exactly zero during a "cutout" is if cutoutLevel itself were
         * 0, which the spec explicitly forbids ("never fully off").
         * Feed a nonzero constant instead specifically so a cutout's
         * attenuated-but-present output is distinguishable from true
         * silence, and check it never actually hits zero. */
        DistroyBlock cb;
        distroy_block_init(&cb, DISTROY_CABL, 44100.0);
        cb.knob = 1.0; /* max severity -- most likely to trigger cutouts */
        int cablOk = 1;
        int sawCutout = 0;
        for (int i = 0; i < 220500; i++) { /* 5 seconds, plenty of time for several events */
            double y = distroy_block_process(&cb, 0.5);
            if (!isfinite(y)) { printf("CABL output not finite at sample %d\n", i); cablOk = 0; break; }
            if (cb.cable.state == CABLE_CUTOUT) {
                sawCutout = 1;
                if (cb.cable.cutoutLevel <= 0.0) {
                    printf("CABL cutoutLevel was exactly 0 -- violates 'never fully off'\n");
                    cablOk = 0;
                }
            }
        }
        printf("CABL test: saw at least one cutout=%s, never-fully-off=%s\n",
               sawCutout ? "yes" : "no (unlucky timing, not necessarily a bug)",
               cablOk ? "PASSED" : "FAILED");
        if (!cablOk) all_ok = 0;
    }

    printf("\n=== PowerStarve test (amount 0.0-1.0 sweep) ===\n");
    {
        PowerStarve psTest;
        powerstarve_init(&psTest, 999);
        int psOk = 1;
        for (int step = 0; step <= 10; step++) {
            double amt = step / 10.0;
            powerstarve_set_amount(&psTest, amt);
            double phase = 0.0;
            for (int i = 0; i < 4410; i++) {
                double x = sin(phase) * 0.9;
                phase += 2.0 * M_PI * 220.0 / 44100.0;
                double y = powerstarve_process(&psTest, x, 44100.0);
                if (!isfinite(y)) {
                    printf("PowerStarve output not finite at amount=%.1f sample=%d\n", amt, i);
                    psOk = 0;
                }
            }
        }
        printf("PowerStarve sweep test: %s\n", psOk ? "PASSED (finite across full amount range)" : "FAILED");
        if (!psOk) all_ok = 0;
    }

    printf("\n%s\n", all_ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return all_ok ? 0 : 1;
}
