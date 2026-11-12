#pragma once

// The whole BAXR signal path for one stereo stream. Pure C++, double precision.
//
//   in L/R ──► encode (L/R or M/S) ──► EQ A / EQ B ──► engage mix per channel ──► decode
//          ──► preamp at 1X / 2X / 4X (latency-compensated) ──► DC block ──► output gain
//          ──► true-bypass crossfade per output channel against the delayed dry signal
//
// Transitions
//   * shelf gains glide in dB over 20 ms
//   * frequency detent and M/S changes crossfade between the old and new EQ (30 ms)
//   * oversampling changes crossfade between the old and new preamp path (30 ms)
//   * Engage on/off crossfades over 200 ms
//   * output gain glides over 20 ms
// All of it runs per sample, so with static settings the output does not depend on
// the host block size.

#include "EqChannel.h"
#include "Oversampler.h"

namespace bax
{
class BaxEngine
{
public:
    static constexpr int kLatency = PreampPath::kLatency;    // samples, every rate and every factor
    static constexpr double kEngageFadeSeconds = 0.2;
    static constexpr double kEqFadeSeconds = 0.03;
    static constexpr double kOsFadeSeconds = 0.03;
    static constexpr double kOutputGlideSeconds = 0.02;
    static constexpr double kDcBlockHz = 2.0;

    void prepare (double sampleRate, const EngineSettings& s)
    {
        fs = sampleRate;
        current = s;
        designTable.build (fs);
        for (auto* st : { &stage[0], &stage[1] })
        {
            st->a.prepare (fs, s.a, designTable);
            st->b.prepare (fs, s.b, designTable);
            st->midSide = s.midSide;
        }
        cur = 0;
        eqFadeLeft = 0;
        eqFadeLen = std::max (1, (int) std::lround (kEqFadeSeconds * fs));
        osFadeLen = std::max (1, (int) std::lround (kOsFadeSeconds * fs));
        engageStep = 1.0 / std::max (1.0, kEngageFadeSeconds * fs);

        const int f = factorOf (s.oversampling);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int p = 0; p < 2; ++p) { paths[ch][p].setFactor (f); paths[ch][p].reset(); }
            dry[ch].delay = kLatency;
            dry[ch].reset();
            dcX[ch] = dcY[ch] = 0.0;
            for (auto& v : osHistory[ch]) v = 0.0;
        }
        curPath = 0;
        osFadeLeft = 0;
        osHistPos = 0;

        dcCoeff = std::exp (-2.0 * design::kPi * kDcBlockHz / fs);

        engA = s.engagedA ? 1.0 : 0.0;
        engB = s.engagedB ? 1.0 : 0.0;
        bypassL = gateTargetL (s); bypassR = gateTargetR (s);

        outDb = outTarget = s.outputDb;
        outGain = design::dbToGain (outDb);
        outGlideLeft = 0;
    }

    /** Call before each block (or whenever settings may have changed). */
    void setSettings (const EngineSettings& s)
    {
        const bool detentsChanged = ! s.a.sameDetents (current.a) || ! s.b.sameDetents (current.b);
        const bool msChanged = s.midSide != current.midSide;

        if (detentsChanged || msChanged)
        {
            // the previous EQ keeps running and fades out; the new one fades in
            const int next = 1 - cur;
            stage[next] = stage[cur];
            stage[next].a.setDetents (s.a);
            stage[next].b.setDetents (s.b);
            if (msChanged)
            {
                stage[next].midSide = s.midSide;
                stage[next].a.reset();
                stage[next].b.reset();
            }
            cur = next;
            eqFadeLeft = eqFadeLen;
        }

        stage[cur].a.setGains (s.a.lsGainDb, s.a.hsGainDb, true);
        stage[cur].b.setGains (s.b.lsGainDb, s.b.hsGainDb, true);

        if (s.oversampling != current.oversampling)
        {
            const int next = 1 - curPath;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& p = paths[ch][next];
                p.setFactor (factorOf (s.oversampling));
                p.reset();
                // prime the new path with the recent input so it starts warm
                for (int i = 0; i < kHistory; ++i)
                    p.process (osHistory[ch][(size_t) ((osHistPos + i) % kHistory)]);
            }
            curPath = next;
            osFadeLeft = osFadeLen;
        }

        if (! sameValue (s.outputDb, outTarget))
        {
            outTarget = s.outputDb;
            outGlideLeft = std::max (1, (int) std::lround (kOutputGlideSeconds * fs));
            outStep = (outTarget - outDb) / outGlideLeft;
        }

        current = s;
    }

    /** In-place stereo processing. */
    void process (double* left, double* right, int numSamples) noexcept
    {
        const double engTargetA = current.engagedA ? 1.0 : 0.0;
        const double engTargetB = current.engagedB ? 1.0 : 0.0;
        const double gateL = gateTargetL (current), gateR = gateTargetR (current);

        for (int i = 0; i < numSamples; ++i)
        {
            const double inL = left[i], inR = right[i];

            engA = ramp (engA, engTargetA, engageStep);
            engB = ramp (engB, engTargetB, engageStep);
            bypassL = ramp (bypassL, gateL, engageStep);
            bypassR = ramp (bypassR, gateR, engageStep);

            // --- linear EQ ---------------------------------------------------------
            double l, r;
            stage[cur].process (inL, inR, engA, engB, l, r);
            if (eqFadeLeft > 0)
            {
                double pl, pr;
                stage[1 - cur].process (inL, inR, engA, engB, pl, pr);
                const double t = 1.0 - (double) eqFadeLeft / (double) eqFadeLen;
                l = pl + t * (l - pl);
                r = pr + t * (r - pr);
                --eqFadeLeft;
            }

            // --- preamp ------------------------------------------------------------
            osHistory[0][(size_t) osHistPos] = l;
            osHistory[1][(size_t) osHistPos] = r;
            osHistPos = (osHistPos + 1) % kHistory;

            double pl = paths[0][curPath].process (l);
            double pr = paths[1][curPath].process (r);
            if (osFadeLeft > 0)
            {
                const double ol = paths[0][1 - curPath].process (l);
                const double orr = paths[1][1 - curPath].process (r);
                const double t = 1.0 - (double) osFadeLeft / (double) osFadeLen;
                pl = ol + t * (pl - ol);
                pr = orr + t * (pr - orr);
                --osFadeLeft;
            }

            // --- DC block (the preamp's even harmonics produce a little DC) ---------
            pl = dcBlock (0, pl);
            pr = dcBlock (1, pr);

            // --- output gain --------------------------------------------------------
            if (outGlideLeft > 0)
            {
                if (--outGlideLeft == 0) outDb = outTarget; else outDb += outStep;
                outGain = design::dbToGain (outDb);
            }
            pl *= outGain;
            pr *= outGain;

            // --- true bypass (latency-aligned dry) ----------------------------------
            const double dl = dry[0].process (inL), dr = dry[1].process (inR);
            left[i]  = dl + bypassL * (pl - dl);
            right[i] = dr + bypassR * (pr - dr);
        }
    }

    /** Host-bypass path: just the latency-compensating delay. */
    void processBypassed (double* left, double* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = dry[0].process (left[i]);
            right[i] = dry[1].process (right[i]);
        }
    }

private:
    struct LinearStage
    {
        EqChannel a, b;
        bool midSide = false;

        inline void process (double inL, double inR, double eA, double eB, double& outL, double& outR) noexcept
        {
            const double xa = midSide ? 0.5 * (inL + inR) : inL;
            const double xb = midSide ? 0.5 * (inL - inR) : inR;
            const double ya = xa + eA * (a.process (xa) - xa);
            const double yb = xb + eB * (b.process (xb) - xb);
            outL = midSide ? ya + yb : ya;
            outR = midSide ? ya - yb : yb;
        }
    };

    static int factorOf (Oversampling o) { return o == Oversampling::x4 ? 4 : (o == Oversampling::x2 ? 2 : 1); }

    // In L/R mode each output channel is truly bypassed with its own Engage switch.
    // In M/S mode both outputs stay in circuit while either channel is engaged
    // (a disengaged M or S channel bypasses only its EQ).
    static double gateTargetL (const EngineSettings& s) { return (s.midSide ? (s.engagedA || s.engagedB) : s.engagedA) ? 1.0 : 0.0; }
    static double gateTargetR (const EngineSettings& s) { return (s.midSide ? (s.engagedA || s.engagedB) : s.engagedB) ? 1.0 : 0.0; }

    static inline double ramp (double v, double target, double step) noexcept
    {
        if (v < target) return std::min (target, v + step);
        if (v > target) return std::max (target, v - step);
        return v;
    }

    inline double dcBlock (int ch, double x) noexcept
    {
        const double y = x - dcX[ch] + dcCoeff * dcY[ch];
        dcX[ch] = x;
        dcY[ch] = y;
        return y;
    }

    static constexpr int kHistory = 192;

    double fs = 48000.0;
    EngineSettings current;
    DesignTable designTable;

    LinearStage stage[2];
    int cur = 0, eqFadeLeft = 0, eqFadeLen = 1;

    PreampPath paths[2][2];               // [channel][slot]
    int curPath = 0, osFadeLeft = 0, osFadeLen = 1;
    double osHistory[2][kHistory] {};
    int osHistPos = 0;

    DelayLine<kLatency> dry[2];
    double dcX[2] {}, dcY[2] {}, dcCoeff = 0.0;

    double engA = 1.0, engB = 1.0, bypassL = 1.0, bypassR = 1.0, engageStep = 0.0;
    double outDb = 0.0, outTarget = 0.0, outStep = 0.0, outGain = 1.0;
    int outGlideLeft = 0;
};

} // namespace bax
