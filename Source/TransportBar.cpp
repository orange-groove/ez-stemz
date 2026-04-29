#include "TransportBar.h"

#include "MultitrackPlayer.h"

namespace ezstemz
{

namespace
{
juce::String formatTime (double seconds)
{
    if (seconds < 0.0 || std::isnan (seconds))
        seconds = 0.0;
    auto totalMillis = (juce::int64) (seconds * 1000);
    auto mins  = (totalMillis / 60000);
    auto secs  = (totalMillis / 1000) % 60;
    auto cents = (totalMillis / 10) % 100;
    return juce::String::formatted ("%02lld:%02lld.%02lld",
                                    (long long) mins, (long long) secs, (long long) cents);
}
} // namespace

TransportBar::TransportBar (MultitrackPlayer& p) : player (p)
{
    playPauseButton.setTooltip ("Play / Pause");
    playPauseButton.onClick = [this]
    {
        if (player.isPlaying())
        {
            player.pause();
            playPauseButton.setShape (IconButton::Shape::Play);
        }
        else
        {
            player.play();
            playPauseButton.setShape (IconButton::Shape::Pause);
        }
    };
    addAndMakeVisible (playPauseButton);

    stopButton.setTooltip ("Stop");
    stopButton.onClick = [this]
    {
        player.stop();
        playPauseButton.setShape (IconButton::Shape::Play);
        scrubSlider.setValue (0.0, juce::dontSendNotification);
    };
    addAndMakeVisible (stopButton);

    scrubSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    scrubSlider.setRange (0.0, 1.0, 0.0001);
    scrubSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    scrubSlider.onDragStart = [this] { userScrubbing = true; };
    scrubSlider.onDragEnd   = [this]
    {
        userScrubbing = false;
        const auto length = player.getLengthSeconds();
        player.setPositionSeconds (scrubSlider.getValue() * length);
    };
    addAndMakeVisible (scrubSlider);

    masterSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    masterSlider.setRange (0.0, 1.5, 0.01);
    masterSlider.setValue (1.0, juce::dontSendNotification);
    masterSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 16);
    masterSlider.onValueChange = [this] { player.setMasterGain ((float) masterSlider.getValue()); };
    addAndMakeVisible (masterSlider);

    auto styleHeaderLabel = [] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
        l.setColour (juce::Label::textColourId, juce::Colour (0xFF94A3B8));
        l.setBorderSize ({ 0, 0, 0, 0 });
    };
    styleHeaderLabel (rateHeader,   "RATE");
    styleHeaderLabel (masterHeader, "MASTER VOLUME");
    styleHeaderLabel (timeHeader,   "TIME");
    addAndMakeVisible (rateHeader);
    addAndMakeVisible (masterHeader);
    addAndMakeVisible (timeHeader);

    rateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    rateSlider.setRange (0.5, 1.5, 0.01);
    rateSlider.setValue (1.0, juce::dontSendNotification);
    rateSlider.setDoubleClickReturnValue (true, 1.0);
    rateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
    rateSlider.textFromValueFunction = [] (double v)
    {
        return juce::String (juce::roundToInt (v * 100.0)) + "%";
    };
    rateSlider.valueFromTextFunction = [] (const juce::String& t)
    {
        return t.upToFirstOccurrenceOf ("%", false, false).getDoubleValue() / 100.0;
    };
    rateSlider.onValueChange = [this] { player.setPlaybackRate ((float) rateSlider.getValue()); };
    rateSlider.updateText();
    addAndMakeVisible (rateSlider);

    pitchLockButton.setClickingTogglesState (true);
    pitchLockButton.setToggleState (player.getPreservePitch(), juce::dontSendNotification);
    pitchLockButton.setTooltip ("When on, changing the rate keeps the pitch (time-stretch). "
                                "When off, changing the rate also shifts pitch (vinyl mode).");
    pitchLockButton.setColour (juce::TextButton::buttonOnColourId,  juce::Colour (0xFF6366F1));
    pitchLockButton.setColour (juce::TextButton::buttonColourId,    juce::Colour (0xFF1F2937));
    pitchLockButton.setColour (juce::TextButton::textColourOnId,    juce::Colours::white);
    pitchLockButton.setColour (juce::TextButton::textColourOffId,   juce::Colour (0xFFCBD5E1));
    pitchLockButton.onClick = [this]
    {
        player.setPreservePitch (pitchLockButton.getToggleState());
    };
    addAndMakeVisible (pitchLockButton);

    timeLabel.setText ("00:00.00 / 00:00.00", juce::dontSendNotification);
    timeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (timeLabel);
}

void TransportBar::IconButton::paintButton (juce::Graphics& g,
                                            bool isHighlighted,
                                            bool isDown)
{
    auto bg = juce::Colour (0xFF1F2937);
    if      (isDown)        bg = juce::Colour (0xFF111827);
    else if (isHighlighted) bg = juce::Colour (0xFF293548);

    auto bounds = getLocalBounds().toFloat();
    const float corner = 6.0f;
    g.setColour (bg);
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (juce::Colour (0xFF334155));
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

    g.setColour (juce::Colour (0xFFE5E7EB));

    auto centre = bounds.getCentre();
    const float iconSize = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.42f;

    switch (shape)
    {
        case Shape::Play:
        {
            // Right-pointing equilateral triangle, optical-centred (1/4 width
            // nudge so it doesn't look left-heavy).
            juce::Path tri;
            const float h = iconSize * 1.05f;
            const float w = h * 0.95f;
            tri.addTriangle (-w * 0.45f, -h * 0.5f,
                             -w * 0.45f,  h * 0.5f,
                              w * 0.55f,  0.0f);
            tri.applyTransform (juce::AffineTransform::translation (centre.x, centre.y));
            g.fillPath (tri);
            break;
        }
        case Shape::Pause:
        {
            const float barW   = iconSize * 0.34f;
            const float barH   = iconSize * 1.1f;
            const float gapHalf = iconSize * 0.22f;
            g.fillRoundedRectangle (centre.x - gapHalf - barW,
                                    centre.y - barH * 0.5f,
                                    barW, barH, 1.5f);
            g.fillRoundedRectangle (centre.x + gapHalf,
                                    centre.y - barH * 0.5f,
                                    barW, barH, 1.5f);
            break;
        }
        case Shape::Stop:
        {
            const float side = iconSize * 1.0f;
            g.fillRoundedRectangle (centre.x - side * 0.5f,
                                    centre.y - side * 0.5f,
                                    side, side, 2.0f);
            break;
        }
    }
}

double TransportBar::SnappingSlider::snapValue (double attemptedValue, DragMode dragMode)
{
    juce::ignoreUnused (dragMode);
    if (std::abs (attemptedValue - snapTarget) < snapTolerance)
        return snapTarget;
    return attemptedValue;
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    constexpr int kIconBtn = 34;
    playPauseButton.setBounds (r.removeFromLeft (kIconBtn).withSizeKeepingCentre (kIconBtn, kIconBtn));
    r.removeFromLeft (6);
    stopButton.setBounds (r.removeFromLeft (kIconBtn).withSizeKeepingCentre (kIconBtn, kIconBtn));
    r.removeFromLeft (10);

    // Right-hand cluster: each labelled column stacks a small header label
    // on top of the actual control. Pitch lock has no header.
    auto right = r.removeFromRight (620);

    auto withLabel = [] (juce::Rectangle<int> col,
                          juce::Label& header,
                          juce::Component& body)
    {
        header.setBounds (col.removeFromTop (12));
        col.removeFromTop (1);
        body.setBounds (col);
    };

    auto timeCol = right.removeFromRight (160);
    withLabel (timeCol, timeHeader, timeLabel);
    right.removeFromRight (10);

    auto masterCol = right.removeFromRight (160);
    withLabel (masterCol, masterHeader, masterSlider);
    right.removeFromRight (10);

    auto rateCol = right.removeFromRight (180);
    withLabel (rateCol, rateHeader, rateSlider);
    right.removeFromRight (6);

    pitchLockButton.setBounds (right.removeFromRight (60)
                                    .withSizeKeepingCentre (60, 26));

    r.removeFromRight (8);
    scrubSlider.setBounds (r);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF0F172A));
    g.setColour (juce::Colour (0xFF1E293B));
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void TransportBar::update (double currentSeconds, double lengthSeconds)
{
    timeLabel.setText (formatTime (currentSeconds) + " / " + formatTime (lengthSeconds),
                       juce::dontSendNotification);

    if (! userScrubbing && lengthSeconds > 0.0)
        scrubSlider.setValue (currentSeconds / lengthSeconds, juce::dontSendNotification);

    playPauseButton.setShape (player.isPlaying() ? IconButton::Shape::Pause
                                                 : IconButton::Shape::Play);
}

} // namespace ezstemz
