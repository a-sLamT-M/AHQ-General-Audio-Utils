#include "pluginState.h"
#include <algorithm>
#include <charconv>
PluginState::PluginState(juce::AudioProcessorValueTreeState &parameters,
                         std::span<const char *const> parameterIds, const char *tag,
                         std::atomic<int> &program)
    : ids(parameterIds.begin(), parameterIds.end()), xmlTag(tag), parameterRanges(ids.size()),
      currentProgram(program), rawParameters(ids.size()), rangedParameters(ids.size()) {
    for (size_t index = 0; index < ids.size(); ++index) {
        rawParameters[index] = parameters.getRawParameterValue(ids[index]);
        rangedParameters[index] = parameters.getParameter(ids[index]);
        const auto &range = rangedParameters[index]->getNormalisableRange();
        parameterRanges[index] = {range.start, range.end, range.interval};
    }
}
bool PluginState::validateValues(std::span<const float> values) const noexcept {
    if (values.size() != parameterRanges.size())
        return false;
    for (size_t index = 0; index < values.size(); ++index)
        if (!parameterRanges[index].contains(values[index]))
            return false;
    return true;
}

class PluginState::ParameterEdit final : public juce::UndoableAction {
  public:
    ParameterEdit(PluginState &owner, Values previous, Values next, bool alreadyApplied = false)
        : processor(owner), before(previous), after(next), skipFirst(alreadyApplied) {}
    bool perform() override {
        if (skipFirst) {
            skipFirst = false;
            return true;
        }
        apply(after);
        return true;
    }
    bool undo() override {
        apply(before);
        return true;
    }

  private:
    void apply(const Values &target) {
        for (size_t index = 0; index < target.size(); ++index)
            if (before[index] != after[index])
                processor.writeParameter(index, target[index], true);
    }
    PluginState &processor;
    Values before, after;
    bool skipFirst;
};

void PluginState::fillValues(std::span<float> values) const noexcept {
    jassert(values.size() == rawParameters.size());
    if (values.size() != rawParameters.size())
        return;
    for (size_t index = 0; index < values.size(); ++index)
        values[index] = rawParameters[index]->load(std::memory_order_relaxed);
}
PluginState::Values PluginState::readValues() const {
    Values values(ids.size());
    fillValues(values);
    return values;
}

juce::String PluginState::createPresetXml() const {
    juce::XmlElement xml(xmlTag);
    xml.setAttribute("version", 1);
    const auto values = readValues();
    for (size_t index = 0; index < values.size(); ++index)
        xml.setAttribute(ids[index], static_cast<double>(values[index]));
    return xml.toString();
}

bool PluginState::decodeXml(const juce::XmlElement &xml, Values &values) const {
    if (!xml.hasTagName(xmlTag) || xml.getStringAttribute("version") != "1" ||
        xml.getNumAttributes() != int(ids.size() + 1) || xml.getFirstChildElement() != nullptr)
        return false;
    for (size_t index = 0; index < values.size(); ++index) {
        const auto text = xml.getStringAttribute(ids[index]).toStdString();
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), values[index]);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            return false;
    }
    return validateValues(values);
}

bool PluginState::decodePresetXml(const juce::String &text, Values &values) const {
    if (text.getNumBytesAsUTF8() > 65536)
        return false;
    const auto xml = juce::parseXML(text);
    values.resize(ids.size());
    return xml && decodeXml(*xml, values);
}

bool PluginState::applyUserPreset(juce::String before, juce::String after,
                                  std::function<bool(const juce::String &)> restore) {
    class SnapshotEdit final : public juce::UndoableAction {
      public:
        SnapshotEdit(juce::String from, juce::String to,
                     std::function<bool(const juce::String &)> callback)
            : previous(std::move(from)), next(std::move(to)), apply(std::move(callback)) {}
        bool perform() override { return apply(next); }
        bool undo() override { return apply(previous); }
        int getSizeInUnits() override {
            return int(std::min<size_t>(size_t(std::numeric_limits<int>::max()),
                                        previous.getNumBytesAsUTF8() + next.getNumBytesAsUTF8()));
        }

      private:
        juce::String previous, next;
        std::function<bool(const juce::String &)> apply;
    };
    return performUserAction(
        new SnapshotEdit(std::move(before), std::move(after), std::move(restore)), "Load preset");
}

void PluginState::writeValues(std::span<const float> values, bool notifyHost) {
    jassert(values.size() == ids.size());
    if (values.size() != ids.size())
        return;
    for (size_t index = 0; index < values.size(); ++index)
        writeParameter(index, values[index], notifyHost);
}

void PluginState::writeParameter(size_t index, float value, bool notifyHost) {
    auto &parameter = *rangedParameters[index];
    const float normalized = parameter.convertTo0to1(value);
    if (notifyHost) {
        parameter.beginChangeGesture();
        parameter.setValueNotifyingHost(normalized);
        parameter.endChangeGesture();
    } else {
        parameter.setValueNotifyingHost(normalized);
    }
    currentProgram.store(-1);
}

bool PluginState::applyUserValues(std::span<const float> values, const juce::String &name) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (values.size() != ids.size() || !validateValues(values))
        return false;
    if (historyRevision != stateRevision.load()) {
        undoManager.clearUndoHistory();
        historyRevision = stateRevision.load();
    }
    const auto before = readValues();
    if (std::equal(before.begin(), before.end(), values.begin()))
        return true;
    undoManager.beginNewTransaction(name);
    return undoManager.perform(
        new ParameterEdit(*this, before, Values(values.begin(), values.end())));
}

bool PluginState::performUserAction(juce::UndoableAction *action, const juce::String &name) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (historyRevision != stateRevision.load()) {
        undoManager.clearUndoHistory();
        historyRevision = stateRevision.load();
    }
    // IR preset actions report their byte size; keep one complete snapshot even if large,
    // rather than inheriting UndoManager's minimum of thirty retained transactions.
    undoManager.setMaxNumberOfStoredUnits(32 * 1024 * 1024, 1);
    undoManager.beginNewTransaction(name);
    return undoManager.perform(action);
}

void PluginState::recordUserEdit(std::span<const float> previous, int parameterIndex,
                                 const juce::String &name) {
    recordUserEdit(previous, std::span<const int>(&parameterIndex, 1), name);
}
void PluginState::recordUserEdit(std::span<const float> previous, std::span<const int> indices,
                                 const juce::String &name) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (historyRevision != stateRevision.load()) {
        undoManager.clearUndoHistory();
        historyRevision = stateRevision.load();
        return;
    }
    if (previous.size() != ids.size())
        return;
    const auto after = readValues();
    auto before = after;
    for (int index : indices) {
        if (!juce::isPositiveAndBelow(index, int(after.size())))
            return;
        before[size_t(index)] = previous[size_t(index)];
    }
    if (before != after && validateValues(before)) {
        undoManager.beginNewTransaction(name);
        undoManager.perform(new ParameterEdit(*this, before, after, true));
    }
}

bool PluginState::loadPresetXml(const juce::String &text, bool undoable) {
    if (text.getNumBytesAsUTF8() > 65536)
        return false;
    const auto xml = juce::parseXML(text);
    Values values(ids.size());
    if (xml == nullptr || !decodeXml(*xml, values))
        return false;
    if (undoable)
        return applyUserValues(values, "Load preset");
    writeValues(values, false);
    stateRevision.fetch_add(1);
    return true;
}

void PluginState::getStateInformation(juce::MemoryBlock &block) {
    const auto xml = juce::parseXML(createPresetXml());
    juce::AudioProcessor::copyXmlToBinary(*xml, block);
}

void PluginState::setStateInformation(const void *data, int size) {
    if (data == nullptr || size <= 0 || size > 65536)
        return;
    const auto xml = juce::AudioProcessor::getXmlFromBinary(data, size);
    Values values(ids.size());
    if (xml != nullptr && decodeXml(*xml, values)) {
        writeValues(values, false);
        stateRevision.fetch_add(1);
    }
}

bool PluginState::undo() {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    return canUndo() && undoManager.undo();
}

bool PluginState::redo() {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    return canRedo() && undoManager.redo();
}
