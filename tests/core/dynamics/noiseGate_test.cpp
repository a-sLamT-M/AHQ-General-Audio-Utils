#include <ahq/audio/noiseGate.h>
#include <array>
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
    NoiseGate gate;
    check(gate.prepare(48000), "prepare");
    NoiseGateParameters p;
    p.thresholdDb = -20;
    p.attackMs = .5;
    p.holdMs = 50;
    p.releaseMs = 50;
    check(gate.setParameters(p), "parameters");
    std::vector<float> l(4800, .01F), r(4800, -.005F);
    check(gate.process(l, r) && l.back() == 0, "closed below threshold");
    std::fill(l.begin(), l.end(), .5F);
    std::fill(r.begin(), r.end(), -.25F);
    check(gate.process(l, r) && l.back() > .499F && r.back() == -.5F * l.back(),
          "open stereo linked");
    std::array<float, 1000> quiet{};
    quiet.fill(.001F);
    check(gate.process(quiet) && quiet.back() > .00099F, "hold prevents chatter");
    std::vector<float> decay(48000, .001F);
    check(gate.process(decay) && decay.back() < 1e-9F, "release closes");
    p.mix = 0;
    check(gate.setParameters(p), "dry parameter");
    quiet.fill(.01F);
    check(gate.process(quiet) && quiet.front() == .01F, "dry bypass");
    p.thresholdDb = NAN;
    check(!gate.setParameters(p), "reject NaN");
    check(!gate.process(quiet, std::span<float>(quiet).first(10)), "reject layout");
    return ok ? 0 : 1;
}
