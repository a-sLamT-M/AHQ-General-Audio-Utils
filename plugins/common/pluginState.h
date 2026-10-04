#pragma once
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <span>
#include <vector>
/// Shared parameter state for the effects; pointer storage is allocated only at construction. Undo
/// APIs belong to the message thread.
class PluginState {
  public:
    using Values = std::vector<float>;
    using Validator = bool (*)(std::span<const float>) noexcept;
    PluginState(juce::AudioProcessorValueTreeState &, std::span<const char *const>,
                const char *xmlTag, Validator, std::atomic<int> &program);
    void fillValues(std::span<float>) const noexcept;
    juce::String createPresetXml() const;
    bool loadPresetXml(const juce::String &, bool undoable);
    bool applyUserValues(std::span<const float>, const juce::String &);
    void recordUserEdit(std::span<const float>, int, const juce::String &);
    void getStateInformation(juce::MemoryBlock &);
    void setStateInformation(const void *, int);
    void writeValues(std::span<const float>, bool notifyHost);
    void invalidateHistory() noexcept { stateRevision.fetch_add(1); }
    bool undo();
    bool redo();
    bool canUndo() const {
        return historyRevision == stateRevision.load() && undoManager.canUndo();
    }
    bool canRedo() const {
        return historyRevision == stateRevision.load() && undoManager.canRedo();
    }

  private:
    class ParameterEdit;
    Values readValues() const;
    bool decodeXml(const juce::XmlElement &, Values &);
    void writeParameter(size_t index, float value, bool notifyHost);
    std::vector<const char *> ids;
    juce::String xmlTag;
    Validator validator;
    std::atomic<int> &currentProgram;
    std::vector<std::atomic<float> *> rawParameters;
    std::vector<juce::RangedAudioParameter *> rangedParameters;
    juce::UndoManager undoManager;
    std::atomic<unsigned> stateRevision{0};
    unsigned historyRevision = 0;
};
