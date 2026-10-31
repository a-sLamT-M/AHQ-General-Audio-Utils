#include <ahq/audio/tremolo.h>
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
    Tremolo a, b;
    check(a.prepare(48000) && b.prepare(48000), "prepare");
    TremoloParameters p;
    p.beats = 1;
    check(a.setParameters(p) && b.setParameters(p), "parameters");
    std::vector<float> l(48000, 1), r(48000, -.5F), chunks(48000, 1);
    check(a.process(l, r, 120), "process");
    for (size_t n = 0; n < chunks.size(); n += 17)
        check(
            b.process(std::span<float>(chunks).subspan(n, std::min(size_t(17), chunks.size() - n))),
            "chunks");
    check(l == chunks, "block independence");
    check(l[12000] < .001F && l[24000] > .999F && r[12000] == -.5F * l[12000],
          "BPM stereo amplitude");
    std::fill(l.begin(), l.end(), 1);
    check(a.process(l, {}, 120, 0, true, false) && l.back() > .999F, "stopped");
    p.depth = 0;
    check(a.setParameters(p), "zero depth");
    std::fill(l.begin(), l.end(), 1);
    check(a.process(l) && l.back() == 1, "unity at zero depth");
    for (int shape = 0; shape < 4; ++shape) {
        p.depth = 1;
        p.shape = shape;
        check(a.setParameters(p), "shape");
        std::fill(l.begin(), l.end(), 1);
        check(a.process(l), "shape process");
        for (float v : l)
            check(std::isfinite(v) && v >= 0 && v <= 1, "bounded gain");
    }
    return ok ? 0 : 1;
}
