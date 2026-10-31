#include "ahq/audio/parametricEq.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
namespace ahq::audio {
bool EqBandParameters::isValid() const noexcept {
    return int(shape) >= 0 && int(shape) <= 6 && int(channels) >= 0 && int(channels) <= 4 &&
           std::isfinite(frequency) && frequency >= 10 && frequency <= 30000 &&
           std::isfinite(gainDb) && std::abs(gainDb) <= 30 && std::isfinite(q) && q >= .1 &&
           q <= 30 && stages >= 1 && stages <= 4 && std::isfinite(dynamicRangeDb) &&
           std::abs(dynamicRangeDb) <= 24 && std::isfinite(thresholdDb) && thresholdDb >= -80 &&
           thresholdDb <= 0 && std::isfinite(attackMs) && attackMs >= .1 && attackMs <= 200 &&
           std::isfinite(releaseMs) && releaseMs >= 10 && releaseMs <= 2000;
}
// Normalized RBJ biquads: https://www.w3.org/TR/audio-eq-cookbook/
ParametricEq::Coefficients ParametricEq::coefficients(const EqBandParameters &p, double rate,
                                                      double gainDb) noexcept {
    const double w = 2 * std::numbers::pi * std::min(p.frequency, rate * .45) / rate;
    const double c = std::cos(w), s = std::sin(w), alpha = s / (2 * p.q);
    const double a = std::pow(10.0, gainDb / 40), root = 2 * std::sqrt(a) * alpha;
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (p.shape) {
    case EqShape::bell:
        b0 = 1 + alpha * a;
        b1 = -2 * c;
        b2 = 1 - alpha * a;
        a0 = 1 + alpha / a;
        a1 = -2 * c;
        a2 = 1 - alpha / a;
        break;
    case EqShape::lowShelf:
        b0 = a * ((a + 1) - (a - 1) * c + root);
        b1 = 2 * a * ((a - 1) - (a + 1) * c);
        b2 = a * ((a + 1) - (a - 1) * c - root);
        a0 = (a + 1) + (a - 1) * c + root;
        a1 = -2 * ((a - 1) + (a + 1) * c);
        a2 = (a + 1) + (a - 1) * c - root;
        break;
    case EqShape::highShelf:
        b0 = a * ((a + 1) + (a - 1) * c + root);
        b1 = -2 * a * ((a - 1) + (a + 1) * c);
        b2 = a * ((a + 1) + (a - 1) * c - root);
        a0 = (a + 1) - (a - 1) * c + root;
        a1 = 2 * ((a - 1) - (a + 1) * c);
        a2 = (a + 1) - (a - 1) * c - root;
        break;
    case EqShape::lowCut:
        b0 = (1 + c) * .5;
        b1 = -(1 + c);
        b2 = b0;
        a0 = 1 + alpha;
        a1 = -2 * c;
        a2 = 1 - alpha;
        break;
    case EqShape::highCut:
        b0 = (1 - c) * .5;
        b1 = 1 - c;
        b2 = b0;
        a0 = 1 + alpha;
        a1 = -2 * c;
        a2 = 1 - alpha;
        break;
    case EqShape::notch:
        b0 = 1;
        b1 = -2 * c;
        b2 = 1;
        a0 = 1 + alpha;
        a1 = -2 * c;
        a2 = 1 - alpha;
        break;
    case EqShape::bandPass:
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
        a0 = 1 + alpha;
        a1 = -2 * c;
        a2 = 1 - alpha;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}
double ParametricEq::tick(double x, State &s, const Coefficients &c) noexcept {
    const double y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}
void ParametricEq::target(Band &b, double db, bool immediate) noexcept {
    b.target = coefficients(b.p, rate_, db);
    if (immediate) {
        b.current = b.target;
        b.coefficientFrames = 0;
        return;
    }
    b.coefficientFrames = rampFrames_;
    const double frames = double(rampFrames_);
    b.delta = {(b.target.b0 - b.current.b0) / frames, (b.target.b1 - b.current.b1) / frames,
               (b.target.b2 - b.current.b2) / frames, (b.target.a1 - b.current.a1) / frames,
               (b.target.a2 - b.current.a2) / frames};
}
bool ParametricEq::prepare(double rate) noexcept {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000)
        return false;
    rate_ = rate;
    rampFrames_ = unsigned(std::ceil(rate * .005));
    for (auto &b : bands_) {
        b.attack = std::exp(-1 / (b.p.attackMs * .001 * rate));
        b.release = std::exp(-1 / (b.p.releaseMs * .001 * rate));
        auto detector = b.p;
        detector.shape = EqShape::bandPass;
        b.detector = coefficients(detector, rate, 0);
    }
    reset();
    return true;
}
bool ParametricEq::setBand(size_t index, const EqBandParameters &p) noexcept {
    if (rate_ == 0 || index >= bandCount || !p.isValid())
        return false;
    auto &b = bands_[index];
    if (b.p == p)
        return true;
    const bool topology =
        b.p.shape != p.shape || b.p.stages != p.stages || b.p.channels != p.channels;
    if (topology && b.wetValue > 0) {
        b.fadingParameters = b.p;
        b.fadingCoefficients = b.current;
        b.fadingStates = b.states;
        b.fadingWet = b.wetValue;
        b.fadingFrames = rampFrames_;
    }
    b.p = p;
    b.attack = std::exp(-1 / (p.attackMs * .001 * rate_));
    b.release = std::exp(-1 / (p.releaseMs * .001 * rate_));
    auto detector = p;
    detector.shape = EqShape::bandPass;
    b.detector = coefficients(detector, rate_, 0);
    if (topology) {
        b.states = {};
        b.detectorStates = {};
        b.envelope = 0;
        b.dynamicDb = 0;
        b.wet.reset(0);
        b.wetValue = 0;
    }
    b.wet.setTarget(p.enabled ? 1 : 0, rampFrames_);
    target(b, p.gainDb + b.dynamicDb, topology || b.wetValue == 0);
    return true;
}
void ParametricEq::reset() noexcept {
    for (auto &b : bands_) {
        b.states = {};
        b.detectorStates = {};
        b.envelope = b.dynamicDb = 0;
        b.controlClock = 0;
        b.wetValue = b.p.enabled ? 1 : 0;
        b.wet.reset(b.wetValue);
        b.fadingFrames = 0;
        if (rate_ != 0)
            target(b, b.p.gainDb, true);
    }
}
bool ParametricEq::process(std::span<float> left, std::span<float> right) noexcept {
    if (!right.empty() && left.size() != right.size())
        return false;
    if (left.empty())
        return true;
    if (rate_ == 0)
        return false;
    for (auto &b : bands_) {
        if (!b.p.enabled && b.wetValue == 0 && b.fadingFrames == 0)
            continue;
        const bool stereo = !right.empty(),
                   ms = b.p.channels == EqChannels::mid || b.p.channels == EqChannels::side;
        const bool dynamic = b.p.dynamicRangeDb != 0 && int(b.p.shape) <= int(EqShape::highShelf);
        const int stages =
            b.p.shape == EqShape::lowCut || b.p.shape == EqShape::highCut ? b.p.stages : 1;
        for (size_t i = 0; i < left.size(); ++i) {
            const double dryL = left[i], dryR = stereo ? right[i] : dryL;
            double l = ms ? (dryL + dryR) * .5 : dryL;
            double r = ms ? (dryL - dryR) * .5 : dryR;
            if (dynamic) {
                const double dl = std::abs(tick(l, b.detectorStates[0], b.detector));
                const double dr = std::abs(tick(r, b.detectorStates[1], b.detector));
                const double level =
                    b.p.channels == EqChannels::right || b.p.channels == EqChannels::side ? dr
                    : b.p.channels == EqChannels::left || b.p.channels == EqChannels::mid
                        ? dl
                        : std::max(dl, dr);
                const double coefficient = level > b.envelope ? b.attack : b.release;
                b.envelope = level + coefficient * (b.envelope - level);
                if (++b.controlClock >= 32) {
                    b.controlClock = 0;
                    const double amount = std::clamp(
                        (20 * std::log10(std::max(b.envelope, 1e-12)) - b.p.thresholdDb) / 12, 0.0,
                        1.0);
                    const double db = b.p.dynamicRangeDb * amount;
                    if (std::abs(db - b.dynamicDb) > .01) {
                        b.dynamicDb = db;
                        target(b, b.p.gainDb + db, false);
                    }
                }
            } else if (b.dynamicDb != 0) {
                b.dynamicDb = 0;
                target(b, b.p.gainDb, false);
            }
            if (b.coefficientFrames) {
                b.current.b0 += b.delta.b0;
                b.current.b1 += b.delta.b1;
                b.current.b2 += b.delta.b2;
                b.current.a1 += b.delta.a1;
                b.current.a2 += b.delta.a2;
                if (--b.coefficientFrames == 0)
                    b.current = b.target;
            }
            for (int section = 0; section < stages; ++section) {
                if (b.p.channels != EqChannels::right && b.p.channels != EqChannels::side)
                    l = tick(l, b.states[0][size_t(section)], b.current);
                if (stereo && b.p.channels != EqChannels::left && b.p.channels != EqChannels::mid)
                    r = tick(r, b.states[1][size_t(section)], b.current);
            }
            const double wetL = ms ? l + r : l, wetR = ms ? l - r : r;
            b.wetValue = b.wet.next();
            double outputL = dryL + b.wetValue * (wetL - dryL);
            double outputR = dryR + b.wetValue * (wetR - dryR);
            if (b.fadingFrames) {
                const auto &old = b.fadingParameters;
                const bool oldMs =
                    old.channels == EqChannels::mid || old.channels == EqChannels::side;
                double oldL = oldMs ? (dryL + dryR) * .5 : dryL,
                       oldR = oldMs ? (dryL - dryR) * .5 : dryR;
                const int oldStages =
                    old.shape == EqShape::lowCut || old.shape == EqShape::highCut ? old.stages : 1;
                for (int section = 0; section < oldStages; ++section) {
                    if (old.channels != EqChannels::right && old.channels != EqChannels::side)
                        oldL = tick(oldL, b.fadingStates[0][size_t(section)], b.fadingCoefficients);
                    if (stereo && old.channels != EqChannels::left &&
                        old.channels != EqChannels::mid)
                        oldR = tick(oldR, b.fadingStates[1][size_t(section)], b.fadingCoefficients);
                }
                const double amount = b.fadingWet * double(--b.fadingFrames) / rampFrames_;
                outputL += amount * ((oldMs ? oldL + oldR : oldL) - dryL);
                outputR += amount * ((oldMs ? oldL - oldR : oldR) - dryR);
            }
            left[i] = float(outputL);
            if (stereo)
                right[i] = float(outputR);
        }
    }
    return true;
}
double ParametricEq::magnitude(size_t index, double hz) const noexcept {
    if (index >= bandCount || rate_ == 0 || !std::isfinite(hz) || hz < 0)
        return 1;
    const auto &b = bands_[index];
    if (!b.p.enabled)
        return 1;
    const auto c = b.target;
    const auto z = std::polar(1.0, -2 * std::numbers::pi * std::min(hz, rate_ * .5) / rate_);
    const double magnitude =
        std::abs((c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z));
    return std::pow(magnitude,
                    b.p.shape == EqShape::lowCut || b.p.shape == EqShape::highCut ? b.p.stages : 1);
}
double ParametricEq::dynamicGain(size_t index) const noexcept {
    return index < bandCount ? bands_[index].dynamicDb : 0;
}
std::array<double, 2> ParametricEq::responseMagnitudes(double hz, double mix) const noexcept {
    if (rate_ == 0 || !std::isfinite(hz) || hz < 0)
        return {1, 1};
    const auto z = std::polar(1.0, -2 * std::numbers::pi * std::min(hz, rate_ * .5) / rate_);
    std::array<std::complex<double>, 4> paths{1., 1., 1., -1.};
    for (const auto &b : bands_) {
        if (!b.p.enabled)
            continue;
        const auto &c = b.target;
        auto h = (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z);
        if (b.p.shape == EqShape::lowCut || b.p.shape == EqShape::highCut) {
            const auto section = h;
            for (int i = 1; i < b.p.stages; ++i)
                h *= section;
        }
        for (size_t input = 0; input < 2; ++input) {
            auto &l = paths[input * 2], &r = paths[input * 2 + 1];
            if (b.p.channels == EqChannels::mid || b.p.channels == EqChannels::side) {
                auto m = (l + r) * .5, s = (l - r) * .5;
                if (b.p.channels == EqChannels::mid)
                    m *= h;
                else
                    s *= h;
                l = m + s;
                r = m - s;
            } else {
                if (b.p.channels != EqChannels::right)
                    l *= h;
                if (b.p.channels != EqChannels::left)
                    r *= h;
            }
        }
    }
    paths[0] = 1. + mix * (paths[0] - 1.);
    paths[1] = 1. + mix * (paths[1] - 1.);
    paths[2] = 1. + mix * (paths[2] - 1.);
    paths[3] = -1. + mix * (paths[3] + 1.);
    return {std::sqrt((std::norm(paths[0]) + std::norm(paths[1])) * .5),
            std::sqrt((std::norm(paths[2]) + std::norm(paths[3])) * .5)};
}
} // namespace ahq::audio
