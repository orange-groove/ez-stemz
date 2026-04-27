#include "TrackStripComponent.h"

#include "MultitrackPlayer.h"

namespace ezstemz
{

namespace
{
juce::Colour colourForStem (const juce::String& name)
{
    auto lc = name.toLowerCase();
    if (lc.contains ("vocal"))  return juce::Colour (0xFF22C55E);
    if (lc.contains ("drum"))   return juce::Colour (0xFFEF4444);
    if (lc.contains ("bass"))   return juce::Colour (0xFF8B5CF6);
    if (lc.contains ("guitar")) return juce::Colour (0xFFF59E0B);
    if (lc.contains ("piano"))  return juce::Colour (0xFF38BDF8);
    return juce::Colour (0xFF94A3B8);
}
} // namespace

TrackStripComponent::TrackStripComponent (MultitrackPlayer& p,
                                          int               idx,
                                          juce::AudioFormatManager& fmgr,
                                          juce::AudioThumbnailCache& cache)
    : player (p),
      trackIndex (idx),
      thumbnail (256, fmgr, cache)
{
    auto& info = player.getTrackInfo (trackIndex);

    nameLabel.setText (info.name, juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, colourForStem (info.name));
    nameLabel.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    addAndMakeVisible (nameLabel);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFEF4444));
    muteButton.onClick = [this] { player.setTrackMuted (trackIndex, muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFFACC15));
    soloButton.onClick = [this] { player.setTrackSoloed (trackIndex, soloButton.getToggleState()); };
    addAndMakeVisible (soloButton);

    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);
    gainSlider.setRange (0.0, 1.5, 0.01);
    gainSlider.setValue (player.getTrackGain (trackIndex), juce::dontSendNotification);
    gainSlider.onValueChange = [this] { player.setTrackGain (trackIndex, (float) gainSlider.getValue()); };
    addAndMakeVisible (gainSlider);

    thumbnail.addChangeListener (this);
    thumbnail.setSource (new juce::FileInputSource (info.file));
}

TrackStripComponent::~TrackStripComponent()
{
    thumbnail.removeChangeListener (this);
}

void TrackStripComponent::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    // The thumbnail scans the audio file off the message thread; when
    // more data is available it broadcasts here. Repaint so the waveform
    // grows in instead of being stuck as a tiny slither at the start.
    if (src == &thumbnail)
        repaint();
}

void TrackStripComponent::resized()
{
    auto r = getLocalBounds().reduced (4);
    nameLabel.setBounds (r.removeFromLeft (110));

    auto squareIn = [] (juce::Rectangle<int> col, int size)
    {
        return juce::Rectangle<int> (0, 0, size, size).withCentre (col.getCentre());
    };

    constexpr int kBtnSize = 30; // 6px of breathing room inside the 36px column
    muteButton.setBounds (squareIn (r.removeFromLeft (36), kBtnSize));
    soloButton.setBounds (squareIn (r.removeFromLeft (36), kBtnSize));
    gainSlider.setBounds (r.removeFromLeft (180));
    // Remaining space is the waveform.
}

juce::Rectangle<int> TrackStripComponent::getWaveformBounds() const
{
    auto waveform = getLocalBounds().reduced (4);
    waveform.removeFromLeft (110 + 36 + 36 + 180 + 8);
    return waveform;
}

int TrackStripComponent::getWaveformLeftX() const noexcept
{
    return getWaveformBounds().getX();
}

void TrackStripComponent::paint (juce::Graphics& g)
{
    auto bg = juce::Colour (0xFF1F2937);
    g.fillAll (bg);

    g.setColour (juce::Colour (0xFF111827));
    g.fillRect (getLocalBounds().removeFromBottom (1));

    auto waveform = getWaveformBounds();
    if (waveform.getWidth() <= 0)
        return;

    auto colour = colourForStem (player.getTrackInfo (trackIndex).name);
    g.setColour (colour.withAlpha (0.85f));

    const double trackLength = juce::jmax (0.001,
                                  player.getTrackInfo (trackIndex).lengthSeconds);

    // Compute the time range visible inside the waveform area.
    double pps    = pixelsPerSecond > 0.0 ? pixelsPerSecond
                                          : (double) waveform.getWidth() / trackLength;
    double startSec = scrollSeconds;
    double endSec   = startSec + (double) waveform.getWidth() / pps;

    // Clamp the drawn range to [0, trackLength]; the rest of the waveform
    // area is filled with silence by drawChannels (looks fine).
    const double drawStart = juce::jlimit (0.0, trackLength, startSec);
    const double drawEnd   = juce::jlimit (0.0, trackLength, endSec);

    if (drawEnd > drawStart)
    {
        const int leftPx  = (int) std::round ((drawStart - startSec) * pps);
        const int rightPx = (int) std::round ((drawEnd   - startSec) * pps);
        const auto drawRect = juce::Rectangle<int> (
            waveform.getX() + leftPx,
            waveform.getY(),
            juce::jmax (1, rightPx - leftPx),
            waveform.getHeight());
        thumbnail.drawChannels (g, drawRect, drawStart, drawEnd, 1.0f);
    }

    if (playhead >= startSec && playhead <= endSec)
    {
        const int x = waveform.getX()
                    + (int) std::round ((playhead - startSec) * pps);
        g.setColour (juce::Colour (0xFFFFFFFF));
        g.drawLine ((float) x, (float) waveform.getY(),
                    (float) x, (float) waveform.getBottom(), 1.5f);
    }
}

void TrackStripComponent::seekFromMouseX (int x)
{
    auto waveform = getWaveformBounds();
    if (waveform.getWidth() <= 0)
        return;

    const auto length = player.getLengthSeconds();
    if (length <= 0.0)
        return;

    const double pps = pixelsPerSecond > 0.0
                            ? pixelsPerSecond
                            : (double) waveform.getWidth() / length;

    const double t = scrollSeconds + (double) (x - waveform.getX()) / pps;
    player.setPositionSeconds (juce::jlimit (0.0, length, t));
}

void TrackStripComponent::mouseDown (const juce::MouseEvent& e)
{
    if (getWaveformBounds().contains (e.getPosition()))
        seekFromMouseX (e.x);
}

void TrackStripComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getMouseDownPosition().getX() >= getWaveformBounds().getX())
        seekFromMouseX (e.x);
}

void TrackStripComponent::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (getWaveformBounds().contains (e.getPosition())
                        ? juce::MouseCursor::PointingHandCursor
                        : juce::MouseCursor::NormalCursor);
}

void TrackStripComponent::mouseWheelMove (const juce::MouseEvent& e,
                                          const juce::MouseWheelDetails& wheel)
{
    if (onWheel != nullptr)
        onWheel (e, wheel);
}

void TrackStripComponent::setPlayheadSeconds (double s)
{
    if (! juce::approximatelyEqual (s, playhead))
    {
        playhead = s;
        repaint();
    }
}

void TrackStripComponent::setView (double pps, double scroll)
{
    if (! juce::approximatelyEqual (pps, pixelsPerSecond)
        || ! juce::approximatelyEqual (scroll, scrollSeconds))
    {
        pixelsPerSecond = pps;
        scrollSeconds   = scroll;
        repaint();
    }
}

} // namespace ezstemz
