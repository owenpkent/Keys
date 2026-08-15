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

    // Owen's window, near enough: the editor's minimum width is 1280 and the arp panel gets all
    // of it. Testing at the *floor* is the point - anything that fits here fits everywhere, and
    // every starved-control bug this file exists for showed up first at the narrow end.
    constexpr int panelW = 1280;

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
            for (const auto& [macro, page] : views)
            {
                panel.setMacroView(macro);
                if (! macro)
                    panel.setPage(page);
                panel.setSize(panelW, panel.preferredHeight());

                juce::Array<juce::Component*> targets;
                collectTargets(panel, targets);
                expect(targets.size() > 0, "the view has controls on it at all");
                for (auto* t : targets)
                {
                    const auto b = t->getBounds();
                    const auto name = t->getTitle().isNotEmpty() ? t->getTitle() : t->getName();
                    expect(b.getWidth() >= 20 && b.getHeight() >= 16,
                           "starved control '" + name + "' at " + b.toString()
                               + (macro ? " (macro view)" : " (page " + juce::String((int) page) + ")"));
                }
            }
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

        beginTest("the panel is one height in every view and page");
        {
            // The whole point of paging: Details used to grow the window by 372 px and All
            // shrank it back. preferredHeight feeds the editor's idealHeight, so a constant
            // here is what stops the window moving.
            Host h;
            ArpPanel panel { h.processor };
            panel.setMacroView(true);
            const int macroH = panel.preferredHeight();

            panel.setMacroView(false);
            for (const auto page : { ArpPanel::Page::setup, ArpPanel::Page::slots,
                                     ArpPanel::Page::steps })
            {
                panel.setPage(page);
                expectEquals(panel.preferredHeight(), macroH,
                             "page " + juce::String((int) page) + " is the same height as the macro view");
            }
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
    }
};

static LayoutTests layoutTests;
} // namespace keys::tests
