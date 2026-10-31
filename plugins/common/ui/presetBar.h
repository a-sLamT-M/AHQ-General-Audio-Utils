#pragma once
#include "pluginTheme.h"
#include "presetLibrary.h"

namespace ahq::ui {
/// One shared preset browser for all effects and instruments; no polling or audio-thread work.
class PresetBar final : public juce::Component {
  public:
    static constexpr int height = 66;
    template <class Processor>
    explicit PresetBar(Processor &processor, PluginTheme &style,
                       juce::File presetRoot = PresetLibrary::root())
        : library(processor.getName(), std::move(presetRoot)), theme(style),
          capture([&processor] { return processor.createPresetXml(); }),
          restore([&processor](const juce::String &xml) { return processor.loadPresetXml(xml); }) {
        setName("User preset library");
        for (auto *c :
             std::array<juce::Component *, 6>{&browser, &save, &load, &folder, &name, &status})
            addAndMakeVisible(c);
        browser.setTextWhenNothingSelected("User presets");
        browser.setTooltip("Presets for " + processor.getName() + ". Reopen to refresh.");
        browser.beforePopup = [this] { refresh(); };
        browser.onChange = [this] {
            const int index = browser.getSelectedId() - 1;
            if (juce::isPositiveAndBelow(index, entries.size()))
                loadFile(entries[index], false);
        };
        name.setTextToShowWhenEmpty("New preset name", juce::Colour(0xffbac2ca));
        name.setTooltip("Name for a new saved preset. Existing files are kept.");
        name.setSelectAllWhenFocused(true);
        name.onReturnKey = [this] { saveNamed(); };
        save.onClick = [this] { saveNamed(); };
        load.onClick = [this] { chooseImport(); };
        folder.onClick = [this] {
            if (library.directory.createDirectory().wasOk())
                library.directory.startAsProcess();
            else
                report("Cannot create the preset folder.");
        };
        folder.setTooltip(library.directory.getFullPathName());
        load.setTooltip("Import a preset file into this plugin's central library.");
        status.setColour(juce::Label::textColourId, juce::Colour(0xffbac2ca));
        refresh();
        report("User library: " + library.directory.getFullPathName());
    }
    void refresh() {
        const auto selected = browser.getText();
        entries = library.files();
        browser.clear(juce::dontSendNotification);
        for (int i = 0; i < entries.size(); ++i) {
            const auto title = entries[i].getFileNameWithoutExtension();
            browser.addItem(title, i + 1);
            if (title == selected)
                browser.setSelectedId(i + 1, juce::dontSendNotification);
        }
    }
    bool loadFile(const juce::File &file, bool import) {
        const auto xml = PresetLibrary::read(file);
        if (xml.isEmpty() || !restore(xml)) {
            report("Invalid preset or wrong plugin; current settings kept.");
            return false;
        }
        auto saved = file;
        if (import &&
            (file.getParentDirectory() != library.directory || !file.hasFileExtension("ahqpreset")))
            saved = library.save(file.getFileNameWithoutExtension(), capture());
        if (saved == juce::File{}) {
            report("Loaded, but could not copy into the user library.");
            return true;
        }
        refresh();
        const auto title = saved.getFileNameWithoutExtension();
        name.setText(title, false);
        for (int i = 0; i < entries.size(); ++i)
            if (entries[i] == saved)
                browser.setSelectedId(i + 1, juce::dontSendNotification);
        report("Loaded: " + title);
        return true;
    }
    void paint(juce::Graphics &g) override {
        g.fillAll(theme.background.darker(.15F));
        g.setColour(theme.accent);
        g.fillRect(0, 0, getWidth(), 1);
        g.setFont(juce::FontOptions(12).withStyle("Bold"));
        g.drawText(theme.category, 20, 42, 160, 20, juce::Justification::centredLeft);
    }
    void resized() override {
        auto row = getLocalBounds().reduced(20, 0).withY(8).withHeight(28);
        const int available = row.getWidth();
        browser.setBounds(row.removeFromLeft(int(available * .30F)).withTrimmedRight(8));
        name.setBounds(
            row.removeFromLeft(available - int(available * .30F) - 262).withTrimmedRight(8));
        save.setBounds(row.removeFromLeft(100).withTrimmedRight(6));
        load.setBounds(row.removeFromLeft(100).withTrimmedRight(6));
        folder.setBounds(row);
        status.setBounds(184, 42, getWidth() - 204, 20);
    }
    const PresetLibrary library;

  private:
    class Browser final : public juce::ComboBox {
      public:
        std::function<void()> beforePopup;
        void showPopup() override {
            if (beforePopup)
                beforePopup();
            juce::ComboBox::showPopup();
        }
    };
    void report(const juce::String &text) {
        status.setText(text, juce::dontSendNotification);
        status.setTooltip(text);
    }
    void saveNamed() {
        if (name.getText().trim().isEmpty()) {
            report("Enter a preset name before saving.");
            name.grabKeyboardFocus();
            return;
        }
        const auto file = library.save(name.getText(), capture());
        if (file == juce::File{}) {
            report("Could not save. Check the name and folder permissions.");
            return;
        }
        refresh();
        name.setText(file.getFileNameWithoutExtension(), false);
        for (int i = 0; i < entries.size(); ++i)
            if (entries[i] == file)
                browser.setSelectedId(i + 1, juce::dontSendNotification);
        report("Saved: " + file.getFileNameWithoutExtension());
    }
    void chooseImport() {
        if (chooser)
            return;
        chooser =
            std::make_unique<juce::FileChooser>("Import preset", library.directory, "*.ahq*;*.xml");
        const juce::Component::SafePointer<PresetBar> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [safe](const juce::FileChooser &choice) {
                                 if (!safe)
                                     return;
                                 if (choice.getResult() != juce::File{})
                                     safe->loadFile(choice.getResult(), true);
                                 juce::MessageManager::callAsync([safe] {
                                     if (safe)
                                         safe->chooser.reset();
                                 });
                             });
    }
    PluginTheme &theme;
    std::function<juce::String()> capture;
    std::function<bool(const juce::String &)> restore;
    Browser browser;
    juce::TextEditor name;
    juce::TextButton save{"Save preset"}, load{"Load preset"}, folder{"Folder"};
    juce::Label status;
    juce::Array<juce::File> entries;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace ahq::ui
