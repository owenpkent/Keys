#include "KeysHostEditor.h"
#include <algorithm>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #pragma comment(lib, "version.lib")
#endif

namespace keys
{
namespace
{
    constexpr int barHeight = 44;
    constexpr int keysHeight = 640;      // the embedded editor's comfortable height (gen-panel floor)
    constexpr int minKeysHeight = 520;

    juce::File defaultVst3Folder()
    {
       #if JUCE_WINDOWS
        return juce::File("C:\\Program Files\\Common Files\\VST3");
       #elif JUCE_MAC
        return juce::File("/Library/Audio/Plug-Ins/VST3");
       #else
        return {};
       #endif
    }

    // The folders the DAW itself scans, so the picker lists what Live's browser lists.
    juce::Array<juce::File> vst3Folders()
    {
        juce::Array<juce::File> dirs { defaultVst3Folder() };
       #if JUCE_WINDOWS
        dirs.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                     .getChildFile("Ableton").getChildFile("vst3"));
       #elif JUCE_MAC
        dirs.add(juce::File("~/Library/Audio/Plug-Ins/VST3"));
       #endif
        return dirs;
    }

    // A .vst3 is either a single file or a bundle directory; either way it is one
    // entry and we do not descend into it (bundles contain an inner .vst3 module
    // that would double-list).
    void collectVst3s(const juce::File& dir, std::vector<juce::File>& out)
    {
        for (const auto& entry : juce::RangedDirectoryIterator(dir, false, "*",
                                                               juce::File::findFilesAndDirectories))
        {
            const auto f = entry.getFile();
            if (f.getFileName().endsWithIgnoreCase(".vst3"))
                out.push_back(f);
            else if (f.isDirectory())
                collectVst3s(f, out);
        }
    }

   #if JUCE_WINDOWS
    // CompanyName from a DLL's version resource — metadata read, nothing is loaded.
    juce::String fileCompanyName(const juce::File& f)
    {
        const auto path = f.getFullPathName();
        DWORD handle = 0;
        const auto size = GetFileVersionInfoSizeW(path.toWideCharPointer(), &handle);
        if (size == 0)
            return {};
        juce::HeapBlock<char> data((size_t) size);
        if (! GetFileVersionInfoW(path.toWideCharPointer(), 0, size, data.getData()))
            return {};
        struct LangCp { WORD lang, cp; };
        LangCp* lc = nullptr;
        UINT lcSize = 0;
        if (! VerQueryValueW(data.getData(), L"\\VarFileInfo\\Translation", (void**) &lc, &lcSize)
            || lc == nullptr || lcSize < sizeof(LangCp))
            return {};
        wchar_t query[64];
        swprintf(query, 64, L"\\StringFileInfo\\%04x%04x\\CompanyName", lc[0].lang, lc[0].cp);
        wchar_t* company = nullptr;
        UINT companySize = 0;
        if (VerQueryValueW(data.getData(), query, (void**) &company, &companySize)
            && company != nullptr && companySize > 1)
            return juce::String(company).trim();
        return {};
    }
   #endif

    juce::String publisherFor(const juce::File& f)
    {
        if (f.isDirectory())
        {
            // A VST3 bundle's moduleinfo.json names the vendor.
            const auto mi = f.getChildFile("Contents").getChildFile("Resources").getChildFile("moduleinfo.json");
            if (mi.existsAsFile())
            {
                const auto parsed = juce::JSON::parse(mi.loadFileAsString());
                const auto vendor = parsed.getProperty("Factory Info", juce::var())
                                          .getProperty("Vendor", juce::var()).toString().trim();
                if (vendor.isNotEmpty())
                    return vendor;
            }
           #if JUCE_WINDOWS
            const auto inner = f.getChildFile("Contents").getChildFile("x86_64-win").getChildFile(f.getFileName());
            if (inner.existsAsFile())
                if (auto name = fileCompanyName(inner); name.isNotEmpty())
                    return name;
           #endif
        }
        else
        {
           #if JUCE_WINDOWS
            if (auto name = fileCompanyName(f); name.isNotEmpty())
                return name;
           #endif
        }
        // Vendor subfolder under the VST3 root (e.g. VST3\FabFilter\...) as a hint.
        const auto parent = f.getParentDirectory();
        if (! vst3Folders().contains(parent))
            return parent.getFileName();
        return {};
    }

    struct PickerEntry
    {
        juce::String publisher; // empty = unknown, grouped last as "Other"
        juce::File file;
    };

    std::vector<PickerEntry> findInstalledVst3s()
    {
        std::vector<juce::File> found;
        for (const auto& dir : vst3Folders())
            collectVst3s(dir, found);

        std::sort(found.begin(), found.end(), [](const juce::File& a, const juce::File& b)
                  { return a.getFileName().compareIgnoreCase(b.getFileName()) < 0; });
        found.erase(std::unique(found.begin(), found.end(), [](const juce::File& a, const juce::File& b)
                                { return a.getFileName().equalsIgnoreCase(b.getFileName()); }),
                    found.end());
        // Hosting yourself is a hall of mirrors, not an instrument.
        found.erase(std::remove_if(found.begin(), found.end(), [](const juce::File& f)
                                   { return f.getFileNameWithoutExtension().equalsIgnoreCase("Keys Host"); }),
                    found.end());

        std::vector<PickerEntry> entries;
        entries.reserve(found.size());
        for (const auto& f : found)
            entries.push_back({ publisherFor(f), f });

        std::stable_sort(entries.begin(), entries.end(), [](const PickerEntry& a, const PickerEntry& b)
        {
            if (a.publisher.isEmpty() != b.publisher.isEmpty())
                return b.publisher.isEmpty(); // unknowns sort last
            const int byPublisher = a.publisher.compareIgnoreCase(b.publisher);
            if (byPublisher != 0)
                return byPublisher < 0;
            return a.file.getFileName().compareIgnoreCase(b.file.getFileName()) < 0;
        });
        return entries;
    }
} // namespace

InstrumentPicker::InstrumentPicker()
{
    title.setText("Load instrument", juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title);

    rescanButton.onClick = [this] { refresh(); };
    browseButton.onClick = [this] { if (onBrowse) onBrowse(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(rescanButton);
    addAndMakeVisible(browseButton);
    addAndMakeVisible(closeButton);

    viewport.setViewedComponent(&rowHolder, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    refresh();
}

void InstrumentPicker::refresh()
{
    items.clear();
    juce::String currentPublisher("\x01"); // never matches a real (or empty) publisher
    for (const auto& entry : findInstalledVst3s())
    {
        if (entry.publisher != currentPublisher)
        {
            currentPublisher = entry.publisher;
            auto* header = new juce::Label({}, currentPublisher.isEmpty() ? "Other" : currentPublisher);
            header->setJustificationType(juce::Justification::bottomLeft);
            header->setAlpha(0.6f);
            items.add(header);
            rowHolder.addAndMakeVisible(header);
        }
        auto* row = new juce::TextButton(entry.file.getFileNameWithoutExtension());
        const auto file = entry.file;
        row->onClick = [this, file] { if (onPick) onPick(file); };
        items.add(row);
        rowHolder.addAndMakeVisible(row);
    }
    if (items.isEmpty())
    {
        auto* row = new juce::TextButton("No VST3s found - use Browse files...");
        row->setEnabled(false);
        items.add(row);
        rowHolder.addAndMakeVisible(row);
    }
    resized();
}

juce::Rectangle<int> InstrumentPicker::panelBounds() const
{
    return getLocalBounds().withSizeKeepingCentre(juce::jmin(520, getWidth() - 40),
                                                  juce::jmin(560, getHeight() - 40));
}

void InstrumentPicker::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.55f)); // dim the editor behind the panel
    auto panel = panelBounds().toFloat();
    g.setColour(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawRoundedRectangle(panel, 8.0f, 1.0f);
}

void InstrumentPicker::resized()
{
    auto panel = panelBounds().reduced(12);

    auto top = panel.removeFromTop(38);
    closeButton.setBounds(top.removeFromRight(80));
    top.removeFromRight(8);
    title.setBounds(top);
    panel.removeFromTop(8);

    auto bottom = panel.removeFromBottom(38);
    rescanButton.setBounds(bottom.removeFromLeft(100));
    bottom.removeFromLeft(8);
    browseButton.setBounds(bottom.removeFromLeft(150));
    panel.removeFromBottom(8);

    viewport.setBounds(panel);
    const int rowH = 40, headerH = 30;
    const int width = panel.getWidth() - viewport.getScrollBarThickness();
    int y = 0;
    for (auto* item : items)
    {
        const bool isHeader = dynamic_cast<juce::Label*>(item) != nullptr;
        const int h = isHeader ? headerH : rowH;
        item->setBounds(0, y, width, h - 4);
        y += h;
    }
    rowHolder.setSize(width, juce::jmax(1, y));
}

void InstrumentPicker::mouseDown(const juce::MouseEvent& e)
{
    if (! panelBounds().contains(e.getPosition()) && onClose)
        onClose();
}

InstrumentWindow::InstrumentWindow(const juce::String& name, juce::Component& content,
                                   std::function<void()> onClose)
    : juce::DocumentWindow(name, juce::Colours::black, juce::DocumentWindow::closeButton),
      onCloseClick(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setResizable(false, false);
    // Not owned: the editor tears the content down itself, in the right order
    // relative to the plugin instance. `true` keeps the window sized to the GUI,
    // including when the plugin later resizes itself (skin/zoom changes).
    setContentNonOwned(&content, true);
}

void InstrumentWindow::closeButtonPressed()
{
    if (onCloseClick)
        onCloseClick(); // hide, never eject — closing a window must not lose state
}

KeysHostEditor::KeysHostEditor(KeysHostProcessor& p)
    : juce::AudioProcessorEditor(p), host(p), keysEditor(p)
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { openPicker(); };

    addAndMakeVisible(showHideButton);
    showHideButton.onClick = [this] { setInstrumentShown(! instShown); };

    addAndMakeVisible(ejectButton);
    ejectButton.onClick = [this] { host.ejectInstrument(); };

    instLabel.setJustificationType(juce::Justification::centredLeft);
    instLabel.setMinimumHorizontalScale(0.8f);
    addAndMakeVisible(instLabel);

    // The full Keys editor rides along as an ordinary child; the window, not the
    // child, owns resizing (setEmbedded stops it from ever calling setSize itself).
    addAndMakeVisible(keysEditor);
    keysEditor.setResizable(false, false);
    keysEditor.setEmbedded(true);
    startTimerHz(4);

    host.onInstrumentWillChange = [this] { closeInstrumentEditor(); };
    host.addChangeListener(this);

    setResizable(true, true);
    setResizeLimits(1010, barHeight + minKeysHeight, 2600, 1700);
    // Owen resizes to the minimum every time anyway — open there. (keysHeight is what
    // the chord-generator overlay grows the embedded editor to when opened.)
    setSize(1010, barHeight + minKeysHeight);
    juce::ignoreUnused(keysHeight);
    openInstrumentEditor(); // reflects "no instrument" too (label + bar state)
}

KeysHostEditor::~KeysHostEditor()
{
    host.onInstrumentWillChange = nullptr;
    host.removeChangeListener(this);
    closeInstrumentEditor();
}

void KeysHostEditor::openPicker()
{
    if (picker != nullptr)
        return;
    picker = std::make_unique<InstrumentPicker>();
    picker->onPick = [this](const juce::File& f)
    {
        closePicker();
        loadAndReport(f);
    };
    picker->onBrowse = [this]
    {
        closePicker();
        chooseInstrument();
    };
    picker->onClose = [this] { closePicker(); };
    addAndMakeVisible(*picker);
    picker->setBounds(getLocalBounds());
}

void KeysHostEditor::closePicker()
{
    // Deleting the overlay from inside its own button callback is unsafe; let the
    // message loop finish the click first.
    if (auto* p = picker.release())
        juce::MessageManager::callAsync([p] { delete p; });
}

void KeysHostEditor::loadAndReport(const juce::File& file)
{
    const auto error = host.loadInstrument(file);
    if (error.isNotEmpty())
        instLabel.setText(error, juce::dontSendNotification);
}

bool KeysHostEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".vst3"))
            return true;
    return false;
}

void KeysHostEditor::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".vst3"))
        {
            loadAndReport(juce::File(f));
            return;
        }
}

void KeysHostEditor::chooseInstrument()
{
    chooser = std::make_unique<juce::FileChooser>("Choose an instrument (.vst3)",
                                                  defaultVst3Folder(), "*.vst3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc)
                         {
                             const auto file = fc.getResult();
                             if (file != juce::File())
                                 loadAndReport(file);
                         });
}

void KeysHostEditor::openInstrumentEditor()
{
    closeInstrumentEditor();

    if (auto* inst = host.instrumentInstance())
    {
        if (auto* ed = inst->createEditorIfNeeded())
            instEditor.reset(ed);
        else
            instEditor = std::make_unique<juce::GenericAudioProcessorEditor>(*inst);

        instWindow = std::make_unique<InstrumentWindow>(host.instrumentName(), *instEditor,
                                                        [this] { setInstrumentShown(false); });
        placeInstrumentWindow();
        instWindow->setVisible(instShown);
    }

    updateBar();
}

void KeysHostEditor::closeInstrumentEditor()
{
    instWindow = nullptr; // the window references the editor, so it goes first
    if (instEditor == nullptr)
        return;
    if (auto* inst = host.instrumentInstance())
        inst->editorBeingDeleted(instEditor.get()); // no-op for the generic fallback
    instEditor = nullptr;
}

void KeysHostEditor::placeInstrumentWindow()
{
    if (instWindow == nullptr)
        return;
    const auto keysBounds = getScreenBounds();
    const auto area = juce::Desktop::getInstance().getDisplays()
                          .getDisplayForRect(keysBounds)->userArea;
    const auto wb = instWindow->getBounds();
    // Above the keyboard window when there's room, otherwise over it, always on-screen.
    const int x = juce::jlimit(area.getX(), juce::jmax(area.getX(), area.getRight() - wb.getWidth()),
                               keysBounds.getX());
    int y = keysBounds.getY() - wb.getHeight() - 8;
    if (y < area.getY())
        y = juce::jlimit(area.getY(), juce::jmax(area.getY(), area.getBottom() - wb.getHeight()),
                         keysBounds.getY());
    instWindow->setTopLeftPosition(x, y);
}

void KeysHostEditor::setInstrumentShown(bool shown)
{
    instShown = shown;
    if (instWindow != nullptr)
    {
        instWindow->setVisible(shown);
        if (shown)
            instWindow->toFront(false);
    }
    updateBar();
}

void KeysHostEditor::updateBar()
{
    const bool loaded = host.hasInstrument();
    showHideButton.setEnabled(loaded);
    ejectButton.setEnabled(loaded);
    showHideButton.setButtonText(instShown ? "Hide Instrument" : "Show Instrument");
    if (loaded)
        instLabel.setText(host.instrumentName(), juce::dontSendNotification);
    else
        instLabel.setText(host.lastError().isNotEmpty() ? host.lastError()
                                                        : "No instrument loaded - everything still works like plain Keys",
                          juce::dontSendNotification);
}

void KeysHostEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    openInstrumentEditor();
}

void KeysHostEditor::timerCallback()
{
    // The embedded editor never resizes itself, so the host window reacts to the
    // Layout param instead: Performer's control strip needs the extra height.
    const int lay = (int) host.apvts.getRawParameterValue("uiLayout")->load();
    if (lay == lastLayout)
        return;
    lastLayout = lay;
    if (lay == 1)
        setSize(getWidth(), juce::jmax(getHeight(), barHeight + 720));
}

void KeysHostEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void KeysHostEditor::resized()
{
    auto area = getLocalBounds();

    auto bar = area.removeFromTop(barHeight).reduced(8, 5);
    loadButton.setBounds(bar.removeFromLeft(160));
    bar.removeFromLeft(8);
    ejectButton.setBounds(bar.removeFromRight(80));
    bar.removeFromRight(8);
    showHideButton.setBounds(bar.removeFromRight(150));
    bar.removeFromRight(8);
    instLabel.setBounds(bar);

    keysEditor.setBounds(area);

    if (picker != nullptr)
        picker->setBounds(getLocalBounds()); // the overlay always covers the whole editor
}
} // namespace keys
