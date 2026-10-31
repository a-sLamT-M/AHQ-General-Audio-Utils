#include "ahq/audio/volumeEnvelope.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
using namespace ahq::audio;
int main() {
    bool passed = true;
    auto check = [&](bool ok, const char *name) {
        if (!ok)
            std::cerr << "FAILED: " << name << '\n';
        passed &= ok;
    };
    VolumeEnvelope whole, chunks;
    check(whole.prepare(48000) && chunks.prepare(48000), "prepare");
    std::vector<float> a(48000, 1), b(a), r(a);
    check(whole.process(a, r, {}, {}, 120), "BPM envelope");
    for (size_t offset = 0; offset < b.size();) {
        auto n = std::min(size_t(137), b.size() - offset);
        check(chunks.process(std::span(b).subspan(offset, n), {}, {}, {}, 120), "chunks");
        offset += n;
    }
    check(a == b && a == r, "block invariant and stereo linked");
    check(a[2000] < .01 && a[23500] > .99 && a[26000] < .01, "one beat at 120 BPM");
    VolumeEnvelopeParameters p;
    p.audioTrigger = true;
    check(whole.setParameters(p) && chunks.setParameters(p), "audio trigger");
    whole.reset();
    chunks.reset();
    a.assign(48000, 1);
    b = a;
    std::vector<float> soft(48000, 0), loud(48000, 0);
    soft[10] = .1F;
    loud[10] = 1.F;
    check(whole.process(a, {}, soft, {}, 120) && chunks.process(b, {}, loud, {}, 120), "trigger");
    check(a == b, "trigger amplitude does not determine attenuation");
    check(a[0] == 1 && a[2000] < .01 && a[30000] > .999, "one-shot returns to unity");
    whole.reset();
    a.assign(48000, 1);
    soft.assign(48000, .01F);
    check(whole.process(a, {}, soft, {}, 120) && a[10000] == 1, "below threshold stays at unity");
    p.audioTrigger = false;
    check(whole.setParameters(p), "sync mode");
    whole.reset();
    a.assign(100, 1);
    check(whole.process(a, {}, {}, {}, 120, 7, true, false) && a[99] == 1,
          "stopped host does not duck");
    p.times[3] = p.times[2];
    check(!whole.setParameters(p), "reject unordered nodes");
    p = {};
    p.levels[4] = std::numeric_limits<double>::quiet_NaN();
    check(!whole.setParameters(p), "reject nonfinite values");
    check(!whole.process(a, std::span(a).first(1), {}, {}), "reject mismatched stereo");
    return passed ? 0 : 1;
}
