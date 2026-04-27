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
    playPauseButton.onClick = [this]
    {
        if (player.isPlaying()) { player.pause(); playPauseButton.setButtonText ("Play"); }
        else                    { player.play();  playPauseButton.setButtonText ("Pause"); }
    };
    addAndMakeVisible (playPauseButton);

    stopButton.onClick = [this]
    {
        player.stop();
        playPauseButton.setButtonText ("Play");
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

    rateLabel.setText ("Rate", juce::dontSendNotification);
    rateLabel.setJustificationType (juce::Justification::centredRight);
    rateLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    rateLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFCBD5E1));
    addAndMakeVisible (rateLabel);

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

    timeLabel.setText ("00:00.00 / 00:00.00", juce::dontSendNotification);
    timeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (timeLabel);
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
    playPauseButton.setBounds (r.removeFromLeft (80));
    r.removeFromLeft (4);
    stopButton.setBounds (r.removeFromLeft (60));
    r.removeFromLeft (8);

    // Right-hand cluster: time | master gain | rate (laid out right-to-left)
    auto right = r.removeFromRight (520);

    timeLabel.setBounds (right.removeFromRight (140));
    right.removeFromRight (8);

    masterSlider.setBounds (right.removeFromRight (140));
    right.removeFromRight (8);

    rateSlider.setBounds (right.removeFromRight (170));
    right.removeFromRight (4);
    rateLabel.setBounds (right.removeFromRight (40));

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

    playPauseButton.setButtonText (player.isPlaying() ? "Pause" : "Play");
}

} // namespace ezstemz
