#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ezstemz
{

class MultitrackPlayer;

/**
    Bottom transport bar: play/pause, stop, master volume, scrub slider, time readout.
*/
class TransportBar  : public juce::Component
{
public:
    explicit TransportBar (MultitrackPlayer& player);

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Refresh the playhead/length displays. */
    void update (double currentSeconds, double lengthSeconds);

private:
    MultitrackPlayer& player;

    /** Slider that snaps to a target value when the user drags within
        a small tolerance window. */
    class SnappingSlider  : public juce::Slider
    {
    public:
        double snapTarget    = 1.0;
        double snapTolerance = 0.04;
        double snapValue (double attemptedValue, DragMode dragMode) override;
    };

    juce::TextButton playPauseButton { "Play" };
    juce::TextButton stopButton      { "Stop" };
    juce::Slider     scrubSlider;
    juce::Slider     masterSlider;
    SnappingSlider   rateSlider;
    juce::Label      rateLabel;
    juce::Label      timeLabel;

    bool userScrubbing = false;
};

} // namespace ezstemz
