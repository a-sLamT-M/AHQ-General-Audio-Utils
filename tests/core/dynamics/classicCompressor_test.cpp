#include <ahq/audio/classicCompressor.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>
int main() {
    using namespace ahq::audio;
    bool ok = true;
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    for (auto model : {ClassicCompressor::Model::Opto, ClassicCompressor::Model::Fet}) {
        ClassicCompressor a(model), b(model);
        check(a.prepare(48000) && b.prepare(48000), "prepare");
        ClassicCompressorParameters p;
        p.peakReduction = 50;
        p.inputDb = 6;
        check(a.setParameters(p) && b.setParameters(p), "parameters");
        std::vector<float> l(48000, .5F), r(48000, -.25F), chunks(48000, .5F);
        check(a.process(l, r), "process");
        for (size_t n = 0; n < chunks.size(); n += 31)
            check(b.process(
                      std::span<float>(chunks).subspan(n, std::min(size_t(31), chunks.size() - n))),
                  "chunks");
        check(l == chunks && r.back() == -.5F * l.back(), "block independence and stereo link");
        check(a.reductionDb() > 8 && l.back() < .3F, "steady compression");
        std::fill(l.begin(), l.end(), 0);
        check(a.process(l), "release");
        check(a.reductionDb() < 25 && a.reductionDb() >= 0, "release bounded");
        p.mix = 0;
        check(a.setParameters(p), "mix");
        std::fill(l.begin(), l.end(), .25F);
        check(a.process(l) && l.back() == .25F, "dry mix");
        p.inputDb = NAN;
        check(!a.setParameters(p), "invalid parameter");
    }
    return ok ? 0 : 1;
}
