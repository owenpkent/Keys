#include "KnobBank.h"
#include "CCMenu.h"
#include <okstudio/MouseOnly.h>

namespace keys
{
KnobBank::KnobBank(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);

    for (int i = 0; i < numKnobs; ++i)
    {
        auto& knob = knobs[(size_t) i];
        knob = std::make_unique<okstudio::RotaryKnob>();
        knob->setRange(0, 127, 1);
        knob->setValue(64, juce::dontSendNotification); // Octavium starts centred; silent until touched
        knob->onValueChange = [this, i]
        {
            const float value = (float) knobs[(size_t) i]->getValue();
            processor.sendCC(assignedCC(i), (int) value);
            // Also drives a hosted instrument's parameter directly, if Keys Host has
            // bound this knob to one; a no-op on plain Keys.
            processor.faderMoved(i, value / 127.0f);
        };
        addAndMakeVisible(*knob);

        auto& button = ccButtons[(size_t) i];
        button = std::make_unique<juce::TextButton>();
        button->onClick = [this, i]
        {
            cc::showMenu(*ccButtons[(size_t) i], assignedCC(i), [this, i](int cc)
            {
                if (auto* param = processor.apvts.getParameter("faderCC" + juce::String(i + 1)))
                    param->setValueNotifyingHost(param->convertTo0to1((float) cc));
            });
        };
        addAndMakeVisible(*button);
    }
    refreshAssignments();
}

int KnobBank::assignedCC(int knob) const
{
    return (int) processor.apvts.getRawParameterValue("faderCC" + juce::String(knob + 1))->load();
}

void KnobBank::refreshAssignments()
{
    for (int i = 0; i < numKnobs; ++i)
    {
        // A hosted instrument's bound parameter name takes over the label when
        // present (Keys Host auto-assignment); otherwise fall back to the CC label.
        const auto target = processor.faderTargetName(i);
        const auto text = target.isNotEmpty() ? target : cc::label(assignedCC(i));
        if (ccButtons[(size_t) i]->getButtonText() != text)
            ccButtons[(size_t) i]->setButtonText(text);
    }
}

void KnobBank::resized()
{
    auto area = getLocalBounds().reduced(10, 6);
    const int w = area.getWidth() / numKnobs;
    for (int i = 0; i < numKnobs; ++i)
    {
        auto col = area.removeFromLeft(w).reduced(8, 0);
        auto buttonArea = col.removeFromBottom(34);
        col.removeFromBottom(4);
        // Knob square, centred in what's left, never smaller than the kit's
        // recommended 48 px minimum for a rotary drag target.
        const int side = juce::jmax(48, juce::jmin(col.getWidth(), col.getHeight()));
        knobs[(size_t) i]->setBounds(col.withSizeKeepingCentre(side, side));
        ccButtons[(size_t) i]->setBounds(buttonArea);
    }
}

void KnobBank::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b1b1b));
}
} // namespace keys
