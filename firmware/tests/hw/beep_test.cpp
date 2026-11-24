#include "beep.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int total(const BeepStep *s, int n) { int t = 0; for (int i = 0; i < n; i++) t += s[i].ms; return t; }

int main() {
    BeepStep out[BEEP_MAX_STEPS];
    {   // a normal sequence passes unchanged
        long hz[] = {1800, 0, 2400}, ms[] = {120, 60, 160};
        int n = beep_clamp(hz, ms, 3, out);
        CHECK(n == 3 && out[0].hz == 1800 && out[0].ms == 120 && out[1].hz == 0 && out[2].hz == 2400);
    }
    {   // frequency clamp: 0 stays a pause, anything else is pulled into 100..5000
        long hz[] = {1, 99999, -5, 0}, ms[] = {50, 50, 50, 50};
        int n = beep_clamp(hz, ms, 4, out);
        CHECK(n == 4 && out[0].hz == BEEP_MIN_HZ && out[1].hz == BEEP_MAX_HZ && out[2].hz == 0 && out[3].hz == 0);
    }
    {   // one step never lasts longer than 2 s
        long hz[] = {1000}, ms[] = {60000};
        int n = beep_clamp(hz, ms, 1, out);
        CHECK(n == 1 && out[0].ms == BEEP_MAX_STEP_MS);
    }
    {   // steps without a positive duration are dropped
        long hz[] = {1000, 1000, 1000}, ms[] = {0, -10, 30};
        int n = beep_clamp(hz, ms, 3, out);
        CHECK(n == 1 && out[0].ms == 30);
    }
    {   // the whole sequence is cut at 5 s: 2000 + 2000 + 1000 (shortened), the rest is dropped
        long hz[] = {1000, 1000, 1000, 1000}, ms[] = {2000, 2000, 2000, 2000};
        int n = beep_clamp(hz, ms, 4, out);
        CHECK(n == 3 && total(out, n) == BEEP_MAX_TOTAL_MS && out[2].ms == 1000);
    }
    {   // at most 16 steps
        long hz[40], ms[40];
        for (int i = 0; i < 40; i++) { hz[i] = 1000; ms[i] = 10; }
        int n = beep_clamp(hz, ms, 40, out);
        CHECK(n == BEEP_MAX_STEPS);
    }
    {   // nothing in, nothing out
        CHECK(beep_clamp(nullptr, nullptr, 0, out) == 0);
    }
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
