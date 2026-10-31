#include <ahq/audio/elevation.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
int main() {
    using namespace ahq::audio;
    bool ok = true;
    auto check = [&](bool b, const char *name) {
        if (!b)
            std::cerr << name << '\n';
        ok &= b;
    };
    auto a = std::make_unique<Elevation>(), b = std::make_unique<Elevation>();
    check(a->prepare(48000) && b->prepare(48000), "prepare");
    ElevationParameters p;
    p.elevationDegrees = 90;
    check(a->setParameters(p) && b->setParameters(p), "parameters");
    a->reset();
    b->reset();
    std::vector<float> l(4096), r(4096), x(4096), y(4096);
    l[0] = r[0] = x[0] = y[0] = 1;
    check(a->process(l, r), "process");
    for (size_t i = 0; i < x.size(); i += 17) {
        const auto n = std::min(size_t(17), x.size() - i);
        check(b->process(std::span<float>(x).subspan(i, n), std::span<float>(y).subspan(i, n)),
              "chunks");
    }
    check(l == x && r == y, "block independence");
    p.elevationDegrees = -90;
    check(b->setParameters(p), "below");
    b->reset();
    std::fill(x.begin(), x.end(), 0);
    std::fill(y.begin(), y.end(), 0);
    x[0] = y[0] = 1;
    check(b->process(x, y) && l != x, "above and below differ");
    p.mix = 0;
    check(a->setParameters(p), "dry");
    a->reset();
    std::fill(l.begin(), l.end(), 0);
    std::fill(r.begin(), r.end(), 0);
    l[0] = 1;
    r[0] = -.5;
    check(a->process(l, r) && l[a->latencySamples()] == 1 && r[a->latencySamples()] == -.5,
          "dry aligned");
    for (double rate : {8000., 44100., 96000., 384000.}) {
        check(a->prepare(rate), "sample rate");
        p.mix = 1;
        check(a->setParameters(p), "wet");
        a->reset();
        std::fill(l.begin(), l.end(), .25F);
        std::fill(r.begin(), r.end(), -.25F);
        check(a->process(l, r), "opposite stereo");
        for (size_t i = 0; i < l.size(); ++i)
            check(l[i] == 0 && r[i] == 0, "wet is one mid source");
    }
    check(!a->prepare(NAN), "invalid rate");
    return ok ? 0 : 1;
}
