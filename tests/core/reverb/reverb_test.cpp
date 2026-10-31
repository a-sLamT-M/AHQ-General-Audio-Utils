#include <ahq/audio/reverb.h>
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
    Reverb a, b;
    check(a.prepare(48000) && b.prepare(48000), "prepare");
    ReverbParameters p;
    p.mix = 1;
    p.decaySeconds = .5;
    p.preDelayMs = 0;
    check(a.setParameters(p) && b.setParameters(p), "parameters");
    a.reset();
    b.reset();
    std::vector<float> l(48000 * 3), r(l.size()), chunks(l.size()), chunksR(l.size());
    l[0] = r[0] = chunks[0] = chunksR[0] = 1;
    check(a.process(l, r), "process");
    for (size_t i = 0; i < chunks.size(); i += 17) {
        const size_t n = std::min(size_t(17), chunks.size() - i);
        check(b.process(std::span<float>(chunks).subspan(i, n),
                        std::span<float>(chunksR).subspan(i, n)),
              "chunk process");
    }
    check(l == chunks && r == chunksR, "block independent");
    double first = 0, last = 0, side = 0;
    for (size_t i = 0; i < l.size(); ++i) {
        check(std::isfinite(l[i]) && std::isfinite(r[i]), "finite tail");
        if (i < 24000) {
            first += l[i] * l[i];
            side += std::abs(l[i] - r[i]);
        }
        if (i > 96000)
            last += l[i] * l[i];
    }
    check(first > .001 && last < first * 1e-8 && side > .1, "audible stereo tail decays");
    p.mix = 0;
    check(a.setParameters(p), "dry parameters");
    a.reset();
    std::fill(l.begin(), l.end(), .25F);
    check(a.process(l) && l.front() == .25F && l.back() == .25F, "exact dry");
    p.decaySeconds = NAN;
    check(!a.setParameters(p), "reject invalid parameter");
    return ok ? 0 : 1;
}
