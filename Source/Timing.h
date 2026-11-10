#pragma once

// Timing rules, shared state between the audio thread and the editor, statistics.
// Rules and numbers follow the Rehearsal tab of Insinistra (REAPER: Scripts\rehearsal\config.lua,
// tools\rehearsal.lua) so that both tools agree. Offsets in ms: + = behind (late), - = ahead.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ubv
{
constexpr float kMaxOffMs = 60.0f;   // a hit counts if within this of the nearest grid point...
constexpr float kMaxFrac  = 0.4f;    // ...and within this share of a grid step
constexpr float kBinMs    = 0.5f;    // histogram resolution of the running total
constexpr int   kBins     = (int) (2.0f * kMaxOffMs / kBinMs) + 1;
constexpr int   kRing     = 4096;    // recent hits (for the last bars)
constexpr int   kMinNotes = 8;       // fewer hits: no number
constexpr int   kShortBars = 8;      // "last bars" window

enum Role { kit = 0, kick, overheads, bass, guitar };

// grid step in quarter notes (ppq): 1/16, 1/16T, 1/8T, 1/32
inline double gridStep (int grid)
{
    switch (grid)
    {
        case 1:  return 1.0 / 6.0;
        case 2:  return 1.0 / 3.0;
        case 3:  return 0.125;
        default: return 0.25;
    }
}

// The pocket: no penalty inside. Drums sit a touch ahead of the click (so bass and guitar right
// on the click are not ahead of the drums), bass and guitar a touch behind. The wrong side costs
// double: drums behind, bass/guitar ahead.
struct Pocket { float lo, hi; bool worseBehind; };

inline bool isDrums (int role) { return role == kit || role == kick || role == overheads; }

inline Pocket defaultPocket (int role)
{
    return isDrums (role) ? Pocket { -10.0f, 0.0f, true } : Pocket { 0.0f, 15.0f, false };
}

struct Judgement
{
    bool valid = false;
    int n = 0;
    float med = 0, sig = 0, score = 0;
};

inline Judgement judge (float med, float sig, int n, Pocket p)
{
    Judgement j;
    j.n = n;
    if (n < kMinNotes) return j;
    j.valid = true;
    j.med = med;
    j.sig = sig;
    const float ahead  = std::max (0.0f, p.lo - med);
    const float behind = std::max (0.0f, med - p.hi);
    const float wa = p.worseBehind ? 1.0f : 2.0f;
    const float wb = p.worseBehind ? 2.0f : 1.0f;
    j.score = std::clamp (100.0f - 2.0f * (wa * ahead + wb * behind + 0.5f * sig), 0.0f, 100.0f);
    return j;
}

// median and sigma of the 10%-trimmed values (as median_sigma in rehearsal.lua)
inline void medianSigma (std::vector<float> v, float& med, float& sig)
{
    const auto n = v.size();
    med = sig = 0;
    if (n == 0) return;
    std::sort (v.begin(), v.end());
    med = n % 2 == 1 ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
    const size_t cut = n / 10;
    double sum = 0;
    size_t cnt = 0;
    for (size_t i = cut; i < n - cut; ++i) { sum += v[i]; ++cnt; }
    const double mean = sum / (double) cnt;
    double var = 0;
    for (size_t i = cut; i < n - cut; ++i) var += (v[i] - mean) * (v[i] - mean);
    sig = (float) std::sqrt (var / (double) std::max<size_t> (1, cnt - 1));
}

inline float binValue (int b) { return -kMaxOffMs + (float) b * kBinMs; }
inline int valueBin (float ms) { return std::clamp ((int) std::lround ((ms + kMaxOffMs) / kBinMs), 0, kBins - 1); }

// the same from histogram counts (bin centres)
inline void medianSigmaHist (const std::array<uint32_t, kBins>& h, int64_t n, float& med, float& sig)
{
    med = sig = 0;
    if (n <= 0) return;

    auto valueAt = [&] (int64_t k)
    {
        int64_t pos = 0;
        for (int b = 0; b < kBins; ++b)
        {
            pos += h[(size_t) b];
            if (pos > k) return binValue (b);
        }
        return binValue (kBins - 1);
    };
    med = n % 2 == 1 ? valueAt (n / 2) : 0.5f * (valueAt (n / 2 - 1) + valueAt (n / 2));

    const int64_t cut = n / 10, hiEnd = n - cut;
    double sum = 0, sumSq = 0;
    int64_t cnt = 0, pos = 0;
    for (int b = 0; b < kBins; ++b)
    {
        const int64_t c = h[(size_t) b];
        const int64_t kept = std::max<int64_t> (0, std::min (pos + c, hiEnd) - std::max (pos, cut));
        pos += c;
        if (kept == 0) continue;
        const double v = binValue (b);
        sum += v * (double) kept;
        sumSq += v * v * (double) kept;
        cnt += kept;
    }
    if (cnt == 0) return;
    const double mean = sum / (double) cnt;
    const double var = std::max (0.0, sumSq - (double) cnt * mean * mean);
    sig = (float) std::sqrt (var / (double) std::max<int64_t> (1, cnt - 1));
}

// Written by the audio thread, read by the editor. Reads may see a hit half-written during a
// reset: harmless for a display refreshed many times a second.
struct Shared
{
    std::array<std::atomic<uint32_t>, kBins> hist {};
    std::atomic<uint32_t> outside { 0 };     // hits too far from the grid
    std::array<std::atomic<float>, kRing> ringMs {};
    std::array<std::atomic<int>, kRing> ringBar {};
    std::atomic<uint32_t> ringCount { 0 };
    std::atomic<int> bar { 0 };              // bar counter, advances while playing
    std::atomic<bool> resetRequest { false };

    void clear() noexcept
    {
        for (auto& b : hist) b.store (0, std::memory_order_relaxed);
        outside.store (0, std::memory_order_relaxed);
        ringCount.store (0, std::memory_order_release);
    }

    void addHit (float ms, int barIndex) noexcept
    {
        hist[(size_t) valueBin (ms)].fetch_add (1, std::memory_order_relaxed);
        const auto i = ringCount.load (std::memory_order_relaxed);
        ringMs[i % kRing].store (ms, std::memory_order_relaxed);
        ringBar[i % kRing].store (barIndex, std::memory_order_relaxed);
        ringCount.store (i + 1, std::memory_order_release);
    }
};

// Onset detector, a port of Scripts\rehearsal\insinistra_onset.jsfx: one-pole filters per role,
// fast/slow envelopes, a hit when the fast one jumps over the slow one and a threshold.
class Detector
{
public:
    void setup (int role, float sensDb, double sampleRate)
    {
        double lpf = 0, hpf = 0, thrDb = -45, refr = 0.05;
        switch (role)
        {
            case kick:      lpf = 3000; ratio = 2.0; thrDb = -50; refr = 0.050; break;
            case overheads: hpf = 200;  ratio = 1.8; thrDb = -45; refr = 0.040; break;
            case bass:      hpf = 30;   ratio = 1.6; thrDb = -45; refr = 0.050; break;
            case guitar:    hpf = 80;   ratio = 1.6; thrDb = -45; refr = 0.050; break;
            default:        /* whole kit, new: no filter */ ratio = 1.8; thrDb = -50; refr = 0.035; break;
        }
        const double twoPi = 6.283185307179586;
        alp = lpf > 0 ? std::exp (-twoPi * lpf / sampleRate) : 0;
        ahp = hpf > 0 ? std::exp (-twoPi * hpf / sampleRate) : 0;
        thr = std::pow (10.0, (thrDb - sensDb) / 20.0);
        att = std::exp (-1.0 / (0.0003 * sampleRate));
        rel = std::exp (-1.0 / (0.015 * sampleRate));
        sl  = std::exp (-1.0 / (0.050 * sampleRate));
        refrSamples = (int64_t) (refr * sampleRate);
    }

    // true on the sample of an onset
    bool process (float in) noexcept
    {
        double y = in;
        if (alp > 0) { lp1 = y + alp * (lp1 - y); y = lp1; }
        if (ahp > 0) { lp2 = y + ahp * (lp2 - y); y = y - lp2; }
        const double r = std::abs (y);
        fast = r > fast ? r + att * (fast - r) : r + rel * (fast - r);
        slow = fast + sl * (slow - fast);
        ++since;
        if (! armed && fast < 1.1 * slow) armed = true;
        if (armed && fast > thr && fast > ratio * slow && since >= refrSamples)
        {
            armed = false;
            since = 0;
            return true;
        }
        return false;
    }

    void reset() noexcept { lp1 = lp2 = fast = slow = 0; armed = true; since = 1 << 30; }

private:
    double alp = 0, ahp = 0, ratio = 2, thr = 0, att = 0, rel = 0, sl = 0;
    double lp1 = 0, lp2 = 0, fast = 0, slow = 0;
    bool armed = true;
    int64_t since = 1 << 30, refrSamples = 0;
};
} // namespace ubv
