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

    /** Tiny vector-icon button used for transport (play / pause / stop). */
    class IconButton  : public juce::Button
    {
    public:
        enum class Shape { Play, Pause, Stop };

        explicit IconButton (Shape s) : juce::Button ({}), shape (s) {}

        void setShape (Shape s) { shape = s; repaint(); }
        Shape getShape() const noexcept { return shape; }

    private:
        void paintButton (juce::Graphics&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

        Shape shape;
    };

    IconButton playPauseButton { IconButton::Shape::Play };
    IconButton stopButton      { IconButton::Shape::Stop };
    juce::Slider     scrubSlider;
    juce::Slider     masterSlider;
    SnappingSlider   rateSlider;
    juce::TextButton pitchLockButton  { "Pitch" };
    juce::Label      timeLabel;

    juce::Label rateHeader;
    juce::Label masterHeader;
    juce::Label timeHeader;

    bool userScrubbing = false;
};

} // namespace ezstemz
