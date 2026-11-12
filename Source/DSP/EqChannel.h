#pragma once

#include "FilterDesign.h"
#include "EqTables.h"

namespace bax
{
//==============================================================================
/** Per-sample-rate design data that is too slow to compute on the audio thread:
    the low-pass designs and the shelf match frequencies for every detent.
    build() takes some tens of milliseconds. */
struct DesignTable
{
    double sampleRate = 0.0;
    design::LowPassPair lp[kNumDetents];
    double lsMatch[kNumDetents] {}, hsMatch[kNumDetents] {};

    static double lsCentre (int d) { return kLsFreqs[d] * kLsMidpointRatio; }
    static double hsCentre (int d) { return kHsFreqs[d] * kHsMidpointRatio; }

    void build (double fs)
    {
        if (sameValue (fs, sampleRate)) return;
        for (int d = 0; d < kNumDetents; ++d)
        {
            if (d > 0) lp[d] = design::lowPassCorrected (kLpFreqs[d], kFilterQ, fs);
            lsMatch[d] = design::bestShelfMatchFrequency (false, lsCentre (d), fs);
            hsMatch[d] = design::bestShelfMatchFrequency (true,  hsCentre (d), fs);
        }
        sampleRate = fs;
    }
};

//==============================================================================
/** Transposed direct form II biquad, double precision state. */
struct BiquadState
{
    design::Biquad c;
    double s1 = 0.0, s2 = 0.0;

    inline double process (double x) noexcept
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() noexcept { s1 = s2 = 0.0; }
};

/** Direct form I biquad: the state is the actual past input/output, so the shelf
    gain glide (coefficients changing every sample) does not inject transients. */
struct BiquadDF1
{
    design::Biquad c;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    inline double process (double x) noexcept
    {
        const double y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
};

//==============================================================================
/** One EQ channel: HP -> low shelf -> high shelf -> LP. Shelf gains glide in dB. */
class EqChannel
{
public:
    static constexpr double kGainGlideSeconds = 0.02;

    /** The table must outlive the channel and be built for sampleRate. */
    void prepare (double sampleRate, const ChannelSettings& s, const DesignTable& designTable)
    {
        fs = sampleRate;
        table = &designTable;
        settings = s;
        lsDb = s.lsGainDb;
        hsDb = s.hsGainDb;
        lsTarget = lsDb;
        hsTarget = hsDb;
        glideLeft = 0;
        designDetents();
        designShelves();
        reset();
    }

    void reset() noexcept
    {
        hp.reset(); lp.reset(); lpCorr.reset(); ls.reset(); hs.reset();
    }

    /** New detents take effect immediately (the engine crossfades around this). */
    void setDetents (const ChannelSettings& s)
    {
        settings.hp = s.hp; settings.lsFreq = s.lsFreq; settings.hsFreq = s.hsFreq; settings.lp = s.lp;
        designDetents();
        designShelves();
    }

    void setGains (double lsGainDb, double hsGainDb, bool glide)
    {
        if (sameValue (lsGainDb, lsTarget) && sameValue (hsGainDb, hsTarget))
            return;

        lsTarget = lsGainDb;
        hsTarget = hsGainDb;

        if (! glide)
        {
            lsDb = lsTarget; hsDb = hsTarget; glideLeft = 0;
            designShelves();
            return;
        }

        glideLeft = std::max (1, (int) std::lround (kGainGlideSeconds * fs));
        lsStep = (lsTarget - lsDb) / glideLeft;
        hsStep = (hsTarget - hsDb) / glideLeft;
    }

    inline double process (double x) noexcept
    {
        if (glideLeft > 0)
        {
            if (--glideLeft == 0) { lsDb = lsTarget; hsDb = hsTarget; }
            else                  { lsDb += lsStep; hsDb += hsStep; }
            designShelves();
        }

        if (hpOn) x = hp.process (x);
        x = ls.process (x);
        x = hs.process (x);
        if (lpOn) x = lpCorr.process (lp.process (x));
        return x;
    }

    /** Magnitude of a channel with the given settings (dB), for display and tests. */
    static double responseDb (const ChannelSettings& s, double sampleRate, const DesignTable& designTable, double freq)
    {
        EqChannel ch;
        ch.fs = sampleRate;
        ch.table = &designTable;
        ch.settings = s;
        ch.lsDb = s.lsGainDb;
        ch.hsDb = s.hsGainDb;
        ch.designDetents();
        ch.designShelves();
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (ch.response (freq))));
    }

    std::complex<double> response (double freq) const
    {
        const double om = 2.0 * design::kPi * freq / fs;
        auto h = ls.c.response (om) * hs.c.response (om);
        if (hpOn) h *= hp.c.response (om);
        if (lpOn) h *= lp.c.response (om) * lpCorr.c.response (om);
        return h;
    }

private:
    void designDetents()
    {
        hpOn = settings.hp > 0;
        if (hpOn) hp.c = design::highPass (kHpFreqs[settings.hp], kFilterQ, fs);
        lpOn = settings.lp > 0;
        if (lpOn)
        {
            lp.c = table->lp[settings.lp].main;
            lpCorr.c = table->lp[settings.lp].corr;
        }
    }

    void designShelves() noexcept
    {
        ls.c = design::shelf (false, DesignTable::lsCentre (settings.lsFreq), lsDb, fs, table->lsMatch[settings.lsFreq]);
        hs.c = design::shelf (true,  DesignTable::hsCentre (settings.hsFreq), hsDb, fs, table->hsMatch[settings.hsFreq]);
    }

    double fs = 48000.0;
    ChannelSettings settings;
    const DesignTable* table = nullptr;
    BiquadState hp, lp, lpCorr;
    BiquadDF1 ls, hs;
    bool hpOn = false, lpOn = false;
    double lsDb = 0.0, hsDb = 0.0, lsTarget = 0.0, hsTarget = 0.0, lsStep = 0.0, hsStep = 0.0;
    int glideLeft = 0;
};

} // namespace bax
