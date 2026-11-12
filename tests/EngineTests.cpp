// Self-contained tests for the BAXR DSP (no JUCE needed).
//   g++ -O2 -std=c++17 -I Source tests/EngineTests.cpp -o engine_tests && ./engine_tests
// Exit code 0 = all checks passed.

#include "DSP/BaxEngine.h"
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace bax;

static int failures = 0, checks = 0;

static void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("  FAIL  %s\n", what.c_str()); }
}

static const double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };

//==============================================================================
// Analogue reference of a whole channel (HP/LP Butterworth, 1st-order shelves).
static double analogueDb (const ChannelSettings& s, double f)
{
    double db = design::shelfAnalogueDb (false, kLsFreqs[s.lsFreq] * kLsMidpointRatio, s.lsGainDb, f)
              + design::shelfAnalogueDb (true,  kHsFreqs[s.hsFreq] * kHsMidpointRatio, s.hsGainDb, f);
    const double w = 2.0 * design::kPi * f;
    if (s.hp > 0) db += 10.0 * std::log10 (design::highPassAnalogueMag2 (kHpFreqs[s.hp], kFilterQ, w));
    if (s.lp > 0) db += 10.0 * std::log10 (design::lowPassAnalogueMag2 (kLpFreqs[s.lp], kFilterQ, w));
    return db;
}

static void testFilterDesignsAgainstAnalogue()
{
    std::printf ("[1] Digital EQ vs analogue prototype, every detent, gains -5/-2.5/+2.5/+5, every native rate\n");
    std::printf ("    (points where the prototype is below -30 dB, i.e. deep HP/LP stop-band, are skipped)\n");
    for (double fs : kRates)
    {
        DesignTable table;
        table.build (fs);
        double worstAudio = 0.0, worstExt = 0.0;
        std::string caseAudio, caseExt;
        for (int band = 0; band < 4; ++band)
            for (int d = 0; d < kNumDetents; ++d)
                for (double g : { -5.0, -2.5, 2.5, 5.0 })
                {
                    ChannelSettings s;
                    if (band == 0) { s.hp = d; }
                    if (band == 1) { s.lsFreq = d; s.lsGainDb = g; }
                    if (band == 2) { s.hsFreq = d; s.hsGainDb = g; }
                    if (band == 3) { s.lp = d; }
                    for (double f = 20.0; f <= design::fitTop (fs); f *= 1.02)
                    {
                        const double ref = analogueDb (s, f);
                        if (ref < -30.0) continue;
                        const double err = std::abs (EqChannel::responseDb (s, fs, table, f) - ref);
                        static const char* names[] = { "HP", "LS", "HS", "LP" };
                        char buf[160];
                        std::snprintf (buf, sizeof buf, "%s detent %d gain %+.1f at %.0f Hz", names[band], d, g, f);
                        if (f <= 20000.0 && err > worstAudio) { worstAudio = err; caseAudio = buf; }
                        if (err > worstExt) { worstExt = err; caseExt = buf; }
                    }
                }
        std::printf ("      fs %6.0f: 20 Hz-20 kHz worst %.4f dB (%s)\n", fs, worstAudio, caseAudio.c_str());
        if (fs > 50000.0)
            std::printf ("                 to 0.45 Fs   worst %.4f dB (%s)\n", worstExt, caseExt.c_str());
        check (worstAudio < (fs <= 50000.0 ? 0.25 : 0.06), "audio band magnitude at " + std::to_string ((int) fs));
        check (worstExt < 0.5, "extended band magnitude at " + std::to_string ((int) fs));
    }
}

//==============================================================================
static EngineSettings flatSettings (Oversampling os)
{
    EngineSettings s;
    s.oversampling = os;
    return s;
}

static std::vector<double> render (double fs, const EngineSettings& s, const std::vector<double>& inL,
                                   const std::vector<double>& inR, std::vector<double>& outR, int blockSize,
                                   bool variableBlocks = false)
{
    BaxEngine e;
    e.prepare (fs, s);
    std::vector<double> l = inL; outR = inR;
    std::mt19937 rng (7);
    size_t pos = 0;
    while (pos < l.size())
    {
        int n = variableBlocks ? 1 + (int) (rng() % 997) : blockSize;
        n = (int) std::min<size_t> ((size_t) n, l.size() - pos);
        e.setSettings (s);
        e.process (l.data() + pos, outR.data() + pos, n);
        pos += (size_t) n;
    }
    return l;
}

static std::vector<double> noise (size_t n, double rms, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<double> d (0.0, rms);
    std::vector<double> v (n);
    for (auto& x : v) x = d (rng);
    return v;
}

static void testLatency()
{
    std::printf ("[2] Latency (impulse peak and cross-correlation), every rate and oversampling factor\n");
    for (double fs : kRates)
        for (auto os : { Oversampling::x1, Oversampling::x2, Oversampling::x4 })
        {
            const size_t n = 4096;
            std::vector<double> in (n, 0.0), r;
            in[100] = 0.01;   // small, so the preamp is linear
            auto out = render (fs, flatSettings (os), in, in, r, 512);
            size_t peak = 0;
            for (size_t i = 0; i < n; ++i) if (std::abs (out[i]) > std::abs (out[peak])) peak = i;
            check ((int) peak - 100 == BaxEngine::kLatency,
                   "latency " + std::to_string ((int) peak - 100) + " at " + std::to_string ((int) fs) + " os " + std::to_string ((int) os));
        }
    std::printf ("      reported latency = %d samples at every rate and factor\n", BaxEngine::kLatency);
}

static void testBlockSizeAndDeterminism()
{
    std::printf ("[3] Determinism and block-size independence\n");
    const double fs = 48000.0;
    EngineSettings s;
    s.a = { 3, 2, 5, 4, 4.0, -3.0 };
    s.b = { 1, 6, 1, 2, -2.0, 5.0 };
    s.oversampling = Oversampling::x4;
    s.outputDb = 1.5;
    auto inL = noise (48000, 0.15, 1), inR = noise (48000, 0.15, 2);

    std::vector<double> refR;
    auto refL = render (fs, s, inL, inR, refR, 512);
    std::vector<double> againR;
    auto againL = render (fs, s, inL, inR, againR, 512);
    check (againL == refL && againR == refR, "two renders bit-identical");

    for (int bs : { 1, 32, 64, 128, 441, 1024, 4096 })
    {
        std::vector<double> r;
        auto l = render (fs, s, inL, inR, r, bs);
        check (l == refL && r == refR, "block size " + std::to_string (bs) + " bit-identical");
    }
    std::vector<double> vr;
    auto vl = render (fs, s, inL, inR, vr, 0, true);
    check (vl == refL && vr == refR, "variable block sizes bit-identical");
}

//==============================================================================
static double toneAmplitude (const std::vector<double>& x, size_t start, size_t len, double f, double fs)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < len; ++i)
    {
        // Hann window
        const double w = 0.5 - 0.5 * std::cos (2.0 * design::kPi * (double) i / (double) len);
        const double ph = 2.0 * design::kPi * f * (double) (start + i) / fs;
        re += w * x[start + i] * std::cos (ph);
        im += w * x[start + i] * std::sin (ph);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (0.5 * (double) len);
}

static void testEqThroughEngine()
{
    std::printf ("[4] Engine magnitude at -40 dBFS matches the EQ design (preamp linear region)\n");
    const double fs = 96000.0;
    EngineSettings s;
    s.a = { 2, 3, 2, 3, 5.0, -5.0 };
    s.b = s.a;
    for (auto os : { Oversampling::x1, Oversampling::x2, Oversampling::x4 })
    {
        s.oversampling = os;
        DesignTable table;
        table.build (fs);
        double worst = 0.0;
        for (double f : { 31.0, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
        {
            const size_t n = 96000;
            std::vector<double> in (n), r;
            for (size_t i = 0; i < n; ++i) in[i] = 0.01 * std::sin (2.0 * design::kPi * f * (double) i / fs);
            auto out = render (fs, s, in, in, r, 512);
            const double got = 20.0 * std::log10 (toneAmplitude (out, n / 2, n / 2, f, fs) / 0.01);
            const double want = EqChannel::responseDb (s.a, fs, table, f)
                              + 20.0 * std::log10 (2.0 * design::kPi * f / std::hypot (2.0 * design::kPi * f, 2.0 * design::kPi * BaxEngine::kDcBlockHz));
            worst = std::max (worst, std::abs (got - want));
        }
        std::printf ("      os %dx: worst error %.4f dB\n", 1 << (int) os, worst);
        check (worst < 0.05, "engine magnitude within 0.05 dB");
    }
}

static void testHarmonicsAndAliasing()
{
    std::printf ("[5] Preamp harmonics (1 kHz, flat EQ) and oversampling alias rejection\n");
    const double fs = 48000.0;
    for (double levelDb : { -18.0, -6.0, 0.0 })
    {
        const double a = std::pow (10.0, levelDb / 20.0);
        const size_t n = 48000;
        std::vector<double> in (n), r;
        for (size_t i = 0; i < n; ++i) in[i] = a * std::sin (2.0 * design::kPi * 1000.0 * (double) i / fs);
        auto out = render (fs, flatSettings (Oversampling::x4), in, in, r, 512);
        const double h1 = toneAmplitude (out, 24000, 24000, 1000.0, fs);
        const double h2 = toneAmplitude (out, 24000, 24000, 2000.0, fs);
        const double h3 = toneAmplitude (out, 24000, 24000, 3000.0, fs);
        std::printf ("      %+5.0f dBFS: gain %+.3f dB, H2 %.1f dBc, H3 %.1f dBc\n", levelDb,
                     20.0 * std::log10 (h1 / a), 20.0 * std::log10 (h2 / h1), 20.0 * std::log10 (h3 / h1));
        if (levelDb == -18.0)
            check (20.0 * std::log10 (h2 / h1) < -55.0 && 20.0 * std::log10 (h3 / h1) < -60.0, "clean at -18 dBFS");
    }

    // 15 kHz at 0 dBFS: H3 = 45 kHz aliases to 3 kHz at 1x; check it shrinks with oversampling
    double alias[3];
    for (int k = 0; k < 3; ++k)
    {
        const size_t n = 48000;
        std::vector<double> in (n), r;
        for (size_t i = 0; i < n; ++i) in[i] = std::sin (2.0 * design::kPi * 15000.0 * (double) i / fs);
        auto out = render (fs, flatSettings ((Oversampling) k), in, in, r, 512);
        alias[k] = 20.0 * std::log10 (toneAmplitude (out, 24000, 24000, 3000.0, fs) / toneAmplitude (out, 24000, 24000, 15000.0, fs));
    }
    std::printf ("      15 kHz at 0 dBFS, alias at 3 kHz: 1x %.1f dBc, 2x %.1f dBc, 4x %.1f dBc\n", alias[0], alias[1], alias[2]);
    check (alias[2] < alias[0] - 20.0 && alias[2] < -90.0, "4x oversampling suppresses aliasing");
}

static double maxStep (const std::vector<double>& x, size_t from, size_t to)
{
    double m = 0.0;
    for (size_t i = from + 1; i < to; ++i) m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

static void testTransitions()
{
    std::printf ("[6] Parameter changes are click-free; Engage Off is a latency-aligned true bypass\n");
    const double fs = 48000.0, f = 100.0;
    const size_t n = 96000;
    std::vector<double> in (n);
    for (size_t i = 0; i < n; ++i) in[i] = 0.25 * std::sin (2.0 * design::kPi * f * (double) i / fs);
    const double steadyStep = 0.25 * 2.0 * design::kPi * f / fs * 1.8;  // max slope with +5 dB boost

    struct Change { const char* name; void (*apply) (EngineSettings&); };
    const Change changes[] =
    {
        { "LS freq detent",  [] (EngineSettings& s) { s.a.lsFreq = 7; s.b.lsFreq = 7; } },
        { "HP detent",       [] (EngineSettings& s) { s.a.hp = 7; s.b.hp = 7; } },
        { "LS gain jump",    [] (EngineSettings& s) { s.a.lsGainDb = -5.0; s.b.lsGainDb = -5.0; } },
        { "M/S toggle",      [] (EngineSettings& s) { s.midSide = true; } },
        { "Oversampling",    [] (EngineSettings& s) { s.oversampling = Oversampling::x4; } },
        { "Engage off",      [] (EngineSettings& s) { s.engagedA = s.engagedB = false; } },
        { "Output gain",     [] (EngineSettings& s) { s.outputDb = -10.0; } },
    };

    for (auto& c : changes)
    {
        EngineSettings s;
        s.a.lsGainDb = s.b.lsGainDb = 5.0;
        BaxEngine e;
        e.prepare (fs, s);
        std::vector<double> l = in, r = in;
        for (size_t pos = 0; pos < n; pos += 480)
        {
            if (pos == 48000) c.apply (s);
            e.setSettings (s);
            e.process (l.data() + pos, r.data() + pos, 480);
        }
        const double step = maxStep (l, 47000, 60000);
        std::printf ("      %-15s max sample step %.5f (steady-state %.5f)\n", c.name, step, steadyStep);
        check (step < steadyStep, std::string (c.name) + " is click-free");
    }

    // Engage off: after the 200 ms fade the output must be exactly the input delayed by kLatency
    EngineSettings s;
    s.a.lsGainDb = 5.0;
    s.engagedA = s.engagedB = false;
    std::vector<double> r;
    auto out = render (fs, s, in, in, r, 256);
    double err = 0.0;
    for (size_t i = 20000; i < n; ++i) err = std::max (err, std::abs (out[i] - in[i - BaxEngine::kLatency]));
    check (err == 0.0, "Engage Off is bit-exact delayed dry");
}

static void testChannelsAndMidSide()
{
    std::printf ("[7] Independent channels and Mid/Side\n");
    const double fs = 48000.0;
    EngineSettings s;
    s.a.lsGainDb = 5.0;   // A only
    auto inL = noise (48000, 0.01, 3);
    std::vector<double> r;
    auto l = render (fs, s, inL, inL, r, 512);
    double diff = 0.0;
    for (size_t i = 0; i < l.size(); ++i) diff = std::max (diff, std::abs (l[i] - r[i]));
    check (diff > 1.0e-3, "L and R processed independently in L/R mode");

    // M/S with a side-only boost leaves a mono signal untouched (side = 0)
    s = EngineSettings();
    s.midSide = true;
    s.b.hsGainDb = 5.0;
    std::vector<double> flatR, msR;
    auto flatL = render (fs, EngineSettings(), inL, inL, flatR, 512);
    auto msL = render (fs, s, inL, inL, msR, 512);
    double e = 0.0;
    for (size_t i = 0; i < msL.size(); ++i) e = std::max (e, std::abs (msL[i] - flatL[i]));
    check (e < 1.0e-12, "side EQ has no effect on mono input in M/S mode");
}

static void testSilence()
{
    std::printf ("[8] Silent input gives silent output (no noise floor)\n");
    std::vector<double> z (48000, 0.0), r;
    auto l = render (48000.0, flatSettings (Oversampling::x4), z, z, r, 512);
    double m = 0.0;
    for (size_t i = 0; i < l.size(); ++i) m = std::max ({ m, std::abs (l[i]), std::abs (r[i]) });
    check (m == 0.0, "digital silence in, digital silence out");
}

int main()
{
    testFilterDesignsAgainstAnalogue();
    testLatency();
    testBlockSizeAndDeterminism();
    testEqThroughEngine();
    testHarmonicsAndAliasing();
    testTransitions();
    testChannelsAndMidSide();
    testSilence();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
