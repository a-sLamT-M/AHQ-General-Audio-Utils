#include <ahq/audio/retroTools.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
int main() {
    using namespace ahq::audio;
    bool ok = true;
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    RetroTools a, b;
    check(a.prepare(48000) && b.prepare(48000), "prepare");
    RetroToolsParameters p;
    p.wow = .5;
    p.flutter = .5;
    p.bits = 12;
    p.drive = .4;
    check(a.setParameters(p) && b.setParameters(p), "parameters");
    a.reset();
    b.reset();
    std::vector<float> l(48000), r(48000), x(48000), y(48000);
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = x[i] = y[i] = float(.4 * std::sin(double(i) * .09));
    check(a.process(l, r), "process");
    for (size_t i = 0; i < x.size(); i += 31) {
        const auto n = std::min(size_t(31), x.size() - i);
        check(b.process(std::span<float>(x).subspan(i, n), std::span<float>(y).subspan(i, n)),
              "chunks");
    }
    check(l == x && r == y, "block independent");
    double side = 0;
    for (size_t i = 0; i < l.size(); ++i) {
        check(std::isfinite(l[i]) && std::abs(l[i]) < 2, "bounded output");
        side += std::abs(l[i] - r[i]);
    }
    check(side > 1, "stereo ensemble");
    p.mix = 0;
    check(a.setParameters(p), "dry");
    a.reset();
    std::fill(l.begin(), l.end(), 0);
    l[0] = 1;
    check(a.process(l) && l[a.latencySamples()] == 1 && l[0] == 0, "latency aligned dry");
    for (double invalid : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()}) {
        p.fault = invalid;
        check(!a.setParameters(p), "reject invalid fault");
    }
    p = {};
    p.ensemble = 0;
    p.mix = 1;
    p.fault = .8;
    check(a.setParameters(p) && b.setParameters(p), "fault parameters");
    a.reset();
    b.reset();
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = x[i] = y[i] = float(.4 * std::sin(double(i) * .09));
    check(a.process(l, r), "fault stereo");
    for (size_t i = 0; i < x.size();) {
        const auto n = std::min(size_t(1 + i % 257), x.size() - i);
        check(b.setParameters(p), "repeated fault parameters");
        check(b.process(std::span<float>(x).subspan(i, n), std::span<float>(y).subspan(i, n)),
              "fault chunks");
        i += n;
    }
    check(l == x && r == y, "fault independent of block size and repeated targets");
    check(l == r, "fault stereo linked");
    a.reset();
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = float(.4 * std::sin(double(i) * .09));
    check(a.process(x) && x == l, "fault reset repeats sequence and mono matches stereo");

    for (double rate : {8000., 44100., 48000., 96000., 384000.}) {
        for (double amplitude : {.02, .2, .9}) {
            RetroTools clean, broken;
            check(clean.prepare(rate) && broken.prepare(rate), "fault sample rates");
            p.fault = 0;
            check(clean.setParameters(p), "clean comparison");
            p.fault = 1;
            check(broken.setParameters(p), "maximum fault");
            clean.reset();
            broken.reset();
            std::vector<float> original(size_t(rate), 0.F), damaged(size_t(rate), 0.F);
            for (size_t i = 0; i < original.size(); ++i)
                original[i] = damaged[i] =
                    float(amplitude * std::sin(2 * 3.141592653589793 * 220 * i / rate));
            check(clean.process(original) && broken.process(damaged), "fault tone render");
            double cleanEnergy = 0, brokenEnergy = 0, difference = 0;
            bool bounded = true;
            for (size_t i = 0; i < damaged.size(); ++i) {
                bounded &= std::isfinite(damaged[i]) && std::abs(damaged[i]) <= amplitude + 1e-6;
                cleanEnergy += double(original[i]) * original[i];
                brokenEnergy += double(damaged[i]) * damaged[i];
                difference += std::abs(damaged[i] - original[i]);
            }
            check(bounded && brokenEnergy <= cleanEnergy, "fault does not boost peak or RMS");
            check(difference > .01 * amplitude * rate, "fault audibly sized signal change");
            broken.reset();
            std::fill(damaged.begin(), damaged.end(), 0);
            check(broken.process(damaged) &&
                      std::all_of(damaged.begin(), damaged.end(), [](float v) { return v == 0; }),
                  "fault does not generate sound from silence");
            p.mix = 0;
            check(broken.setParameters(p), "fault dry setting");
            broken.reset();
            damaged[0] = .5F;
            check(broken.process(damaged) && damaged[broken.latencySamples()] == .5F,
                  "fault preserves aligned dry path");
            p.mix = 1;
        }
    }
    // A constant source isolates random amplitude collapse from pitch modulation.
    check(a.prepare(48000), "fault DC prepare");
    p.fault = 1;
    check(a.setParameters(p), "fault DC parameters");
    a.reset();
    l.assign(96000, .3F);
    check(a.process(l), "fault DC render");
    double minimum = 1, maximum = 0, repeatDifference = 0;
    for (size_t i = 24000; i < l.size() - 4800; ++i) {
        minimum = std::min(minimum, double(l[i]));
        maximum = std::max(maximum, double(l[i]));
        repeatDifference += std::abs(l[i] - l[i + 4800]);
    }
    check(maximum - minimum > .04 && repeatDifference > 10,
          "fault has irregular amplitude collapse on a steady source");
    const double previous = l.back();
    p.fault = 0;
    check(a.setParameters(p), "disable fault smoothly");
    l.assign(4800, .3F);
    check(a.process(l), "fault automation render");
    double step = std::abs(l[0] - previous);
    for (size_t i = 1; i < l.size(); ++i)
        step = std::max(step, double(std::abs(l[i] - l[i - 1])));
    check(step < .01 && std::abs(l.back() - .3F) < 1e-6,
          "fault automation is smoothed and off restores clean signal");
    return ok ? 0 : 1;
}
