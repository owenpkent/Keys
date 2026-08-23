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
#include <juce_events/juce_events.h>

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

        beginTest("a halo gesture spends its whole travel on band the knob can actually reach");
        {
            // The range knobs opened lit on 2026-08-23, which made this reachable: the halo
            // drag ran over the span's *whole* travel while the band is capped at the distance
            // to the nearer rail, so most of every sweep moved nothing at all. H.TIME is the
            // worst of the four - it opens at 24 of 0..100, so its band can never exceed 24
            // however far the halo is turned, and 228 px of a 300 px drag were inert.
            //
            // These are the numbers of the shipping defaults rather than round ones on purpose:
            // the bug was invisible until a default put a face near a rail, and a test on a
            // centred knob would have passed throughout.
            juce::ScopedJuceInitialiser_GUI juceInit;

            RangeKnob rk;
            rk.face().setRange(0.0, 100.0, 1.0);
            rk.face().setValue(24.0, juce::dontSendNotification); // arpHumanize's own default
            rk.setSpan(100.0);                                    // arpHumanizeSpan's own default

            // The band, unchanged by any of this: the face is the centre and the low rail is
            // nearer, so the reach stops there and H.TIME plays 0..48.
            expectEquals(rk.room(), 24.0, "the nearer rail is 24 away");
            expectEquals(rk.reach(), 24.0, "the band is capped by the rail, not the span");
            expectEquals(rk.rangeLo(), 0.0, "the low end sits on the rail");
            expectEquals(rk.rangeHi(), 48.0, "the high end is as far the other way");

            // **The span itself is untouched.** A session, a host lane or the parameter's own
            // default may hold one wider than the face currently allows, and it has to survive
            // being loaded - that is what keeps H.TIME's 100 meaning "floor pinned at zero".
            expectEquals(rk.getSpan(), 100.0, "a span wider than the rail is still stored");

            // What the gesture may land on, which is the whole fix.
            expectEquals(rk.usefulSpanMax(), 24.0, "the gesture's ceiling is the reachable band");

            // A full sweep down closes the band completely, and a full sweep up reopens all of
            // it. Before this, 300 px down landed on 100 - 100 = 0 by luck on this knob, while
            // every intermediate position between 100 and 24 drew the identical arc.
            expectEquals(rk.spanFromDrag(100.0, -300.0), 0.0, "a full sweep down closes it");
            expectEquals(rk.spanFromDrag(0.0, 300.0), 24.0, "a full sweep up opens all of it");

            // The pixel that used to do nothing. A quarter of the sweep down from wide open is
            // a quarter off the band; it used to be the first of 228 px that changed nothing.
            expectEquals(rk.spanFromDrag(100.0, -75.0), 18.0, "a quarter sweep takes a quarter");
            expectEquals(rk.spanFromDrag(100.0, 1.0), 24.0, "already open: up does nothing");

            // The wheel answers out of the same arithmetic, a twentieth of the sweep a notch.
            expectEquals(rk.spanFromWheel(100.0, -1.0), 22.8, "one notch down is 5% of the band");
            expectEquals(rk.spanFromWheel(24.0, 1.0), 24.0, "one notch up at the ceiling holds");

            // VEL's shape: the ring carries a parameter of its own (0..127) and the face sits
            // at 100 of 0..127, so the rail is 27 away - the ceiling CLAUDE.md names for it.
            RangeKnob vel;
            vel.face().setRange(0.0, 127.0, 1.0);
            vel.face().setValue(100.0, juce::dontSendNotification); // arpVelLevel's default
            vel.setSpanMax(127.0);                                  // arpHumanVel's own range
            vel.setSpan(18.0);                                      // and its default
            expectEquals(vel.usefulSpanMax(), 27.0, "VEL's ring stops 27 from the top rail");
            expectEquals(vel.spanFromDrag(18.0, 300.0), 27.0, "a full sweep reaches the rail");
            expectEquals(vel.reach(), 18.0, "and the default sits inside it, as documented");

            // A face parked on a rail has no band to open at all. The gesture must leave the
            // stored span alone there rather than quietly wiping it: nothing is on screen to
            // say a drag did anything, so a drag must not have done anything.
            RangeKnob railed;
            railed.face().setRange(0.0, 100.0, 1.0);
            railed.face().setValue(0.0, juce::dontSendNotification);
            railed.setSpan(60.0);
            expectEquals(railed.usefulSpanMax(), 0.0, "no room at the rail");
            expectEquals(railed.spanFromDrag(60.0, -300.0), 60.0, "a dead halo stores nothing");
            expectEquals(railed.spanFromWheel(60.0, -1.0), 60.0, "and neither does its wheel");
        }
    }
};

static LayoutTests layoutTests;
} // namespace keys::tests
