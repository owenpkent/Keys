#include "FaderBank.h"
#include "CCMenu.h"
#include <okstudio/MouseOnly.h>

namespace keys
{
FaderBank::FaderBank(KeysProcessor& p) : processor(p)
{
    okstudio::ui::makeMouseOnly(*this);

    for (int i = 0; i < numFaders; ++i)
    {
        auto& fader = faders[(size_t) i];
        fader = std::make_unique<juce::Slider>();
        fader->setSliderStyle(juce::Slider::LinearVertical);
        fader->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 44, 18);
        fader->setRange(0, 127, 1);
        fader->setValue(64, juce::dontSendNotification); // Octavium starts centred; silent until touched
        // Relative drag, no click-jump: Octavium's sliders deliberately never leap to
        // the click point, so a stray click can't slam a CC to an extreme.
        fader->setSliderSnapsToMousePosition(false);
        fader->onValueChange = [this, i]
        {
            const float value = (float) faders[(size_t) i]->getValue();
            processor.sendCC(assignedCC(i), (int) value);
            // Also drives a hosted instrument's parameter directly, if Keys Host has
            // bound this fader to one; a no-op on plain Keys.
            processor.faderMoved(i, value / 127.0f);
        };
        addAndMakeVisible(*fader);

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

int FaderBank::assignedCC(int fader) const
{
    return (int) processor.apvts.getRawParameterValue("faderCC" + juce::String(fader + 1))->load();
}

void FaderBank::refreshAssignments()
{
    for (int i = 0; i < numFaders; ++i)
    {
        // A hosted instrument's bound parameter name takes over the label when
        // present (Keys Host auto-assignment); otherwise fall back to the CC label.
        const auto target = processor.faderTargetName(i);
        const auto text = target.isNotEmpty() ? target : cc::label(assignedCC(i));
        if (ccButtons[(size_t) i]->getButtonText() != text)
            ccButtons[(size_t) i]->setButtonText(text);
    }
}

void FaderBank::resized()
{
    auto area = getLocalBounds().reduced(10, 8);
    const int w = area.getWidth() / numFaders;
    for (int i = 0; i < numFaders; ++i)
    {
        auto col = area.removeFromLeft(w).reduced(6, 0);
        ccButtons[(size_t) i]->setBounds(col.removeFromBottom(34));
        col.removeFromBottom(4);
        faders[(size_t) i]->setBounds(col);
    }
}

void FaderBank::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b1b1b));
}
} // namespace keys
