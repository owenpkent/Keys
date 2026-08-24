// Layout invariants for the arp panel.
//
// **Every bug of 2026-08-14 was a layout bug, and the engine suite caught none of them**: the
// Chain tab laid out at zero width, Mute's default-constructed button counted as a tab and
// eating a cell, the Voice button at 22 px under a caption strip, three lanes at a different
// length from their neighbours. Screenshots and UI Automation geometry found all four. That is
// a slow loop and it needs a running app, so the rules those bugs broke live here instead.
//
// They are deliberately *rules*, not pixel snapshots: a snapshot of a layout that is still being
// designed fails every time the design moves, which trains people to delete tests. Each of these
// says something that must be true at any size and in any view.

#include "../src/PluginProcessor.h"
#include "../src/ui/ArpPanel.h"
#include "../src/ui/KeysLookAndFeel.h"
#include "../src/ui/RangeKnob.h"
#include "../src/PluginEditor.h"
#include <juce_events/juce_events.h>
#include <iterator> // std::size, so the parallel arrays below cannot fall out of step

namespace keys::tests
{
namespace
{
    struct Host
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        KeysProcessor processor;

        // The step editor only exists in Pattern shape, and a fresh processor is not in it -
        // so a test that wants lane tabs has to ask for them. Worth a helper rather than a
        // line in each test: forgetting it makes the tabs *legitimately* absent, which reads
        // like a layout failure and is not one.
        void usePattern()
        {
            if (auto* p = processor.apvts.getParameter(
                    KeysProcessor::arpParamId(0, KeysProcessor::apPattern)))
                p->setValueNotifyingHost(1.0f);
        }
    };

    // Owen's window, near enough: the editor's minimum width is 1320 (it rose from 1280 when
    // the settings gear joined the Controls bar on 2026-08-17) and the arp panel gets all of it.
    constexpr int panelW = 1320;

    // **And the narrow end is not there.** The arp section detaches into a window of its own,
    // whose floor is `ArpPanel::minPanelWidth()` and whose content is that less the window's
    // 8 px resizable border - a few hundred pixels under the docked case. Testing only the
    // docked floor is what let the ninth macro knob land: it was measured against a ~614 px
    // column and overflowed a ~420 px one, silently, because JUCE clamps a removeFromLeft to
    // what is there and the row simply ate the shortfall from its last cell.
    //
    // Anything that fits here fits everywhere. That is the whole reason this file exists, and
    // it only holds if "here" is genuinely the narrowest place the view is drawn. **This sweep
    // is what maintains `arpDeepPageMinW`**, the half of that floor which is measured rather
    // than derived: lower it and a band slider and a lane tab starve here, which is how the
    // number was found in the first place.
    const int detachedPanelW = ArpPanel::minPanelWidth();

    bool isTarget(const juce::Component& c)
    {
        return dynamic_cast<const juce::Button*>(&c) != nullptr
            || dynamic_cast<const juce::Slider*>(&c) != nullptr
            || dynamic_cast<const juce::ComboBox*>(&c) != nullptr;
    }

    // Every visible thing you could click, wherever it is in the tree.
    void collectTargets(juce::Component& c, juce::Array<juce::Component*>& out)
    {
        for (auto* child : c.getChildren())
        {
            if (! child->isVisible())
                continue; // an invisible control is allowed any bounds; nothing can reach it
            if (isTarget(*child))
                out.add(child);
            collectTargets(*child, out);
        }
    }
}

class LayoutTests : public juce::UnitTest
{
public:
    LayoutTests() : juce::UnitTest("Keys arp layout", "keys") {}

    void runTest() override
    {
        beginTest("every popup row is sized wide enough to draw its own text");
        {
            // Written on 2026-08-21 while chasing Owen's "when you select octave plus fifth,
            // it looks like it only just does octave", on the theory that the *row* was
            // ellipsising and he was reading a truncated label.
            //
            // **It was not.** This passed the moment it was written, which is what sent the
            // hunt on to the semitone table, where the bug actually was - "+ Octave & 5th"
            // names two intervals and the engine was playing one. Kept anyway, because the
            // trap it rules out is real and has bitten this codebase once already:
            // getIdealPopupMenuItemSize measures with Font::getStringWidthFloat, which
            // CLAUDE.md records as under-measuring - it is what drew the chord library's
            // `iim7` as `iim`. drawPopupMenuItem then draws into area.reduced(26, 0) with
            // ellipses on, so any shortfall past the 10 px of slack comes off the end of the
            // longest row in the menu, and the text simply ends early with nothing to say so.
            //
            // A rule over the whole list rather than one string: it is the *next* long entry
            // appended to any menu that this is here to catch.
            //
            // The initialiser is needed even though no processor is: a LookAndFeel touches
            // Desktop on the way out and the glyph work brings up the typeface cache, both
            // DeletedAtShutdown singletons. Without a ScopedJuceInitialiser_GUI in scope they
            // are created here and torn down at static destruction, which is a leak-detector
            // assertion or a crash depending on which test ran first. Every other block in
            // this file gets one through `Host`; this one has no processor, so it says so.
            const juce::ScopedJuceInitialiser_GUI juceInit;
            KeysLookAndFeel lnf;
            const auto font = lnf.getPopupMenuFont();
            for (const auto& text : KeysProcessor::harmonyChoices())
            {
                int w = 0, h = 0;
                lnf.getIdealPopupMenuItemSize(text, false, 34, w, h);
                juce::GlyphArrangement ga;
                ga.addLineOfText(font, text, 0.0f, 0.0f);
                const float drawn = ga.getBoundingBox(0, -1, true).getRight();
                expect((float) w >= drawn + 52.0f,
                       "the popup row \"" + text + "\" is " + juce::String(w)
                           + " px wide but needs " + juce::String((int) std::ceil(drawn + 52.0f))
                           + " to draw its text inside its own gutters, so it ellipsises");
            }
        }

        beginTest("no visible control is starved, in any view or page");
        {
            // The rule the Chain tab broke by being laid out at zero width, and the Shape
            // stepper broke on 2026-08-02 by being squeezed to nothing: a control that is on
            // screen must be big enough to see and hit. Not the full 34 px mouse-only floor -
            // bar chips are 24 px tall by design - but nothing may collapse.
            Host h;
            h.usePattern(); // the busiest shape, and the only one with a Draw page
            ArpPanel panel { h.processor };
            panel.setSize(panelW, panel.preferredHeight());

            const std::pair<bool, ArpPanel::Page> views[] = {
                { true, ArpPanel::Page::setup },   // the macro view; page is ignored
                { false, ArpPanel::Page::setup },
                { false, ArpPanel::Page::slots },
                { false, ArpPanel::Page::steps },
            };
            // Both floors: the docked editor's, and the detached Arp window's, which is the
            // narrower of the two and the one nothing was checking.
            for (const int w : { panelW, detachedPanelW })
            for (const auto& [macro, page] : views)
            {
                panel.setMacroView(macro);
                if (! macro)
                    panel.setPage(page);
                panel.setSize(w, panel.preferredHeight());

                juce::Array<juce::Component*> targets;
                collectTargets(panel, targets);
                expect(targets.size() > 0, "the view has controls on it at all");
                for (auto* t : targets)
                {
                    const auto b = t->getBounds();
                    auto name = t->getTitle().isNotEmpty() ? t->getTitle() : t->getName();
                    // An unnamed control reported as '' is a bug report with its subject line
                    // removed. Fall back to its type and its ancestry, which is enough to find
                    // it in the source in one search.
                    if (name.isEmpty())
                    {
                        name = "<" + juce::String(typeid(*t).name()) + ">";
                        for (auto* q = t->getParentComponent(); q != nullptr; q = q->getParentComponent())
                            name += " in " + (q->getTitle().isNotEmpty() ? q->getTitle()
                                              : q->getName().isNotEmpty() ? q->getName()
                                              : juce::String(typeid(*q).name()));
                    }
                    expect(b.getWidth() >= 20 && b.getHeight() >= 16,
                           "starved control '" + name + "' at " + b.toString()
                               + " (panel " + juce::String(w) + "px"
                               + (macro ? ", macro view)" : ", page " + juce::String((int) page) + ")"));
                }
            }
        }

        beginTest("the macro knob strip is never clamped, at either window's floor");
        {
            // The starvation sweep above cannot catch this on its own and it is worth saying
            // why, because the same hole will be there for the next control. Its floor is
            // `width >= 20`, which a knob squeezed to 20 passes - and a *range* knob is wider
            // than a plain one by its two rings, so the first cell to be eaten loses 16 px to
            // ring before its face loses anything. H.TIME's face went to 16 px inside a cell
            // that still measured 32 and the sweep waved it through.
            //
            // So this asks the question directly: does the row get the width its own
            // arithmetic says it needs, or is JUCE clamping the difference away? Every knob at
            // its documented floor, in a row that was handed exactly what it asked for.
            Host h;
            ArpPanel panel { h.processor };
            panel.setMacroView(true);

            // At `minMacroWidth()` as well as the docked floor: that is the width the macro
            // view's own arithmetic says it needs, so it is the width that proves the
            // derivation right. No window is actually that narrow - minPanelWidth() is wider,
            // because the deep pages want more - but if this ever fails, the derivation and
            // the layout have drifted apart, which is the thing worth hearing about.
            for (const int w : { panelW, ArpPanel::minMacroWidth() })
            {
                panel.setSize(w, panel.preferredHeight());
                juce::Array<juce::Component*> knobs;
                collectTargets(panel, knobs);

                int found = 0;
                for (auto* t : knobs)
                {
                    const auto name = t->getTitle();
                    if (! name.startsWith("Macro ") || dynamic_cast<juce::Slider*>(t) == nullptr)
                        continue;
                    if (name.contains("rate") || name.contains("harmony")) // not strip knobs
                        continue;
                    ++found;
                    expect(t->getWidth() >= 34,
                           "macro knob '" + name + "' is " + juce::String(t->getWidth())
                               + " px wide at panel " + juce::String(w)
                               + " px - under the mouse-only floor");
                }
                // Every knob on every card. If this drops, the filter above stopped matching
                // and the loop is passing by finding nothing, which is the failure mode a
                // name-matched sweep has and a hand-written list does not.
                expectEquals(found, ArpPanel::MacroRow::numKnobs * KeysProcessor::uiArpLines,
                             "every macro knob on every card was measured");
            }
        }

        beginTest("the Shape combo can draw its longest name at either window's floor");
        {
            // The dice took 34 px plus a 14 px gap out of this row, and the comment beside
            // arpMacroShapeMaxW asserted the combo "still gets ~166 px, room for the longest
            // name". It gets 151 at minPanelWidth and less at minMacroWidth - the figure was
            // carried over from before the dice and never re-derived. **So this measures it
            // rather than restating it**, which is the only version of that claim that can
            // stay true: a name appended to the shape list, or a control added to this row,
            // moves the answer and nothing on screen says the label got shorter.
            //
            // A ComboBox draws its text into `getWidth() - 32` (label inset plus the arrow),
            // and ellipsises silently past that.
            Host h;
            ArpPanel panel { h.processor };
            panel.setMacroView(true);

            KeysLookAndFeel lnf;
            for (const int w : { panelW, ArpPanel::minPanelWidth() })
            {
                panel.setSize(w, panel.preferredHeight());
                juce::Array<juce::Component*> all;
                collectTargets(panel, all);

                int found = 0;
                for (auto* t : all)
                {
                    auto* box = dynamic_cast<juce::ComboBox*>(t);
                    if (box == nullptr || ! t->getTitle().startsWith("Macro shape"))
                        continue;
                    ++found;

                    const auto font = lnf.getComboBoxFont(*box);
                    float widest = 0.0f;
                    juce::String widestText;
                    for (int i = 0; i < box->getNumItems(); ++i)
                    {
                        juce::GlyphArrangement ga;
                        ga.addLineOfText(font, box->getItemText(i), 0.0f, 0.0f);
                        const float drawn = ga.getBoundingBox(0, -1, true).getRight();
                        if (drawn > widest)
                        {
                            widest = drawn;
                            widestText = box->getItemText(i);
                        }
                    }
                    expect((float) (box->getWidth() - 32) >= widest,
                           "'" + t->getTitle() + "' is " + juce::String(box->getWidth())
                               + " px at panel " + juce::String(w) + ", leaving "
                               + juce::String(box->getWidth() - 32) + " px of text area for \""
                               + widestText + "\", which needs "
                               + juce::String((int) std::ceil(widest)) + " - it ellipsises");
                }
                expectEquals(found, KeysProcessor::uiArpLines,
                             "every card's Shape combo was measured");
            }
        }

        beginTest("the harmony menu carries every row's own id, enabled, in order");
        {
            // 2026-08-22, Owen: "I can't turn off the harmony. off is grey."
            //
            // `ComboBox::isItemEnabled` takes an item **ID**; `getItemText` and `getItemId` take
            // an **index**. The hand-rolled two-column popup passed the loop index into the one
            // call of the three that wants an id. The list is added with `addItemList(..., 1)`,
            // so every row was checked against its neighbour's flag - invisible for all of them
            // but the first, because `isItemEnabled` answers false for an id no item has, and
            // index 0 is **Off**. The one row that silences a voice was the one row greyed.
            //
            // **The ids are the assertion, not just the enablement.** A first cut of this test
            // pinned the text, the enabled flag and the column break, and passed green with
            // `menu.addItem(i, ...)` - the identical index-for-id slip one call over, which
            // makes "+ Perfect 5th" select "+ Tritone" and gives Off the id 0 that JUCE refuses
            // outright. The id is the value this whole bug class turns on, so it is the value
            // worth pinning.
            //
            // Against the **live** boxes on a real panel, not a synthetic ComboBox filled by
            // re-typing production's own setup line: a test that rebuilds what it is guarding
            // cannot see that setup change.
            Host h;
            ArpPanel panel { h.processor };
            panel.setMacroView(true);
            panel.setSize(panelW, panel.preferredHeight());

            juce::Array<juce::Component*> all;
            collectTargets(panel, all);

            int boxes = 0;
            for (auto* t : all)
            {
                auto* box = dynamic_cast<juce::ComboBox*>(t);
                if (box == nullptr || ! t->getTitle().startsWith("Macro harmony"))
                    continue;
                if (t->getTitle().contains("chance")) // the knob beside it, not the combo
                    continue;
                ++boxes;

                auto menu = ArpPanel::buildHarmonyMenu(*box);
                int row = 0, breaks = 0;
                juce::String breakAfter;
                for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                {
                    const auto& item = it.getItem();
                    if (item.isSectionHeader || item.isSeparator)
                        continue;

                    // Every row, in order: its own id, its own text, and enabled.
                    expectEquals(item.itemID, box->getItemId(row),
                                 "row " + juce::String(row) + " carries its own item id");
                    expectEquals(item.text, box->getItemText(row),
                                 "row " + juce::String(row) + " carries its own text");
                    expect(item.isEnabled, "harmony row \"" + item.text + "\" came up greyed");

                    // A column break is not an item: addColumnBreak() sets `shouldBreakAfter`
                    // on whatever was added last, so it shows up on the row before the split.
                    if (item.shouldBreakAfter)
                    {
                        ++breaks;
                        breakAfter = item.text;
                    }
                    ++row;
                }
                expectEquals(row, KeysProcessor::harmonyChoices().size(),
                             "every choice reached the menu");
                expectEquals(breaks, 1, "exactly one column break");
                expectEquals(breakAfter, juce::String("- minor 2nd"),
                             "the break falls after the last descending interval");
            }
            expectEquals(boxes, 2 * KeysProcessor::uiArpLines,
                         "two harmony combos on every card were measured");
        }

        beginTest("the harmony table stays grouped, descending before ascending");
        {
            // The two-column split is one break in an ordered list, so it can only be correct
            // while the list is grouped: every non-positive interval before every positive one.
            // Nothing in the table's own type enforces that, and the table's stated rule is that
            // **appending is the only safe edit** - so appending a *descending* interval would
            // land it after the ascending ones and draw it at the foot of the wrong column, with
            // one break still firing and nothing on screen to say so.
            //
            // This is what makes the break safe to derive rather than hand-place: break the
            // grouping and this fails loudly instead.
            const auto choices = KeysProcessor::harmonyChoices();
            bool seenAscending = false;
            for (int i = 0; i < choices.size(); ++i)
            {
                // The 0-based choice index, which is the combo's index - not its id, which is
                // one more. Getting that wrong is what this whole area keeps costing.
                const int semis = KeysProcessor::harmonySemisFor(i);
                if (semis > 0)
                    seenAscending = true;
                else
                    expect(! seenAscending,
                           "\"" + choices[i] + "\" (" + juce::String(semis) + ") sits after an "
                           "ascending interval, so the two-column split would mis-place it");
            }
            expect(seenAscending, "the table has ascending intervals at all");
        }

        beginTest("every lane tab is laid out, and they are all the same width");
        {
            // Chain was built, made visible and given zero width, so it was in the component
            // tree and absent from the screen - and Rand next to it was squeezed to 60% of its
            // neighbours. Uniformity is the tell: a row of tabs that disagree about width is a
            // row that has run out of room, whatever the last one's width happens to be.
            Host h;
            h.usePattern();
            ArpPanel panel { h.processor };
            panel.setMacroView(false);
            panel.setPage(ArpPanel::Page::steps);
            panel.setSize(panelW, panel.preferredHeight());

            juce::Array<juce::Component*> targets;
            collectTargets(panel, targets);

            // The lane tabs are the buttons whose text is a lane's name.
            static const char* const laneNames[] = { "Note", "Octave", "Velocity", "Gate",
                                                     "Ratchet", "Chance", "Transpose", "Late",
                                                     "Harmony", "Chord", "Rand", "Chain" };
            int found = 0, firstW = -1;
            for (const char* wanted : laneNames)
            {
                juce::Component* tab = nullptr;
                for (auto* t : targets)
                    if (auto* b = dynamic_cast<juce::Button*>(t))
                        if (b->getButtonText() == wanted)
                        {
                            tab = t;
                            break;
                        }
                expect(tab != nullptr, juce::String("lane tab on screen: ") + wanted);
                if (tab == nullptr)
                    continue;
                ++found;
                const int w = tab->getWidth();
                expect(w >= 70, juce::String(wanted) + " is at or above the tab floor, got "
                                    + juce::String(w));
                if (firstW < 0)
                    firstW = w;
                else
                    expect(std::abs(w - firstW) <= 1,
                           juce::String(wanted) + " is the same width as the others ("
                               + juce::String(w) + " vs " + juce::String(firstW) + ")");
            }
            expectEquals(found, 12, "all twelve lane tabs were laid out");
        }

        beginTest("the panel is exactly as tall as the view showing");
        {
            // This asserted the *opposite* until 2026-08-16 ("the panel is one height in every
            // view and page"), and the reversal was Owen's call: "fix arp", after "there's some
            // deadspace I want to remove at bottom". Paging solved the *size* problem on
            // 2026-08-14 - the un-paged deep view was 612 px against the macro view's 240, so
            // Details grew the window by 372 - and a shared constant then solved the *movement*.
            // What the constant cost was invisible: it was a max over five sums, so every view
            // under the tallest carried the difference as dead panel, 174 px of it on Slots.
            //
            // So the contract is now the other one, and it is worth a test because the failure
            // mode is silent either way: nothing on screen says a panel is reserving room it
            // never draws into.
            Host h;
            ArpPanel panel { h.processor };
            panel.setMacroView(true);
            const int macroH = panel.preferredHeight();

            // **Collapsing the bottom row is worth about a card** (2026-08-19, Owen: "maybe you
            // should be able to minimize bottom arps"). Four lines in a 2x2 grid is two card
            // rows, and the All view alone was setting the editor's minimum window height - so
            // this is the test that the fold actually buys the height back rather than merely
            // hiding two cards inside a box that stayed the same size. It also pins the other
            // half of the contract: folding a *view* must not disturb the lines behind it.
            expect(! panel.bottomRowFolded(), "the bottom row starts open");
            panel.setBottomRowFolded(true);
            const int foldedH = panel.preferredHeight();
            expect(panel.bottomRowFolded(), "the fold stuck");
            expect(foldedH < macroH,
                   "collapsing the bottom row made the panel shorter ("
                       + juce::String(foldedH) + " vs " + juce::String(macroH) + ")");
            // A card row is ~323 px and the strip that replaces it is 34, so the saving is most
            // of a card. A loose floor rather than an exact number, so tweaking a knob row does
            // not fail this, but tight enough that a fold saving nothing would.
            expect(macroH - foldedH > 200,
                   "the fold saved most of a card row, not a token few pixels ("
                       + juce::String(macroH - foldedH) + " px)");
            panel.setBottomRowFolded(false);
            expectEquals(panel.preferredHeight(), macroH, "unfolding put the height back exactly");

            panel.setMacroView(false);
            std::map<ArpPanel::Page, int> pageH;
            for (const auto page : { ArpPanel::Page::setup, ArpPanel::Page::slots,
                                     ArpPanel::Page::steps })
            {
                panel.setPage(page);
                pageH[page] = panel.preferredHeight();
                expect(panel.preferredHeight() > 0,
                       "page " + juce::String((int) page) + " has a height");
            }

            // Slots is the short one - twelve cards and an action row, no band and no lane grid -
            // so it is the page that proves nothing is padding itself out to the tallest.
            expect(pageH[ArpPanel::Page::slots] < pageH[ArpPanel::Page::setup],
                   "the Slots page is shorter than Setup rather than padded up to it ("
                       + juce::String(pageH[ArpPanel::Page::slots]) + " vs "
                       + juce::String(pageH[ArpPanel::Page::setup]) + ")");
            expect(pageH[ArpPanel::Page::slots] < macroH,
                   "the Slots page is shorter than the macro view too");

            // Steps has nothing to draw outside Pattern shape, so it falls back to Setup's
            // height rather than reserving the lane grid's - the one place a page's height
            // depends on something other than which page it is.
            expectEquals(pageH[ArpPanel::Page::steps], pageH[ArpPanel::Page::setup],
                         "outside Pattern shape, Steps falls back to Setup's height");
        }

        beginTest("opening the panel repairs lanes that disagree about length");
        {
            // A lane appended by an update arrives at ArpPattern's default 8 while the rest of
            // the pattern may be at 16 or 32, and the grid draws each lane at its own length -
            // so it silently shows a different number of cells. With Link on the panel is what
            // puts them back, on open rather than on the next nudge.
            Host h;
            auto& lanes = h.processor.arpLine(0).lanes;
            for (int l = 0; l < ArpEngine::numLanes; ++l)
                lanes.length[(size_t) l].store(16);
            lanes.length[(size_t) ArpEngine::laneRand].store(8);   // as if freshly appended
            lanes.length[(size_t) ArpEngine::laneChain].store(8);

            ArpPanel panel { h.processor };
            panel.setSize(panelW, panel.preferredHeight());

            for (int l = 0; l < ArpEngine::numLanes; ++l)
                expectEquals(lanes.length[(size_t) l].load(), 16,
                             "lane " + juce::String(l) + " was put back in step");
        }

        beginTest("Link off leaves lanes of different lengths alone");
        {
            // The other half of the same rule: polymeter is the whole point of the switch, so
            // the repair above must never run behind it.
            Host h;
            if (auto* link = h.processor.apvts.getParameter(
                    KeysProcessor::arpParamId(0, KeysProcessor::apLinkLanes)))
                link->setValueNotifyingHost(0.0f);

            auto& lanes = h.processor.arpLine(0).lanes;
            for (int l = 0; l < ArpEngine::numLanes; ++l)
                lanes.length[(size_t) l].store(16);
            lanes.length[(size_t) ArpEngine::laneOctave].store(3); // a deliberate 3-against-16

            ArpPanel panel { h.processor };
            panel.setSize(panelW, panel.preferredHeight());

            expectEquals(lanes.length[(size_t) ArpEngine::laneOctave].load(), 3,
                         "a deliberately short lane survived the panel opening");
        }

        beginTest("the pad range knobs hold still when nothing is touching them");
        {
            // **The regression that cost an afternoon** (2026-08-23, Owen: "feels like it's
            // fighting me... is there a race condition"). There was: `timerCallback` pushed a
            // span of its own beside syncPadRangeKnobs, and it passed the band's *full* width
            // to a control whose span is the reach on **each** side. So the band doubled ten
            // times a second until it saturated against the nearer wall, and it did that while
            // the halo was under the hand. Every symptom chased that day - a halo that would
            // not open, a knob that seemed to drag its own band about, Strum reading "0-128 ms"
            // with its knob at 64 - was this one arithmetic slip wearing different clothes.
            //
            // So the test is not about the arithmetic: it runs the **real editor's timer** and
            // asks whether four parameters nobody is touching stay where they were put. Any
            // second writer that disagrees with syncPadRangeKnobs fails this, whatever it gets
            // wrong, which is the only shape of test that would have caught it.
            Host h;
            // No network thread out of a layout test - see KeysEditor::skipUpdateCheckForTest.
            // Scoped, because it is a public mutable static on a class that ships in the plugin:
            // set and left true, it silently covers every test that runs after this one and
            // covers nothing that runs before, so whether the guard works at all becomes a
            // question about registration order.
            const juce::ScopedValueSetter<bool> noUpdateCheck(
                KeysEditor::skipUpdateCheckForTest, true);

            struct Pair { const char* lo; const char* hi; float loV, hiV; };
            // Clear of both walls on purpose: a band already pinned at a wall is saturated, so
            // a doubling bug has nothing left to move and hides.
            //
            // Humanize's is an **odd** width (51), which is the pair this control cannot hold
            // exactly - the face snaps to whole units and the band is symmetric about it, so
            // the centre lands half a unit off and the derived ends can never round back to
            // what is stored. That is the pull's other failure mode (see syncPadRangeKnobs)
            // and it walks the same ten ticks as the doubling one.
            Pair pairs[] = { { "chordStrum", "chordStrumMax", 50.0f, 150.0f },
                             { "humanizeVelMin", "humanizeVelMax", 40.0f, 91.0f } };

            const auto put = [&h](const char* id, float v)
            {
                if (auto* p = h.processor.apvts.getParameter(id))
                    p->setValueNotifyingHost(p->convertTo0to1(v));
            };
            const auto get = [&h](const char* id)
            { return h.processor.apvts.getRawParameterValue(id)->load(); };

            for (const auto& pr : pairs)
            {
                put(pr.lo, pr.loV);
                put(pr.hi, pr.hiV);
            }

            KeysEditor ed { h.processor };
            ed.setSize(panelW, ed.idealHeight());

            // Several turns of the editor's own poll. The old bug doubled the span on every
            // one of them, so even a single tick showed it; ten leaves no doubt, and a drift
            // that needs a hundred would be a different bug worth failing on anyway.
            for (int tick = 0; tick < 10; ++tick)
                ed.tickForTest();

            // **What the knobs draw, not what the parameters say.** `RangeKnob::setSpan` fires
            // no callback, so a second writer handing it a wrong span corrupts the band on
            // screen and leaves the parameters exactly where they were - which is how this bug
            // hid from the first version of this very test. The readout under the knob and the
            // lit arc are both derived from these two, so this is the number Owen was reading
            // off the screen when he said it was fighting him.
            const RangeKnob* knobs[] = { &ed.strumKnobForTest(), &ed.humanizeKnobForTest() };
            // Two arrays walked as one, so they have to be the same length: a third range knob
            // added to `pairs` alone would simply go unasserted, and added to `knobs` alone
            // would read past the end of `pairs` the moment anybody widened a hard-coded 2.
            static_assert(std::size(pairs) == std::size(knobs),
                          "every pad range knob needs a parameter pair to check it against");
            for (size_t i = 0; i < std::size(pairs); ++i)
            {
                expectWithinAbsoluteError(knobs[i]->rangeLo(), (double) pairs[i].loV, 0.51,
                                          juce::String(pairs[i].lo) + ": the band's low end "
                                          "drew somewhere other than the value it holds");
                expectWithinAbsoluteError(knobs[i]->rangeHi(), (double) pairs[i].hiV, 0.51,
                                          juce::String(pairs[i].hi) + ": the band's high end "
                                          "drew somewhere other than the value it holds");
                expectWithinAbsoluteError(get(pairs[i].lo), pairs[i].loV, 0.51f,
                                          juce::String(pairs[i].lo) + " drifted on its own");
                expectWithinAbsoluteError(get(pairs[i].hi), pairs[i].hiV, 0.51f,
                                          juce::String(pairs[i].hi) + " drifted on its own");
            }

            // **And the pair can arrive the wrong way round** (2026-08-23, in review). Nothing
            // orders these two: `migrateStrumRange` says a host or an MCP client may write max
            // below min deliberately, and `baseVelocity01` sorts them before playing them.
            // Unsorted here, the derived span comes out negative, `setSpan` clamps it to zero,
            // and the settle test can then never be satisfied - the every-tick re-run this
            // whole test exists to catch, arriving by the one route the pairs above cannot
            // reach, with a zero-width band drawn over an engine spreading across 20..100.
            put("humanizeVelMin", 100.0f);
            put("humanizeVelMax", 20.0f);
            for (int tick = 0; tick < 10; ++tick)
                ed.tickForTest();
            expectWithinAbsoluteError(ed.humanizeKnobForTest().rangeLo(), 20.0, 0.51,
                                      "an inverted pair drew its low end somewhere else");
            expectWithinAbsoluteError(ed.humanizeKnobForTest().rangeHi(), 100.0, 0.51,
                                      "an inverted pair drew its high end somewhere else");
            expectWithinAbsoluteError(get("humanizeVelMin"), 100.0f, 0.51f,
                                      "the pull wrote back to a parameter it only reads");
            expectWithinAbsoluteError(get("humanizeVelMax"), 20.0f, 0.51f,
                                      "the pull wrote back to a parameter it only reads");
        }

        beginTest("the halo's travel is the same wherever the knob is");
        {
            // Three readings of the halo's ceiling were tried on 2026-08-23 and only this one
            // survives contact: it is the span's own maximum, and **it does not depend on
            // where the face is**. The two that read a wall - first the nearer, then the
            // farther - both made the same gesture worth a different amount depending on where
            // the knob had been left (Owen: "moving knob moves halo weird", then "dragging
            // halo is weird too"). The arp's VEL ring is the shape that was already right:
            // `arpHumanVel` is 0..127 however the level beside it moves.
            juce::ScopedJuceInitialiser_GUI juceInit;

            RangeKnob rk;
            rk.face().setRange(0.0, 200.0, 1.0); // Strum's own range

            // Half the face's travel, and asked for by nobody: spanMax() caps every ring there
            // because a band centred on the face can never open wider than that (room() is the
            // smaller of two numbers summing to the travel). A ceiling above half is inert by
            // construction, which is what the arp's two range knobs were still carrying while
            // the pads had it passed in by hand at one call site.
            const auto ceiling = rk.spanMax();
            expectEquals(ceiling, 100.0, "the ceiling is half the face's travel");
            for (const double where : { 0.0, 30.0, 100.0, 180.0, 200.0 })
            {
                rk.face().setValue(where, juce::dontSendNotification);
                expectEquals(rk.spanMax(), ceiling,
                             "the halo's ceiling moved when the knob did");
            }

            // **And the gestures themselves, at more than one face position.** The loop above
            // cannot fail: spanMax() is `jmin(override, travel * 0.5)`, a pure function of the
            // slider's range with no way to read the face at all. Where a wall creeps back in
            // is the two gesture functions - they carried `min(spanMax(), room())` until
            // 2026-08-23 - and asking them at exactly one position, as this test did for an
            // afternoon, would let that back in with the suite green. Off the rails, where the
            // gesture is live; the rails get their own test below.
            for (const double where : { 1.0, 30.0, 100.0, 180.0, 199.0 })
            {
                rk.face().setValue(where, juce::dontSendNotification);
                expectEquals(rk.spanFromDrag(0.0, 150.0), ceiling * 0.5,
                             "half a sweep stopped being half when the knob moved");
                expectEquals(rk.spanFromWheel(0.0, 1.0), ceiling * 0.05,
                             "and a notch stopped being a notch");
            }

            // A full sweep closes the band and reopens it; a quarter is worth a quarter. Off
            // the walls, so the gesture is live throughout - see the dead-halo test below.
            rk.face().setValue(100.0, juce::dontSendNotification);
            expectEquals(rk.spanFromDrag(0.0, 150.0), ceiling * 0.5, "half a sweep is half");
            expectEquals(rk.spanFromDrag(100.0, -300.0), 0.0, "a full sweep down closes it");
            expectEquals(rk.spanFromDrag(0.0, 300.0), 100.0, "a full sweep up opens all of it");
            expectEquals(rk.spanFromDrag(100.0, -75.0), 75.0, "a quarter sweep takes a quarter");
            expectEquals(rk.spanFromWheel(100.0, -1.0), 95.0, "one notch is 5% of the sweep");

            // And every one of those is a band the knob can actually show. This is the property
            // the ceiling exists for, so it is asserted rather than left to the number above:
            // wind the span all the way up at the face's midpoint and `reach()` uses the lot.
            rk.setSpan(rk.spanFromDrag(0.0, 300.0));
            expectEquals(rk.reach(), ceiling, "the top of the sweep is band you can see");
        }

        beginTest("a halo with nowhere to open stores nothing");
        {
            // **The guard, and why it is a second function rather than a smaller ceiling.**
            // At a rail `room()` is zero, so `reach()` is zero however far the span is wound -
            // there is no band here at any setting. Without this a drag runs to completion,
            // fires onSpanChanged, and brackets a host automation gesture around a parameter
            // move with nothing to show for it on screen, in the readout or in the sound.
            //
            // It was folded into the ceiling as `min(spanMax(), room())` until 2026-08-23, and
            // taking the face back out of the ceiling - which was right, and is the test above -
            // took the guard with it. Two questions, two functions: how far does the gesture
            // reach (never reads the face), and is there a band to open (has to).
            //
            // H.TIME's face at 0 is how Keys reaches this by an ordinary route: "no lateness"
            // is a setting, not a corner case.
            juce::ScopedJuceInitialiser_GUI juceInit;

            RangeKnob rk;
            rk.face().setRange(0.0, 200.0, 1.0);
            rk.setSpan(60.0);

            for (const double rail : { 0.0, 200.0 })
            {
                rk.face().setValue(rail, juce::dontSendNotification);
                expectEquals(rk.room(), 0.0, "there is no travel on the nearer side");
                expectEquals(rk.reach(), 0.0, "so the band cannot open at all");
                expectEquals(rk.spanFromDrag(60.0, 300.0), 60.0, "a drag up writes nothing");
                expectEquals(rk.spanFromDrag(60.0, -300.0), 60.0, "and neither does one down");
                expectEquals(rk.spanFromWheel(60.0, 1.0), 60.0, "the wheel writes nothing either");
            }

            // **And it must not touch the host's automation lane on the way to writing
            // nothing** (2026-08-23, in review). Guarding only the value write left
            // beginSpanDrag/endSpanDrag firing onSpanDragStart/onSpanDragEnd with nothing
            // between them, which is a begin/endChangeGesture pair on the ring's parameter -
            // in Ableton, with the lane armed in Touch or Latch, a touch and an untouch is a
            // write. So the dead halo did still write something; it wrote it somewhere this
            // test could not see. Driven through the handle's own mouse path rather than the
            // private entry point, so it is the gesture that is pinned and not an internal.
            int starts = 0, ends = 0;
            rk.onSpanDragStart = [&starts] { ++starts; };
            rk.onSpanDragEnd = [&ends] { ++ends; };
            rk.setSize(80, 80);
            const auto press = [&rk](float y)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                        { 0.0f, y }, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, &rk.spanHandle(), &rk.spanHandle(),
                                        juce::Time(), { 0.0f, y }, juce::Time(), 1, false);
            };

            rk.face().setValue(0.0, juce::dontSendNotification); // H.TIME's "no lateness"
            rk.spanHandle().mouseDown(press(40.0f));
            expect(! rk.spanDragging(), "a gesture opened where there is no band to open");
            rk.spanHandle().mouseUp(press(40.0f));
            expectEquals(starts, 0, "a dead halo touched the host's lane");
            expectEquals(ends, 0, "and untouched it, which is the write");

            rk.face().setValue(100.0, juce::dontSendNotification);
            rk.spanHandle().mouseDown(press(40.0f));
            expect(rk.spanDragging(), "and the live gesture stopped opening");
            rk.spanHandle().mouseUp(press(40.0f));
            expectEquals(starts, 1, "one touch, where there is something to touch");
            expectEquals(ends, 1, "and one untouch");

            // One step off the rail and it is live again, which is what makes this a guard
            // rather than a range: the span it was holding is still there, untouched.
            rk.face().setValue(1.0, juce::dontSendNotification);
            expectEquals(rk.getSpan(), 60.0, "the stored span survived the rail");
            expect(rk.spanFromDrag(60.0, -300.0) < 60.0, "and the gesture works again");
        }

        beginTest("H.TIME opens as the band the docs say it does");
        {
            // **The one assertion the 2026-08-23 rewrite dropped.** CLAUDE.md states a
            // shipping band for `arpHumanize` - 0-22 since later that same day, 0-48 before it -
            // and StateTests pins the two *parameters*, but nothing was left checking that those
            // two numbers put that band on screen. StateTests does keep VEL's own relationship
            // (its ring fits inside what its level allows); this is H.TIME's twin.
            //
            // The knob is built here the way ArpPanel builds it - face range and ring range
            // both read off the APVTS, values read off the defaults - so a change to either
            // default, either range, or reach()'s clamp fails this rather than quietly moving
            // a documented band.
            Host h;
            const auto faceId = KeysProcessor::arpParamId(0, KeysProcessor::apHumanize);
            const auto ringId = KeysProcessor::arpParamId(0, KeysProcessor::apHumanizeSpan);
            const auto faceRange = h.processor.apvts.getParameterRange(faceId);
            const auto ringRange = h.processor.apvts.getParameterRange(ringId);

            RangeKnob rk;
            rk.face().setRange((double) faceRange.start, (double) faceRange.end, 1.0);
            rk.setSpanMax((double) (ringRange.end - ringRange.start));
            rk.face().setValue((double) h.processor.apvts.getRawParameterValue(faceId)->load(),
                               juce::dontSendNotification);
            rk.setSpan((double) h.processor.apvts.getRawParameterValue(ringId)->load());

            expectEquals(rk.rangeLo(), 0.0, "the band opens at dead on the grid");
            expectEquals(rk.rangeHi(), 22.0, "and reaches twice the knob");
            // Stated as the relationship as well as the two numbers, since that is what makes
            // "twice the knob" true rather than a coincidence of 11 and 22: the ring is open
            // wider than the face's own value, so the nearer rail - zero, dead on the grid - is
            // what both sides stop at.
            expectEquals(rk.reach(), rk.face().getValue(), "the band reaches the knob's own value");
            expect(rk.getSpan() >= rk.face().getValue(),
                   "which only holds because the ring is open wider than that");
        }

        beginTest("the band stays symmetric, so a halo drag can never move the knob");
        {
            // **The invariant the pad knobs are built on.** Strum and Humanize are stored as
            // nothing but their two ends, and the face is derived as the midpoint of them - so
            // the band being symmetric is what makes that derivation exact. Letting each end
            // stop at its own wall was tried on 2026-08-23 and reverted within the hour: with
            // the low end clipped, the pair's midpoint slid off the face, the knob crept under
            // the halo, and the pointer sat outside the middle of its own lit arc. That is the
            // thing centring the band on the face was for (2026-08-19, "moving the halo
            // shouldn't move knob"), so this pins it rather than leaving it to be rediscovered.
            juce::ScopedJuceInitialiser_GUI juceInit;

            RangeKnob rk;
            rk.face().setRange(0.0, 200.0, 1.0);
            rk.setSpanMax(100.0);

            for (const double where : { 0.0, 20.0, 64.0, 100.0, 175.0, 200.0 })
                for (const double howWide : { 0.0, 10.0, 64.0, 100.0 })
                {
                    rk.face().setValue(where, juce::dontSendNotification);
                    rk.setSpan(howWide);
                    const auto lo = rk.rangeLo(), hi = rk.rangeHi();
                    expectWithinAbsoluteError((lo + hi) * 0.5, where, 1.0e-9,
                                              "the knob is the midpoint of its own band");
                    expect(lo >= 0.0 && hi <= 200.0, "and the band stays inside the range");
                }

            // The cost of that invariant, stated so nobody "fixes" it by accident: near a wall
            // the band narrows, because the nearer side governs both. The span itself is
            // untouched and comes back the moment the knob leaves the wall.
            rk.face().setValue(30.0, juce::dontSendNotification);
            rk.setSpan(50.0);
            expectEquals(rk.rangeLo(), 0.0, "the wall stops the near side");
            expectEquals(rk.rangeHi(), 60.0, "and the far side matches it, keeping the centre");
            expectEquals(rk.getSpan(), 50.0, "the span itself is only held back, not lost");
            rk.face().setValue(100.0, juce::dontSendNotification);
            expectEquals(rk.rangeLo(), 50.0, "away from the wall the whole span is back");
            expectEquals(rk.rangeHi(), 150.0, "on both sides");
        }
    }
};

static LayoutTests layoutTests;
} // namespace keys::tests
