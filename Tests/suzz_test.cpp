// suzz_test.cpp
// Offline checks for the TB Suzz DSP. Builds on Linux or macOS:
//   make -C Tests run      (writes demo WAVs to Tests/renders)
//
// 1. DC bias and the ranges of SUSTAIN and GAIN (from the drawing)
// 2. Small-signal response: band shape, SUSTAIN and GAIN are monotonic,
//    the 741's bandwidth narrows as SUSTAIN raises its gain
// 3. Clipping: the 741 hits its swing, a little lopsided on one battery
// 4. Sustain: a decaying note is held longer with SUSTAIN up
// 5. Footswitch: Effect Out is a straight-through bypass
// 6. Stability sweep: knob extremes x 10 mV-4 V x 41 Hz-15 kHz, plus noise
// 7. Automation of every control while playing, click-free pots, CPU, renders

#include "../Source/DSP/Suzz.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

using namespace tbsz;

static const double kPiT = 3.14159265358979323846;
static const int kSr = 44100;
static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static bool allFinite (const std::vector<float>& x, double limit)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (! (x[i] == x[i]) || std::fabs (x[i]) > limit) return false;
    return true;
}

static double rmsRange (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    if (to > x.size()) to = x.size();
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
}

static double peakRange (const std::vector<float>& x, size_t from, size_t to)
{
    double p = 0.0;
    if (to > x.size()) to = x.size();
    for (size_t i = from; i < to; ++i) p = std::max (p, (double) std::fabs (x[i]));
    return p;
}

static std::vector<float> sine (double f, double amp, double sec)
{
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2.0 * kPiT * f * i / kSr));
    return v;
}

static std::vector<float> run (Suzz& a, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    for (size_t pos = 0; pos < in.size(); pos += 256)
    {
        const int n = (int) std::min ((size_t) 256, in.size() - pos);
        a.process (&in[pos], &out[pos], n);
    }
    return out;
}

static void defaults (Suzz& a)
{
    a.setGain (0.85); a.setSustain (0.75); a.setEffectOn (true);
    a.setInputGainDb (0.0); a.setOutputGainDb (0.0);
}

// Small-signal gain (dB) from the input jack to the output jack
static double gainDb (Suzz& a, double f)
{
    a.reset();
    const double amp = 1.0e-4;
    std::vector<float> y = run (a, sine (f, amp, 0.5));
    return 20.0 * std::log10 (peakRange (y, kSr / 4, kSr / 2) / amp + 1e-30);
}

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const unsigned dataBytes = (unsigned) x.size() * 2, riff = 36 + dataBytes;
    const unsigned short fmt = 1, ch = 1, bits = 16, align = 2;
    const unsigned rate = (unsigned) sr, bps = rate * 2, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f); std::fwrite (&align, 2, 1, f);
    std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i] * 0.9;
        v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
        const short s = (short) std::floor (v * 32767.0 + 0.5);
        std::fwrite (&s, 2, 1, f);
    }
    std::fclose (f);
}

// A decaying plucked string (Karplus-Strong), added into buf at start
static void pluck (std::vector<float>& buf, size_t start, double freq, double amp, double dur)
{
    const int period = std::max (2, (int) (kSr / freq + 0.5));
    std::vector<double> line (period);
    unsigned seed = (unsigned) (freq * 1000.0) + (unsigned) start;
    for (int i = 0; i < period; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        line[i] = ((seed >> 9) / 4194304.0 - 1.0) * amp;
    }
    const size_t n = (size_t) (dur * kSr);
    int idx = 0;
    double prev = 0.0;
    for (size_t i = 0; i < n && start + i < buf.size(); ++i)
    {
        const double cur = line[idx];
        const double next = 0.996 * 0.5 * (cur + prev);
        prev = cur;
        line[idx] = next;
        buf[start + i] += (float) cur;
        if (++idx >= period) idx = 0;
    }
}

static std::vector<float> makeChords (double seconds)
{
    std::vector<float> v ((size_t) (seconds * kSr), 0.0f);
    const double chords[4][3] = { { 82.4, 123.5, 164.8 }, { 110.0, 164.8, 220.0 },
                                  { 98.0, 146.8, 196.0 }, { 73.4, 110.0, 146.8 } };
    for (int c = 0; c * 2.0 < seconds; ++c)
        for (int s = 0; s < 3; ++s)
            pluck (v, (size_t) ((c * 2.0 + s * 0.012) * kSr), chords[c % 4][s], 0.22, 2.0);
    return v;
}

static std::vector<float> makeRiff (double seconds)
{
    std::vector<float> v ((size_t) (seconds * kSr), 0.0f);
    const double notes[8] = { 82.4, 98.0, 110.0, 123.5, 110.0, 98.0, 146.8, 130.8 };
    for (int i = 0; i * 0.25 < seconds; ++i)
        pluck (v, (size_t) (i * 0.25 * kSr), notes[i % 8], 0.35, 0.6);
    return v;
}

static std::vector<float> makeLead (double seconds)
{
    std::vector<float> v ((size_t) (seconds * kSr), 0.0f);
    const double notes[6] = { 329.6, 392.0, 440.0, 392.0, 493.9, 440.0 };
    for (int i = 0; i * 1.0 < seconds; ++i)
        pluck (v, (size_t) (i * 1.0 * kSr), notes[i % 6], 0.3, 1.0);
    return v;
}

static std::vector<float> noise (double amp, double sec, unsigned seed)
{
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        v[i] = (float) (amp * ((seed >> 8) / 8388608.0 - 1.0));
    }
    return v;
}

int main (int argc, char** argv)
{
    const std::string renderDir = argc > 1 ? argv[1] : "renders";
    Suzz* ap = new Suzz();
    Suzz& a = *ap;
    a.prepare (kSr);

    std::printf ("1. Bias and control ranges\n");
    {
        std::printf ("  IC1 bias %.2f V, swing %.2f .. %.2f V\n", Suzz::biasVolts(),
                     Suzz::biasVolts() - Suzz::swingDown(), Suzz::biasVolts() + Suzz::swingUp());
        check (std::fabs (Suzz::biasVolts() - 4.5) < 1e-9, "R1 / R2 bias pin 3 at half the 9 V battery");
        check (Suzz::biasVolts() + Suzz::swingUp() < 9.0 - 1.0 && Suzz::biasVolts() - Suzz::swingDown() > 1.0,
               "741 swing stays more than 1 V inside the rails");
        const double gMin = Suzz::ic1Gain (0.0), gMid = Suzz::ic1Gain (0.5), gMax = Suzz::ic1Gain (1.0);
        std::printf ("  IC1 gain: min x%.1f (%.1f dB), mid x%.0f (%.1f dB), max x%.0f (%.1f dB)\n",
                     gMin, 20 * std::log10 (gMin), gMid, 20 * std::log10 (gMid), gMax, 20 * std::log10 (gMax));
        check (std::fabs (gMin - (1.0 + 470.0e3 / 22470.0)) < 0.01, "SUSTAIN min: 1 + R3 / (R4 + 22K)");
        check (std::fabs (gMax - 1001.0) < 0.01, "SUSTAIN max: 1 + R3 / R4");
        bool mono = true;
        for (int k = 1; k <= 20; ++k)
            if (Suzz::ic1Gain (k / 20.0) <= Suzz::ic1Gain ((k - 1) / 20.0)) mono = false;
        check (mono, "SUSTAIN raises the gain all the way round");
        const double dbLo = 20 * std::log10 (gMid / gMin), dbHi = 20 * std::log10 (gMax / gMid);
        check (dbLo > 0.5 * dbHi && dbHi > 0.5 * dbLo, "reverse log track: gain climbs fairly evenly in dB");
        check (Suzz::gainTaper (0.0) == 0.0 && Suzz::gainTaper (1.0) == 1.0
               && std::fabs (Suzz::gainTaper (0.5) - 0.1) < 1e-6, "GAIN: audio taper, 10% at mid-rotation");
    }

    std::printf ("\n2. Small-signal response\n");
    {
        double prevMid = -1e9;
        bool sustainUp = true, bwNarrows = true;
        double prevTop = 1e9;
        for (int k = 0; k <= 4; ++k)
        {
            defaults (a);
            a.setGain (1.0);
            a.setSustain (k / 4.0);
            const double fs[9] = { 50, 100, 200, 400, 800, 1600, 3200, 6400, 12000 };
            double g[9], pk = -1e9;
            int pkI = 0;
            std::printf ("  SUSTAIN %4.1f:", k * 2.5);
            for (int i = 0; i < 9; ++i)
            {
                g[i] = gainDb (a, fs[i]);
                if (g[i] > pk) { pk = g[i]; pkI = i; }
                std::printf (" %g:%+.1f", fs[i], g[i]);
            }
            std::printf ("\n");
            if (pk <= prevMid) sustainUp = false;
            prevMid = pk;
            // Upper -6 dB point
            double top = fs[8];
            for (int i = pkI; i < 9; ++i) if (g[i] < pk - 6.0) { top = fs[i]; break; }
            if (top > prevTop) bwNarrows = false;
            prevTop = top;
            if (k == 0 || k == 4)
            {
                check (g[0] < pk - 9.0, "bass rolled off (C1 into 60K, C2 into SUSTAIN)");
                check (g[8] < pk - 12.0, "treble rolled off (C3, the 741, R6 / C5)");
                check (pkI >= 2 && pkI <= 5, "peak response in the 200-1600 Hz range");
            }
            if (k == 0)
                check (std::fabs (pk - (20 * std::log10 (Suzz::ic1Gain (0.0) * 470.0 / 3770.0))) < 4.0,
                       "SUSTAIN min: mid-band gain = IC1 x the R6 / P2 divider");
        }
        check (sustainUp, "SUSTAIN raises the mid-band gain");
        check (bwNarrows, "the 741's bandwidth narrows as its gain rises");

        defaults (a);
        a.setSustain (0.5);
        double prev = -1e9;
        bool gainUp = true;
        for (int k = 1; k <= 10; ++k)
        {
            a.setGain (k / 10.0);
            const double g = gainDb (a, 800.0);
            if (g <= prev) gainUp = false;
            prev = g;
        }
        check (gainUp, "GAIN raises the output level all the way round");
        a.setGain (0.0);
        a.reset();
        std::vector<float> y = run (a, sine (440.0, 0.3, 0.5));
        check (peakRange (y, kSr / 4, kSr / 2) < 1e-6, "GAIN at min is silent");
    }

    std::printf ("\n3. Clipping\n");
    {
        defaults (a);
        a.setGain (1.0); a.setSustain (1.0);
        a.reset();
        std::vector<float> y = run (a, sine (196.0, 0.2, 1.0));
        double mx = 0.0, mn = 0.0;
        for (size_t i = kSr / 2; i < y.size(); ++i) { mx = std::max (mx, (double) y[i]); mn = std::min (mn, (double) y[i]); }
        std::printf ("  G string, 200 mV, SUSTAIN max: output %+.3f / %+.3f V\n", mx, mn);
        check (mx < 0.5 && -mn < 0.5 && mx > 0.2, "clipped at the 741's swing, through the output divider");
        check (std::fabs (mx / -mn - Suzz::swingUp() / Suzz::swingDown()) < 0.15, "a little lopsided, as on one 9 V battery");

        // Clean at low SUSTAIN with a quiet note; fuzzed at high SUSTAIN
        defaults (a);
        a.setGain (1.0); a.setSustain (0.0);
        a.reset();
        std::vector<float> lo = run (a, sine (440.0, 0.02, 0.5));
        double h3lo, h3hi;
        {
            // 3rd harmonic relative to the fundamental by correlation
            double c1r = 0, c1i = 0, c3r = 0, c3i = 0;
            for (size_t i = kSr / 4; i < lo.size(); ++i)
            {
                const double t = 2.0 * kPiT * 440.0 * i / kSr;
                c1r += lo[i] * std::cos (t); c1i += lo[i] * std::sin (t);
                c3r += lo[i] * std::cos (3 * t); c3i += lo[i] * std::sin (3 * t);
            }
            h3lo = std::sqrt ((c3r * c3r + c3i * c3i) / (c1r * c1r + c1i * c1i));
        }
        a.setSustain (1.0);
        a.reset();
        std::vector<float> hi = run (a, sine (440.0, 0.02, 0.5));
        {
            double c1r = 0, c1i = 0, c3r = 0, c3i = 0;
            for (size_t i = kSr / 4; i < hi.size(); ++i)
            {
                const double t = 2.0 * kPiT * 440.0 * i / kSr;
                c1r += hi[i] * std::cos (t); c1i += hi[i] * std::sin (t);
                c3r += hi[i] * std::cos (3 * t); c3i += hi[i] * std::sin (3 * t);
            }
            h3hi = std::sqrt ((c3r * c3r + c3i * c3i) / (c1r * c1r + c1i * c1i));
        }
        std::printf ("  20 mV at 440 Hz: 3rd harmonic %.3f%% at SUSTAIN min, %.1f%% at max\n", 100 * h3lo, 100 * h3hi);
        check (h3lo < 0.01, "clean with a quiet note at SUSTAIN min");
        check (h3hi > 0.15, "fuzz at SUSTAIN max");
    }

    std::printf ("\n4. Sustain\n");
    {
        double hold[2];
        for (int k = 0; k < 2; ++k)
        {
            defaults (a);
            a.setGain (1.0); a.setSustain (k == 0 ? 0.0 : 1.0);
            a.reset();
            std::vector<float> x (4 * kSr, 0.0f);
            pluck (x, 0, 196.0, 0.3, 4.0);
            std::vector<float> y = run (a, x);
            const double pk = peakRange (y, 0, kSr / 4);
            hold[k] = 0.0;
            for (size_t blk = 0; blk + 2205 <= y.size(); blk += 2205)
                if (peakRange (y, blk, blk + 2205) > 0.5 * pk) hold[k] = (blk + 2205) / (double) kSr;
        }
        std::printf ("  a plucked G holds above half its peak for %.2f s at SUSTAIN min, %.2f s at max\n", hold[0], hold[1]);
        check (hold[1] > 2.0 * hold[0], "SUSTAIN holds a decaying note much longer");
    }

    std::printf ("\n5. Footswitch\n");
    {
        defaults (a);
        a.setEffectOn (false);
        a.reset();
        // Tones (the oversampler's delay is a fraction of a sample off the
        // reported latency, which matters only for broadband material)
        std::vector<float> x = sine (196.0, 0.2, 1.0), x2 = sine (523.3, 0.1, 1.0);
        for (size_t i = 0; i < x.size(); ++i) x[i] += x2[i];
        std::vector<float> y = run (a, x);
        const int lat = Suzz::latencySamples();
        double d = 1e9;
        for (int L = lat - 1; L <= lat; ++L)
        {
            double m = 0.0;
            for (size_t i = kSr / 4; i < x.size(); ++i) m = std::max (m, (double) std::fabs (y[i] - x[i - L]));
            d = std::min (d, m);
        }
        std::printf ("  Effect Out vs input (latency %d): max diff %.3g\n", lat, d);
        check (d < 0.01, "Effect Out passes the input straight through");
    }

    std::printf ("\n6. Stability sweep\n");
    {
        const double amps[4] = { 0.01, 0.1, 1.0, 4.0 };
        const double freqs[5] = { 41.0, 110.0, 440.0, 2000.0, 15000.0 };
        const double corners[4][2] = { { 0, 0 }, { 1, 1 }, { 0, 1 }, { 1, 0 } };
        int runs = 0; bool ok = true;
        for (int c = 0; c < 4; ++c)
            for (int ai = 0; ai < 4; ++ai)
                for (int fi = 0; fi < 5; ++fi)
                {
                    a.setGain (corners[c][0]); a.setSustain (corners[c][1]);
                    a.setEffectOn (true);
                    a.reset();
                    std::vector<float> y = run (a, sine (freqs[fi], amps[ai], 0.4));
                    if (! allFinite (y, 10.0)) ok = false;
                    ++runs;
                }
        unsigned seed = 1;
        for (int r = 0; r < 20; ++r)
        {
            seed = seed * 1664525u + 1013904223u; const double f1 = (seed >> 8) / 16777216.0;
            seed = seed * 1664525u + 1013904223u; const double f2 = (seed >> 8) / 16777216.0;
            a.setGain (f1); a.setSustain (f2);
            a.reset();
            std::vector<float> y = run (a, noise (2.0, 0.4, seed));
            if (! allFinite (y, 10.0)) ok = false;
            ++runs;
        }
        std::printf ("  %d runs\n", runs);
        check (ok, "no NaNs or runaways anywhere");
    }

    std::printf ("\n7. Automation, click-free pots, CPU, renders\n");
    {
        defaults (a);
        a.reset();
        std::vector<float> x = makeRiff (4.0), y (x.size());
        for (size_t pos = 0; pos < x.size(); pos += 64)
        {
            const double t = (double) pos / x.size();
            a.setGain (0.5 + 0.5 * std::sin (2 * kPiT * 3 * t));
            a.setSustain (0.5 + 0.5 * std::sin (2 * kPiT * 5 * t));
            a.setEffectOn (std::sin (2 * kPiT * 4 * t) > 0.0);
            a.setInputGainDb (12.0 * std::sin (2 * kPiT * 2 * t));
            a.setOutputGainDb (12.0 * std::sin (2 * kPiT * 9 * t));
            const int n = (int) std::min ((size_t) 64, x.size() - pos);
            a.process (&x[pos], &y[pos], n);
        }
        check (allFinite (y, 10.0), "every control automated while playing");

        // Turning SUSTAIN with no signal makes no clicks
        defaults (a);
        a.setGain (1.0);
        a.reset();
        std::vector<float> z (2 * kSr, 0.0f), oz (z.size());
        for (size_t pos = 0; pos < z.size(); pos += 64)
        {
            const double t = (double) pos / z.size();
            a.setSustain (0.5 + 0.5 * std::sin (2 * kPiT * 3 * t));
            a.process (&z[pos], &oz[pos], (int) std::min ((size_t) 64, z.size() - pos));
        }
        std::printf ("  pots turned in silence: output peak %.2g\n", peakRange (oz, 0, oz.size()));
        check (peakRange (oz, 0, oz.size()) < 1e-6, "pot moves are click-free");

        defaults (a);
        a.reset();
        std::vector<float> ten = makeChords (10.0);
        std::clock_t t0 = std::clock();
        std::vector<float> yc = run (a, ten);
        const double secs = (double) (std::clock() - t0) / CLOCKS_PER_SEC;
        std::printf ("  CPU: %.1f%% of one core at 44.1 kHz\n", 10.0 * secs);
        check (secs < 3.0, "runs well under real time");

        struct R { const char* name; double g, s; bool on; int src; };
        const R renders[] = {
            { "dry_riff",            0.85, 0.75, false, 0 },
            { "sustain_min_riff",    1.0, 0.0,  true,  0 },
            { "sustain_mid_riff",    0.9, 0.5,  true,  0 },
            { "default_riff",        0.85, 0.75, true,  0 },
            { "sustain_max_chords",  0.85, 1.0,  true,  1 },
            { "sustain_max_lead",    0.85, 1.0,  true,  2 },
        };
        std::vector<float> riff = makeRiff (8.0), chords = makeChords (8.0), lead = makeLead (6.0);
        for (size_t k = 0; k < sizeof (renders) / sizeof (renders[0]); ++k)
        {
            defaults (a);
            a.setGain (renders[k].g); a.setSustain (renders[k].s); a.setEffectOn (renders[k].on);
            a.reset();
            const std::vector<float>& src = renders[k].src == 0 ? riff : (renders[k].src == 1 ? chords : lead);
            std::vector<float> o = run (a, src);
            writeWav (renderDir + "/" + renders[k].name + ".wav", o, kSr);
        }
        check (true, "demo renders written");
    }

    delete ap;
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL DSP TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
