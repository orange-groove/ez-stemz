#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace ezstemz
{

class MultitrackPlayer;

/**
    One row of the multitrack mixer: name, mute/solo buttons, gain slider,
    and a horizontally-zoomable thumbnail waveform with a moving playhead.

    Zoom + horizontal scroll are driven externally — the parent
    SongProcessorComponent owns one shared `pixelsPerSecond` and
    `scrollSeconds` so every strip stays in lock-step.
*/
class TrackStripComponent  : public juce::Component,
                             private juce::ChangeListener
{
public:
    TrackStripComponent (MultitrackPlayer& player,
                         int trackIndex,
                         juce::AudioFormatManager& fmgr,
                         juce::AudioThumbnailCache& cache);
    ~TrackStripComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& wheel) override;

    /** Mouse-wheel events on the waveform are forwarded to the parent so it
        can pan / zoom the shared view. The MouseEvent's x is in strip-local
        coordinates; the parent can translate to time via getWaveformLeftX(). */
    std::function<void (const juce::MouseEvent&,
                        const juce::MouseWheelDetails&)> onWheel;

    /** X coordinate (within the strip) where the waveform region starts. */
    int getWaveformLeftX() const noexcept;

    void setPlayheadSeconds (double s);

    /** Updates the shared horizontal view. `pixelsPerSecond` is how wide
        one second of audio appears (so larger = more zoomed in).
        `scrollSeconds` is the time at the left edge of the waveform. */
    void setView (double pixelsPerSecond, double scrollSeconds);

    /** Width (in pixels) of the fixed control panel on the left side of
        each strip. Used by the parent to align the horizontal scrollbar
        with the waveform region. Layout is two rows:
          row 1 (top):    [stripe][name fills] [M] [S]
          row 2 (bottom): [stripe][gain slider fills full width] */
    static constexpr int kStripeW          = 5;
    static constexpr int kBtnW             = 22;
    static constexpr int kControlsLeftWidth = 160;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::Rectangle<int> getWaveformBounds() const;
    void seekFromMouseX (int x);

    MultitrackPlayer& player;
    int               trackIndex;
    juce::AudioThumbnail thumbnail;

    juce::Label  nameLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    juce::Slider gainSlider;

    double playhead         = 0.0;
    double pixelsPerSecond  = 0.0;  // 0 means "not initialised yet"
    double scrollSeconds    = 0.0;
};

} // namespace ezstemz
