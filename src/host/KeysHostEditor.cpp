#include "KeysHostEditor.h"
#include <okstudio/MouseOnly.h>
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
    constexpr int keysHeight = 640;      // the embedded editor's comfortable height
    // The height Keys Host opens at. It is no longer a *floor*: every section of the Keys
    // editor folds away, and the window follows what the folds add up to, so the real
    // minimum is the Keys editor's own (four bars and the margins, see absMinKeysHeight).
    constexpr int minKeysHeight = 620;
    constexpr int absMinKeysHeight = 150;

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
    title.setFont(skin::uiSemi(16.0f).withExtraKerningFactor(0.04f));
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
    folders.clear();
    emptyRow = nullptr;

    juce::String currentPublisher("\x01"); // never matches a real (or empty) publisher
    Folder* folder = nullptr;
    for (const auto& entry : findInstalledVst3s())
    {
        if (entry.publisher != currentPublisher)
        {
            currentPublisher = entry.publisher;
            folder = folders.add(new Folder());
            folder->name = currentPublisher.isEmpty() ? "Other" : currentPublisher;
            folder->open = openFolderNames.contains(folder->name);
            folder->header = std::make_unique<juce::TextButton>();
            folder->header->setLookAndFeel(&folderLnf);
            folder->header->onClick = [this, folder] { setFolderOpen(*folder, ! folder->open); };
            rowHolder.addAndMakeVisible(*folder->header);
        }
        auto* row = folder->rows.add(new juce::TextButton(entry.file.getFileNameWithoutExtension()));
        const auto file = entry.file;
        row->setLookAndFeel(&itemLnf);
        row->onClick = [this, file] { if (onPick) onPick(file); };
        rowHolder.addChildComponent(*row); // shown only while its folder is open
    }

    for (auto* f : folders)
        applyFolderState(*f);

    if (folders.isEmpty())
    {
        emptyRow = std::make_unique<juce::TextButton>("No VST3s found - use Browse files...");
        emptyRow->setLookAndFeel(&itemLnf);
        emptyRow->setEnabled(false);
        rowHolder.addAndMakeVisible(*emptyRow);
    }
    resized();
}

void InstrumentPicker::FolderLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b,
                                                         bool, bool)
{
    g.setFont(skin::uiSemi(15.0f));
    g.setColour(skin::text);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().withTrimmedLeft(14).withTrimmedRight(12),
                     juce::Justification::centredLeft, 1);
}

void InstrumentPicker::ItemLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                                             const juce::Colour&,
                                                             bool highlighted, bool down)
{
    // No chip at all at rest: giving an instrument the same rounded fill as a folder is
    // what made the two read as siblings, and dimming it only softened the problem.
    // Leaving folders as the only raised objects makes the indent do its job. The row
    // lights up under the mouse, which is where the clickable affordance comes from.
    if (! b.isEnabled() || ! (highlighted || down))
        return;

    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(skin::well.withAlpha(down ? 1.0f : 0.85f));
    g.fillRoundedRectangle(r, skin::radius);
    g.setColour(skin::accentOf(b).base.withAlpha(down ? 0.55f : 0.32f)); // the row, not the LnF
    g.drawRoundedRectangle(r.reduced(0.5f), skin::radius, 1.0f);
}

void InstrumentPicker::ItemLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b,
                                                       bool highlighted, bool)
{
    g.setFont(skin::ui(15.0f));
    g.setColour(! b.isEnabled() ? skin::textFaint : highlighted ? skin::text : skin::textDim);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().withTrimmedLeft(14).withTrimmedRight(12),
                     juce::Justification::centredLeft, 1);
}

void InstrumentPicker::applyFolderState(Folder& folder)
{
    // Triangle then name then count: "5 Spitfire Audio" reads as a folder you can open,
    // where a bare name reads as something to click and load.
    const auto arrow = juce::String::fromUTF8(folder.open ? "\xe2\x96\xbe" : "\xe2\x96\xb8");
    folder.header->setButtonText(arrow + "  " + folder.name + "   (" + juce::String(folder.rows.size()) + ")");
    for (auto* row : folder.rows)
        row->setVisible(folder.open);
}

void InstrumentPicker::setFolderOpen(Folder& folder, bool open)
{
    folder.open = open;
    // Remembered by name, so Rescan doesn't collapse what you just opened.
    if (open)
        openFolderNames.addIfNotAlreadyThere(folder.name);
    else
        openFolderNames.removeString(folder.name);

    applyFolderState(folder);
    resized();
}

juce::Rectangle<int> InstrumentPicker::panelBounds() const
{
    return getLocalBounds().withSizeKeepingCentre(juce::jmin(520, getWidth() - 40),
                                                  juce::jmin(560, getHeight() - 40));
}

void InstrumentPicker::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.65f)); // dim the editor behind the panel
    const auto panel = panelBounds().toFloat();
    g.setColour(juce::Colour(0xff1c1f24));
    g.fillRoundedRectangle(panel, skin::panelRadius);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.fillRoundedRectangle(panel.withHeight(1.5f).reduced(skin::panelRadius, 0.0f), 0.75f);
    skin::glowRect(g, panel, skin::panelRadius, skin::accentOf(*this).base, 0.55f);
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
    // Folder headers are click targets now, not captions, so they get a full row.
    const int rowH = 40, indent = 26;
    const int width = panel.getWidth() - viewport.getScrollBarThickness();
    int y = 0;
    for (auto* folder : folders)
    {
        folder->header->setBounds(0, y, width, rowH - 4);
        y += rowH;
        if (! folder->open)
            continue; // closed folders take no space; their rows stay hidden
        for (auto* row : folder->rows)
        {
            row->setBounds(indent, y, width - indent, rowH - 4);
            y += rowH;
        }
    }
    if (emptyRow != nullptr)
    {
        emptyRow->setBounds(0, y, width, rowH - 4);
        y += rowH;
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
    setLookAndFeel(&hostLnf);
    instLabel.setColour(juce::Label::textColourId, skin::textDim);
    instLabel.setFont(skin::ui(13.5f));

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

    // Folding a section, or opening the arp with its step editor, changes what the Keys
    // editor needs, and the window follows it in both directions: minimizing a section should
    // actually make the window smaller, which is the whole point of being able to fold
    // one. Width is left alone - it is the keybed's, and Owen sets it deliberately.
    keysEditor.onIdealHeightChanged = [this](int wanted)
    {
        const int needed = juce::jlimit(barHeight + absMinKeysHeight, 1700, barHeight + wanted);
        if (needed != getHeight())
            setSize(getWidth(), needed);
    };

    host.onInstrumentWillChange = [this] { closeInstrumentEditor(); };
    host.addChangeListener(this);

    setResizable(true, true);
    // Asked, not copied. This was a literal 1010 until 2026-07-30, when a Generator button
    // joined Fill and Regen on the Pads bar and moved the editor's own floor to 1070 - and a
    // host window narrower than the editor it embeds carves controls off the right-hand end of
    // that bar with nothing to say so.
    const int keysMinWidth = keysEditor.minWidthForView();
    setResizeLimits(keysMinWidth, barHeight + absMinKeysHeight, 2600, 1700);
    // Owen resizes to the minimum every time anyway - open there.
    setSize(keysMinWidth, barHeight + minKeysHeight);
    juce::ignoreUnused(keysHeight);
    openInstrumentEditor(); // reflects "no instrument" too (label + bar state)
}

KeysHostEditor::~KeysHostEditor()
{
    host.onInstrumentWillChange = nullptr;
    host.removeChangeListener(this);
    closeInstrumentEditor();
    if (styledWindow != nullptr)
        styledWindow->setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void KeysHostEditor::parentHierarchyChanged()
{
    // Same standalone-chrome hook as KeysEditor (which skips itself when embedded
    // here): the wrapper window's title bar follows the skin, restored on teardown.
    // Deferred a message-loop turn for the same reason as there: restyling the
    // window mid-construction breaks the wrapper's content sizing.
    if (! juce::JUCEApplicationBase::isStandaloneApp() || styledWindow != nullptr)
        return;
    juce::Component::SafePointer<KeysHostEditor> safe(this);
    juce::MessageManager::callAsync([safe]
    {
        auto* e = safe.getComponent();
        if (e == nullptr || e->styledWindow != nullptr)
            return;
        if (auto* window = dynamic_cast<juce::DocumentWindow*>(e->getTopLevelComponent()))
        {
            e->styledWindow = window;
            window->setLookAndFeel(&e->hostLnf);
            window->setTitleBarHeight(38);
        }
    });
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
    // Placing against our own screen position is meaningless until we have one. On the
    // first open we are still being parented, so getScreenBounds() reads (0, 0) and
    // every offset below is computed from the wrong origin. Try again once we are up.
    // Exactly one retry: re-posting until isShowing() turns true would spin the message
    // thread forever for an editor that never becomes visible. If the second attempt is
    // still blind, place anyway - the clamp at the end keeps the window reachable.
    if (! isShowing() && ! instPlaceDeferred)
    {
        instPlaceDeferred = true;
        juce::Component::SafePointer<KeysHostEditor> safe(this);
        juce::MessageManager::callAsync([safe] { if (auto* e = safe.getComponent()) e->placeInstrumentWindow(); });
        return;
    }
    instPlaceDeferred = false;

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
    // The clamps above are in component coordinates, which exclude this window's native
    // title bar: pinning to area.getY() puts the bar itself off the top of the screen,
    // and the instrument window is then unmovable (no thick frame, title bar only).
    okstudio::ui::ensureWindowReachable(*instWindow);
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

void KeysHostEditor::paint(juce::Graphics& g)
{
    g.fillAll(skin::bgBot);

    // The instrument bar is a header band, same language as the editor below it.
    const auto bar = getLocalBounds().toFloat().withHeight((float) barHeight);
    g.setGradientFill({ skin::headerTop, 0.0f, 0.0f, skin::headerBot, 0.0f, bar.getBottom(), false });
    g.fillRect(bar);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRect(0.0f, bar.getBottom(), bar.getWidth(), 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.fillRect(0.0f, bar.getBottom() + 1.0f, bar.getWidth(), 1.0f);
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
