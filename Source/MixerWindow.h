#pragma once

#include "MultitrackPlayer.h"

namespace ezstemz
{

/** Non-modal mixer with per-stem faders, master fader, and optional VST3/AU inserts. */
class MixerWindow  : public juce::DocumentWindow
{
public:
    explicit MixerWindow (MultitrackPlayer& player);
    ~MixerWindow() override = default;

    void closeButtonPressed() override;

private:
    MultitrackPlayer& player;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerWindow)
};

} // namespace ezstemz
