#include <ahq/audio/synthesizer.h>
#include <algorithm>
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
    auto synth = std::make_unique<Synthesizer>();
    check(synth->prepare(48000), "prepare");
    SynthEffects fx;
    fx.reverb = 0;
    fx.chorus = 0;
    fx.outputDb = -12;
    check(synth->setEffects(fx), "effects");
    SynthPart part;
    part.enabled = true;
    std::vector<float> l(4096), r(4096);
    for (size_t bank = 0; bank < 2; ++bank)
        for (size_t preset = 0; preset < (bank ? 32U : 64U); ++preset) {
            const auto p =
                bank ? Synthesizer::modularProgram(preset) : Synthesizer::pcmProgram(preset);
            check(p.isValid() && synth->setPart(0, part, p), "every factory program valid");
            synth->reset();
            synth->noteOn(1, 60, .8F);
            double energy = 0;
            for (int block = 0; block < 4; ++block) {
                check(synth->process(l, r), "factory render");
                for (size_t i = 0; i < l.size(); ++i) {
                    check(std::isfinite(l[i]) && std::isfinite(r[i]), "finite factory sound");
                    energy += l[i] * l[i] + r[i] * r[i];
                }
            }
            if (energy <= .0001) {
                std::cerr << "Silent factory preset " << bank << ":" << preset << '\n';
                ok = false;
            }
        }
    auto a = std::make_unique<Synthesizer>(), b = std::make_unique<Synthesizer>();
    check(a->prepare(48000) && b->prepare(48000), "block test prepare");
    auto p = Synthesizer::modularProgram(9);
    p.envelopes[0].releaseMs = 50;
    check(a->setPart(0, part, p) && b->setPart(0, part, p) && a->setEffects(fx) &&
              b->setEffects(fx),
          "block setup");
    a->reset();
    b->reset();
    a->noteOn(1, 64, .7F);
    b->noteOn(1, 64, .7F);
    std::vector<float> x(l.size()), y(l.size());
    check(a->process(l, r), "whole");
    for (size_t i = 0; i < x.size(); i += 17) {
        const auto n = std::min(size_t(17), x.size() - i);
        check(b->process(std::span<float>(x).subspan(i, n), std::span<float>(y).subspan(i, n)),
              "chunks");
    }
    check(l == x && r == y, "sample exact block independence");
    a->controller(1, 64, 1);
    a->noteOff(1, 64);
    check(a->process(l, r) && a->activeVoices() == 1, "sustain pedal holds released key");
    a->controller(1, 64, 0);
    check(a->process(l, r) && a->activeVoices() == 0, "pedal release completes envelope");
    fx.polyphony = 8;
    check(a->setEffects(fx), "voice limit");
    for (int n = 0; n < 40; ++n)
        a->noteOn(1, 40 + n, .5F);
    check(a->activeVoices() == 8 && a->process(l, r), "bounded voice stealing");
    a->allNotesOff(true);
    check(a->activeVoices() == 0, "panic");
    part.lowKey = 60;
    part.highKey = 72;
    part.channel = 2;
    check(a->setPart(0, part, p), "split");
    a->noteOn(1, 64, 1);
    a->noteOn(2, 50, 1);
    check(a->activeVoices() == 0, "key and channel split");
    a->noteOn(2, 64, 1);
    check(a->activeVoices() == 1, "matching split");
    for (size_t c = 0; c < Synthesizer::combiCount; ++c)
        for (size_t i = 0; i < 8; ++i)
            check(Synthesizer::combiPart(c, i).isValid(), "combi part validity");
    return ok ? 0 : 1;
}
