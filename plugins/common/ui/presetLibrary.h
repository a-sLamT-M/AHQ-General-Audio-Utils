#pragma once
#include <juce_core/juce_core.h>

namespace ahq::ui {
/// File operations belong to the message thread. XML tags are checked by the processor.
class PresetLibrary {
  public:
    static constexpr juce::int64 maximumBytes = 128 * 1024 * 1024;
    static juce::File root() {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("AHQ/Presets");
    }
    explicit PresetLibrary(const juce::String &product, juce::File location = root())
        : directory(location.getChildFile(juce::File::createLegalFileName(product))) {}
    juce::Array<juce::File> files() const {
        auto result = directory.findChildFiles(juce::File::findFiles, false, "*.ahqpreset");
        result.sort();
        return result;
    }
    static juce::String read(const juce::File &file) {
        const auto size = file.getSize();
        return size > 0 && size <= maximumBytes ? file.loadFileAsString() : juce::String{};
    }
    juce::File save(const juce::String &name, const juce::String &xml) const {
        auto legalName = juce::File::createLegalFileName(name.trim()).substring(0, 100);
        if (legalName.isEmpty() || legalName == "." || legalName == ".." || xml.isEmpty() ||
            xml.getNumBytesAsUTF8() > size_t(maximumBytes) || directory.createDirectory().failed())
            return {};
        // Save as a new revision: never silently overwrite an existing user preset.
        const auto target = directory.getNonexistentChildFile(legalName, ".ahqpreset");
        juce::TemporaryFile temporary(target);
        if (temporary.getFile().replaceWithText(xml) &&
            temporary.overwriteTargetFileWithTemporary())
            return target;
        return {};
    }
    const juce::File directory;
};
} // namespace ahq::ui
