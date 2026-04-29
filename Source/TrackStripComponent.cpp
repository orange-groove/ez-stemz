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

    auto stemColour = colourForStem (info.name);

    nameLabel.setText (info.name, juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFE5E7EB));
    nameLabel.setFont (juce::Font (juce::FontOptions (12.5f).withStyle ("Bold")));
    nameLabel.setMinimumHorizontalScale (1.0f);
    nameLabel.setBorderSize ({ 0, 2, 0, 0 });
    addAndMakeVisible (nameLabel);

    auto styleSquareBtn = [] (juce::TextButton& b, juce::Colour onCol)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, onCol);
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF111827));
        b.setColour (juce::TextButton::textColourOnId,  juce::Colour (0xFF0B0F19));
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xFF94A3B8));
    };

    styleSquareBtn (muteButton, juce::Colour (0xFFEF4444));
    muteButton.setTooltip ("Mute");
    muteButton.onClick = [this] { player.setTrackMuted (trackIndex, muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    styleSquareBtn (soloButton, juce::Colour (0xFFFACC15));
    soloButton.setTooltip ("Solo");
    soloButton.onClick = [this] { player.setTrackSoloed (trackIndex, soloButton.getToggleState()); };
    addAndMakeVisible (soloButton);

    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    gainSlider.setPopupDisplayEnabled (true, true, this);
    gainSlider.setRange (0.0, 1.5, 0.01);
    gainSlider.setValue (player.getTrackGain (trackIndex), juce::dontSendNotification);
    gainSlider.setColour (juce::Slider::trackColourId,     stemColour.withAlpha (0.65f));
    gainSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF111827));
    gainSlider.setColour (juce::Slider::thumbColourId,     juce::Colour (0xFFE5E7EB));
    gainSlider.setTooltip ("Gain");
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
    auto controls = getLocalBounds().withWidth (kControlsLeftWidth);
    controls.removeFromLeft (kStripeW);
    controls.reduce (4, 6);

    auto row1 = controls.removeFromTop (22);

    auto centredSquare = [] (juce::Rectangle<int> col, int size)
    {
        return juce::Rectangle<int> (0, 0, size, size).withCentre (col.getCentre());
    };

    soloButton.setBounds (centredSquare (row1.removeFromRight (kBtnW), kBtnW));
    row1.removeFromRight (2);
    muteButton.setBounds (centredSquare (row1.removeFromRight (kBtnW), kBtnW));
    row1.removeFromRight (4);
    nameLabel.setBounds (row1);

    controls.removeFromTop (6);

    auto gainRow = controls.removeFromTop (20);
    gainSlider.setBounds (gainRow);
}

juce::Rectangle<int> TrackStripComponent::getWaveformBounds() const
{
    auto waveform = getLocalBounds();
    waveform.removeFromLeft (kControlsLeftWidth);
    return waveform.reduced (0, 4);
}

int TrackStripComponent::getWaveformLeftX() const noexcept
{
    return getWaveformBounds().getX();
}

void TrackStripComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF111827));

    auto controls = getLocalBounds().withWidth (kControlsLeftWidth);
    g.setColour (juce::Colour (0xFF1F2937));
    g.fillRect (controls);

    auto stripe = controls.withWidth (kStripeW);
    g.setColour (colourForStem (player.getTrackInfo (trackIndex).name));
    g.fillRect (stripe);

    g.setColour (juce::Colour (0xFF0B0F19));
    g.fillRect (juce::Rectangle<int> (kControlsLeftWidth, 0, 1, getHeight()));

    g.setColour (juce::Colour (0xFF374151));
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
        g.drawLine ((float) x, 0.0f,
                    (float) x, (float) getHeight(), 1.5f);
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
