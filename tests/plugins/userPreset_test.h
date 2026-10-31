#pragma once
#include "../../plugins/common/ui/presetBar.h"
#include "../../plugins/common/ui/spectrumControls.h"
#include <iostream>

template <class Processor> bool testUserPresetLibrary() {
    bool passed = true;
    auto check = [&](bool ok, const char *message) {
        if (!ok)
            std::cerr << "USER PRESETS: " << message << '\n';
        passed &= ok;
    };
    auto processor = std::make_unique<Processor>();
    const auto initial = processor->createPresetXml();
    auto &parameters = static_cast<juce::AudioProcessor &>(*processor).getParameters();
    check(!parameters.isEmpty(), "processor has parameters");
    if (parameters.isEmpty())
        return false;
    auto *parameter = parameters.getLast();
    parameter->setValueNotifyingHost(parameter->getValue() < .5F ? 1.F : 0.F);
    const auto changed = processor->createPresetXml();
    check(initial != changed, "fixture changes a registered parameter");
    juce::TemporaryFile temporary(".preset-tests");
    const auto testRoot = temporary.getFile();
    {
        const ahq::ui::PresetLibrary library(processor->getName(), testRoot);
        const auto unicodeName =
            juce::String::fromUTF8("\xe6\x88\x91\xe7\x9a\x84\xe9\x9f\xb3\xe8\x89\xb2");
        const auto file = library.save(unicodeName, changed);
        check(file.existsAsFile() && file.getParentDirectory() == library.directory &&
                  file.getFileNameWithoutExtension() == unicodeName,
              "Unicode preset saved in its plugin directory");
        const auto revision = library.save(unicodeName, initial);
        check(revision != file && ahq::ui::PresetLibrary::read(file) == changed,
              "same-name save preserves the previous preset");
        const ahq::ui::PresetLibrary secondInstance(processor->getName(), testRoot);
        check(secondInstance.files().size() == 2, "another instance sees the central files");
        const ahq::ui::PresetLibrary otherPlugin("AHQ Other", testRoot);
        check(otherPlugin.files().isEmpty(), "plugin directories are isolated");
        const auto safePath = library.save("../escaped", changed);
        check(safePath != juce::File{} && safePath.getParentDirectory() == library.directory,
              "preset names cannot escape the plugin folder");
        check(processor->loadPresetXml(initial), "restore initial preset");
        check(processor->loadPresetXml(ahq::ui::PresetLibrary::read(file)), "load a saved preset");
        check(processor->createPresetXml() == changed, "complete preset round trip");
        check(processor->undo() && processor->createPresetXml() == initial,
              "preset load is undoable");
        check(processor->redo() && processor->createPresetXml() == changed,
              "preset load can be redone");
        auto wrong = juce::parseXML(changed);
        wrong->setTagName("AHQWrongPluginState");
        check(!processor->loadPresetXml(wrong->toString()) &&
                  processor->createPresetXml() == changed,
              "wrong-plugin preset rejected without changing state");
        check(!processor->loadPresetXml("<broken") && processor->createPresetXml() == changed,
              "corrupt preset rejected without changing state");
        {
            juce::Component owner;
            ahq::ui::PluginTheme theme(owner, processor->getName());
            ahq::ui::PresetBar bar(*processor, theme, testRoot);
            const auto legacy = library.directory.getChildFile("Legacy.ahqlegacy");
            check(legacy.replaceWithText(changed) && bar.loadFile(legacy, true),
                  "import through the shared UI");
            check(library.files().size() == 4 &&
                      ahq::ui::PresetLibrary::read(
                          library.directory.getChildFile("Legacy.ahqpreset")) == changed,
                  "legacy file already in the library is converted and becomes discoverable");
            juce::TextButton *save = nullptr;
            for (auto *child : bar.getChildren()) {
                if (auto *input = dynamic_cast<juce::TextEditor *>(child))
                    input->setText("Saved from UI");
                if (auto *button = dynamic_cast<juce::TextButton *>(child);
                    button && button->getButtonText() == "Save preset")
                    save = button;
            }
            check(save != nullptr, "shared save button exists");
            if (save)
                save->onClick();
            check(ahq::ui::PresetLibrary::read(
                      library.directory.getChildFile("Saved from UI.ahqpreset")) == changed,
                  "shared save button writes a readable preset into the central folder");
        }
    }
    check(testRoot.deleteRecursively(), "remove only this test's temporary library");
    check(processor->loadPresetXml(initial), "restore initial settings before opening editor");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
        ahq::ui::PresetBar *bar = nullptr;
        for (auto *child : editor->getChildren())
            if (auto *candidate = dynamic_cast<ahq::ui::PresetBar *>(child))
                bar = candidate;
        check(bar && bar->isVisible() && editor->getLocalBounds().contains(bar->getBounds()),
              "shared preset bar is visible and fits");
        if (bar) {
            check(bar->library.directory ==
                      ahq::ui::PresetLibrary::root().getChildFile(processor->getName()),
                  "editor uses the product's central folder");
            for (auto *child : bar->getChildren())
                check(!child->getBounds().isEmpty() &&
                          bar->getLocalBounds().contains(child->getBounds()),
                      "preset controls fit at the default editor size");
        }
        const auto snapshotDirectory =
            juce::SystemStats::getEnvironmentVariable("AHQ_UI_SNAPSHOT_DIR", {});
        if (snapshotDirectory.isNotEmpty()) {
            const auto file =
                juce::File(snapshotDirectory).getChildFile(processor->getName() + ".png");
            auto out = file.createOutputStream();
            if (out) {
                out->setPosition(0);
                out->truncate();
                juce::PNGImageFormat png;
                check(png.writeImageToStream(
                          editor->createComponentSnapshot(editor->getLocalBounds()), *out),
                      "write editor snapshot");
            } else
                check(false, "open snapshot destination");
        }
        const auto defaultSize = editor->getBounds();
        const auto audioValues = processor->readValues();
        auto checkBounds = [&] {
            for (auto *child : editor->getChildren()) {
                if (!child->isVisible() || child->getBounds().isEmpty())
                    continue;
                if (!editor->getLocalBounds().contains(child->getBounds())) {
                    std::cerr << processor->getName() << ": outside editor: " << child->getName()
                              << " " << child->getBounds().toString() << '\n';
                    check(false, "visible controls stay inside the editor");
                }
            }
        };
        auto snapshot = [&](const juce::String &suffix) {
            if (snapshotDirectory.isEmpty())
                return;
            const auto file =
                juce::File(snapshotDirectory).getChildFile(processor->getName() + suffix + ".png");
            auto stream = file.createOutputStream();
            if (stream) {
                stream->setPosition(0);
                stream->truncate();
                juce::PNGImageFormat png;
                check(png.writeImageToStream(
                          editor->createComponentSnapshot(editor->getLocalBounds()), *stream),
                      "write layout snapshot");
            }
        };
        checkBounds();
        if (auto *constrainer = editor->getConstrainer()) {
            editor->setSize(constrainer->getMinimumWidth(), constrainer->getMinimumHeight());
            checkBounds();
            snapshot("-minimum");
        }
        for (auto *child : editor->getChildren()) {
            if (auto *pages = dynamic_cast<juce::ComboBox *>(child);
                pages && pages->getNumItems() >= 4 && pages->getItemText(0) == "Generators") {
                const int previous = pages->getSelectedId();
                for (int page = 1; page <= pages->getNumItems(); ++page) {
                    pages->setSelectedId(page, juce::sendNotificationSync);
                    checkBounds();
                    snapshot("-page-" + juce::String(page));
                }
                pages->setSelectedId(previous, juce::sendNotificationSync);
                break; // Page changes rebuild the child list; do not advance its old iterator.
            }
        }
        for (auto *child : editor->getChildren()) {
            if (auto *toolbar = dynamic_cast<ahq::ui::SpectrumControls *>(child)) {
                for (auto *widget : toolbar->getChildren()) {
                    if (auto *slider = dynamic_cast<juce::Slider *>(widget);
                        slider && slider->getName() == "Spectrum area") {
                        const auto previous = slider->getValue();
                        for (double value : {50.0, 75.0}) {
                            slider->setValue(value, juce::sendNotificationSync);
                            checkBounds();
                            snapshot("-area-" + juce::String(value, 0));
                        }
                        slider->setValue(previous, juce::sendNotificationSync);
                    }
                    check(toolbar->getLocalBounds().contains(widget->getBounds()),
                          "spectrum controls fit the toolbar");
                }
            }
        }
        check(processor->readValues() == audioValues,
              "display layout changes leave audio parameters unchanged");
        editor->setSize(defaultSize.getWidth(), defaultSize.getHeight());
        processor->editorBeingDeleted(editor.get());
    }
    return passed;
}
