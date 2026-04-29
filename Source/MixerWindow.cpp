#include "MixerWindow.h"

#include "AppPluginRegistry.h"
#include "PluginPickerWindow.h"

#include <cmath>
#include <functional>

#include <juce_gui_extra/juce_gui_extra.h>

namespace ezstemz
{
namespace
{

float linearGainToVerticalY (juce::Slider&       slider,
                             double              linearGain,
                             float               yTop,
                             float               yBottom)
{
    double p = slider.valueToProportionOfLength (linearGain);
    p        = 1.0 - p;
    return yTop + (float) (p * (double) (yBottom - yTop));
}

juce::String tickDbLabel (int db)
{
    if (db > 0)
        return "+" + juce::String (db);
    return juce::String (db);
}

juce::String formatLinearGainAsDb (double linear)
{
    if (linear <= 1.0e-9)
        return "-inf dB";

    const double db = 20.0 * std::log10 (linear);
    return juce::String (db, 1) + juce::String (" dB");
}

/** Post-fader peak column (linear in -> bar height / colour in dB). */
class MixerPeakMeterBar final : public juce::Component
{
public:
    void setLevel (float linear)
    {
        if (level != linear)
        {
            level = linear;
            repaint();
        }
    }

private:
    float level = 0.f;

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xFF020617));
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (juce::Colour (0xFF334155));
        g.drawRoundedRectangle (bounds, 2.0f, 1.0f);

        constexpr float dbFloor = -54.0f;
        constexpr float dbCeil  = 6.0f;

        if (level <= 1.0e-12f)
            return;

        const float db = juce::jlimit (dbFloor, dbCeil, 20.0f * std::log10 (level));
        const float t  = (db - dbFloor) / (dbCeil - dbFloor);
        const float h  = bounds.getHeight() * t;
        auto        bar  = bounds.withTop (bounds.getBottom() - h);

        juce::Colour fill (0xFF22C55E);

        if (db >= -3.0f)
            fill = juce::Colour (0xFFEF4444);
        else if (db >= -12.0f)
            fill = juce::Colour (0xFFEAB308);

        g.setColour (fill);
        g.fillRoundedRectangle (bar, 1.5f);
    }
};

/** Vertical gain faders: rail, dB ticks, tall rectangular cap. */
class MixerFaderLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPos,
                           float minSliderPos,
                           float maxSliderPos,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g,
                                              x,
                                              y,
                                              width,
                                              height,
                                              sliderPos,
                                              minSliderPos,
                                              maxSliderPos,
                                              style,
                                              slider);
            return;
        }

        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const float tickColW   = juce::jlimit (22.0f, 34.0f, bounds.getWidth() * 0.42f);
        const auto  tickColumn = bounds.removeFromLeft (tickColW);
        const auto  faderArea  = bounds;
        const float  railW       = juce::jlimit (7.0f, 11.0f, faderArea.getWidth() * 0.42f);
        auto         rail        = faderArea.withSizeKeepingCentre (railW, faderArea.getHeight());

        const float yTop    = juce::jmin (minSliderPos, maxSliderPos);
        const float yBottom = juce::jmax (minSliderPos, maxSliderPos);

        const auto railBg = slider.findColour (juce::Slider::backgroundColourId);
        const auto tickColour = juce::Colour (0xFF64748B);

        static constexpr int tickDbs[] = { 6, 3, 0, -3, -6, -12, -18, -24, -30, -36, -42, -48, -54 };

        g.setFont (juce::Font (juce::FontOptions (9.5f)));

        for (int db : tickDbs)
        {
            const double lin = std::pow (10.0, (double) db / 20.0);
            if (lin < slider.getMinimum() - 1.0e-6 || lin > slider.getMaximum() + 1.0e-6)
                continue;

            const float yy       = linearGainToVerticalY (slider, lin, yTop, yBottom);
            const float tickLong = juce::jmin (7.0f, tickColumn.getWidth() * 0.35f);

            g.setColour (tickColour);
            g.drawLine (tickColumn.getRight() - tickLong,
                        yy,
                        tickColumn.getRight(),
                        yy,
                        1.0f);

            g.setColour (tickColour.brighter (0.25f));
            const float labelH = 11.0f;
            auto        labelR = juce::Rectangle<float> (tickColumn.getX(),
                                                   yy - labelH * 0.5f,
                                                   juce::jmax (1.0f, tickColumn.getWidth() - tickLong - 2.0f),
                                                   labelH);
            g.drawText (tickDbLabel (db),
                        labelR.toNearestInt(),
                        juce::Justification::centredRight,
                        true);
        }

        // Full-height rail (slot)
        g.setColour (railBg.brighter (0.12f));
        g.fillRoundedRectangle (rail, 3.0f);
        g.setColour (railBg.darker (0.25f));
        g.drawRoundedRectangle (rail.reduced (0.5f), 3.0f, 1.0f);

        g.setColour (railBg.darker (0.15f));
        g.fillRoundedRectangle (rail.reduced (2.0f, 1.0f), 2.0f);

        const float yFillTop    = juce::jmin (sliderPos, yBottom);
        const float yFillBottom = juce::jmax (sliderPos, yBottom);
        auto        level       = juce::Rectangle<float>::leftTopRightBottom (rail.getX(),
                                                                         yFillTop,
                                                                         rail.getRight(),
                                                                         yFillBottom)
                              .reduced (2.0f, 0.0f);

        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (level, 2.0f);

        const float thumbH = 24.0f;
        const float thumbW = juce::jmin (faderArea.getWidth() - 2.0f, railW + 10.0f);
        auto        thumb  = juce::Rectangle<float> (thumbW, thumbH)
                          .withCentre ({ faderArea.getCentreX(), sliderPos });

        g.setColour (slider.findColour (juce::Slider::thumbColourId));
        g.fillRoundedRectangle (thumb, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawRoundedRectangle (thumb, 2.0f, 1.0f);
        g.setColour (railBg.darker (0.35f).withAlpha (0.45f));
        g.drawLine (thumb.getX() + 3.0f, thumb.getCentreY(), thumb.getRight() - 3.0f, thumb.getCentreY(), 1.0f);
    }

    int getSliderThumbRadius (juce::Slider& slider) override
    {
        if (slider.getSliderStyle() == juce::Slider::LinearVertical)
            return 20;
        return LookAndFeel_V4::getSliderThumbRadius (slider);
    }
};

MixerFaderLookAndFeel& getMixerFaderLookAndFeel()
{
    static MixerFaderLookAndFeel instance;
    return instance;
}

juce::String shortPluginName (const juce::String& s)
{
    if (s.length() <= 18)
        return s;
    return s.substring (0, 16) + "...";
}

} // namespace

class MixerPanel;

//==============================================================================
class PluginEditorHost  : public juce::DocumentWindow
{
public:
    PluginEditorHost (juce::AudioProcessor& procIn, std::function<void()> onClosedIn)
        : DocumentWindow (procIn.getName() + " - EZStemz",
                          juce::Colour (0xff0b1220),
                          DocumentWindow::closeButton,
                          false)
        , proc (procIn)
        , onClosed (std::move (onClosedIn))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);

        if (auto* ed = proc.createEditorIfNeeded())
        {
            setContentNonOwned (ed, true);
            setResizeLimits (ed->getWidth(), ed->getHeight(), 2000, 2000);
            setSize (ed->getWidth() + 12, ed->getHeight() + 28);
        }
        else
        {
            setSize (360, 120);
        }
    }

    ~PluginEditorHost() override { detachEditor(); }

    void closeButtonPressed() override
    {
        detachEditor();

        auto cb = std::move (onClosed);
        juce::MessageManager::callAsync ([cb]
                                         {
                                             if (cb)
                                                 cb();
                                         });
    }

private:
    void detachEditor()
    {
        if (auto* c = getContentComponent())
            if (auto* ed = dynamic_cast<juce::AudioProcessorEditor*> (c))
                proc.editorBeingDeleted (ed);

        setContentNonOwned (nullptr, false);
    }

    juce::AudioProcessor& proc;
    std::function<void()> onClosed;
};

//==============================================================================
class MixerChannelStrip  : public juce::Component
{
public:
    MixerChannelStrip (MixerPanel& panelIn, MultitrackPlayer& p, int trackIndexIn);
    ~MixerChannelStrip() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    void refreshSlots();
    void layoutSlots();
    void syncGainFromPlayer();
    void updateGainDbLabel();

private:
    MixerPanel&        panel;
    MultitrackPlayer& player;
    int                trackIndex;

    juce::Label        nameLabel;
    juce::Label        gainDbLabel;
    juce::Slider       gainSlider;
    MixerPeakMeterBar  peakMeter;
    float              meterBallistic = 0.f;
    juce::TextButton   addButton;
    juce::Viewport     slotsViewport;
    juce::Component    slotsContainer;
    juce::OwnedArray<juce::TextButton> slotButtons;
};

class MixerMasterStrip  : public juce::Component
{
public:
    MixerMasterStrip (MixerPanel& panelIn, MultitrackPlayer& p);
    ~MixerMasterStrip() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    void refreshSlots();
    void layoutSlots();
    void syncGainFromPlayer();
    void updateGainDbLabel();

private:
    MixerPanel&        panel;
    MultitrackPlayer& player;

    juce::Label        title;
    juce::Label        gainDbLabel;
    juce::Slider       gainSlider;
    MixerPeakMeterBar  peakMeter;
    float              meterBallistic = 0.f;
    juce::TextButton   addButton;
    juce::Viewport     slotsViewport;
    juce::Component    slotsContainer;
    juce::OwnedArray<juce::TextButton> slotButtons;
};

//==============================================================================
class MixerPanel  : public juce::Component,
                    private juce::Timer
{
public:
    explicit MixerPanel (MultitrackPlayer& p);
    ~MixerPanel() override;

    void resized() override;

    void chooseAndAddTrackPlugin (int trackIndex);
    void chooseAndAddMasterPlugin();
    void removeTrackPlugin (int trackIndex, int insertIndex);
    void removeMasterPlugin (int insertIndex);
    void openPluginEditor (juce::AudioProcessor& proc);
    void dismissPluginEditor();

    void refreshAllStrips();

private:
    void timerCallback() override;

    MultitrackPlayer& player;

    juce::Viewport                      viewport;
    juce::Component                     content;
    juce::OwnedArray<MixerChannelStrip> channelStrips;
    std::unique_ptr<MixerMasterStrip>   masterStrip;

    std::unique_ptr<PluginPickerWindow> pluginPickerWindow;
    std::unique_ptr<PluginEditorHost>   pluginEditorWindow;
};

//==============================================================================
MixerChannelStrip::MixerChannelStrip (MixerPanel& panelIn, MultitrackPlayer& p, int trackIndexIn)
    : panel (panelIn)
    , player (p)
    , trackIndex (trackIndexIn)
{
    nameLabel.setText (player.getTrackInfo (trackIndex).name, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    nameLabel.setFont (juce::Font (juce::FontOptions (13.0f).withStyle ("Bold")));
    addAndMakeVisible (nameLabel);

    gainDbLabel.setJustificationType (juce::Justification::centred);
    gainDbLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFCBD5E1));
    gainDbLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (gainDbLabel);

    gainSlider.setSliderStyle (juce::Slider::LinearVertical);
    gainSlider.setRange (0.0, 1.5, 0.01);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setValue ((double) player.getTrackGain (trackIndex), juce::dontSendNotification);
    gainSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xFF6366F1));
    gainSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF111827));
    gainSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    gainSlider.setLookAndFeel (&getMixerFaderLookAndFeel());
    gainSlider.onValueChange = [this]
    {
        player.setTrackGain (trackIndex, (float) gainSlider.getValue());
        updateGainDbLabel();
    };
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (peakMeter);
    updateGainDbLabel();

    addButton.setButtonText ("+ FX");
    addButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1F2937));
    addButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    addButton.onClick = [this] { panel.chooseAndAddTrackPlugin (trackIndex); };
    addAndMakeVisible (addButton);

    slotsViewport.setViewedComponent (&slotsContainer, false);
    addAndMakeVisible (slotsViewport);

    refreshSlots();
}

MixerChannelStrip::~MixerChannelStrip()
{
    gainSlider.setLookAndFeel (nullptr);
}

void MixerChannelStrip::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF111827));
    g.setColour (juce::Colour (0xFF334155));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f, 1.0f);
}

void MixerChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (6);
    nameLabel.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);

    addButton.setBounds (r.removeFromBottom (28));
    r.removeFromBottom (4);

    slotsViewport.setBounds (r.removeFromBottom (juce::jmin (140, r.getHeight() / 3)));
    r.removeFromBottom (4);

    gainDbLabel.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (4);

    const int meterW = 12;
    auto      faderRow = r;
    peakMeter.setBounds (faderRow.removeFromRight (meterW));
    gainSlider.setBounds (faderRow);
    layoutSlots();
}

void MixerChannelStrip::updateGainDbLabel()
{
    gainDbLabel.setText (formatLinearGainAsDb (gainSlider.getValue()), juce::dontSendNotification);
}

void MixerChannelStrip::refreshSlots()
{
    slotsContainer.deleteAllChildren();
    slotButtons.clear();

    const int n = player.getNumTrackInserts (trackIndex);

    for (int i = 0; i < n; ++i)
    {
        auto* proc = player.getTrackInsert (trackIndex, i);
        if (proc == nullptr)
            continue;

        auto* b = new juce::TextButton (shortPluginName (proc->getName()));
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1E293B));
        b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        const int slot = i;
        b->onClick = [this, slot]
        { panel.openPluginEditor (*player.getTrackInsert (trackIndex, slot)); };
        b->addMouseListener (this, true);
        slotButtons.add (b);
        slotsContainer.addAndMakeVisible (b);
    }

    layoutSlots();
}

void MixerChannelStrip::layoutSlots()
{
    int y = 0;
    const int w = juce::jmax (40, slotsViewport.getWidth() - 8);

    for (auto* b : slotButtons)
    {
        b->setBounds (0, y, w, 24);
        y += 26;
    }

    slotsContainer.setSize (w, juce::jmax (y, 1));
    slotsViewport.setViewPosition (0, 0);
}

void MixerChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    for (int i = 0; i < slotButtons.size(); ++i)
        if (e.eventComponent == slotButtons[i])
        {
            juce::PopupMenu m;
            m.addItem ("Remove plugin", [this, i] { panel.removeTrackPlugin (trackIndex, i); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotButtons[i]));
            return;
        }
}

void MixerChannelStrip::syncGainFromPlayer()
{
    if (! gainSlider.isMouseButtonDown())
    {
        gainSlider.setValue ((double) player.getTrackGain (trackIndex), juce::dontSendNotification);
        updateGainDbLabel();
    }

    const float inst = player.getTrackPostFaderMeter (trackIndex);
    meterBallistic = juce::jmax (inst, meterBallistic * 0.88f);
    peakMeter.setLevel (meterBallistic);
}

//==============================================================================
MixerMasterStrip::MixerMasterStrip (MixerPanel& panelIn, MultitrackPlayer& p)
    : panel (panelIn)
    , player (p)
{
    title.setText ("MASTER", juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centred);
    title.setColour (juce::Label::textColourId, juce::Colour (0xFFCBD5E1));
    title.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    addAndMakeVisible (title);

    gainDbLabel.setJustificationType (juce::Justification::centred);
    gainDbLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFCBD5E1));
    gainDbLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (gainDbLabel);

    gainSlider.setSliderStyle (juce::Slider::LinearVertical);
    gainSlider.setRange (0.0, 1.5, 0.01);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setValue ((double) player.getMasterGain(), juce::dontSendNotification);
    gainSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xFF22C55E));
    gainSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xFF111827));
    gainSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    gainSlider.setLookAndFeel (&getMixerFaderLookAndFeel());
    gainSlider.onValueChange = [this]
    {
        player.setMasterGain ((float) gainSlider.getValue());
        updateGainDbLabel();
    };
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (peakMeter);
    updateGainDbLabel();

    addButton.setButtonText ("+ FX");
    addButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1F2937));
    addButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    addButton.onClick = [this] { panel.chooseAndAddMasterPlugin(); };
    addAndMakeVisible (addButton);

    slotsViewport.setViewedComponent (&slotsContainer, false);
    addAndMakeVisible (slotsViewport);

    refreshSlots();
}

MixerMasterStrip::~MixerMasterStrip()
{
    gainSlider.setLookAndFeel (nullptr);
}

void MixerMasterStrip::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF0F172A));
    g.setColour (juce::Colour (0xFF475569));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f, 1.5f);
}

void MixerMasterStrip::resized()
{
    auto r = getLocalBounds().reduced (6);
    title.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);

    addButton.setBounds (r.removeFromBottom (28));
    r.removeFromBottom (4);

    slotsViewport.setBounds (r.removeFromBottom (juce::jmin (140, r.getHeight() / 3)));
    r.removeFromBottom (4);

    gainDbLabel.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (4);

    const int meterW = 12;
    auto      faderRow = r;
    peakMeter.setBounds (faderRow.removeFromRight (meterW));
    gainSlider.setBounds (faderRow);
    layoutSlots();
}

void MixerMasterStrip::updateGainDbLabel()
{
    gainDbLabel.setText (formatLinearGainAsDb (gainSlider.getValue()), juce::dontSendNotification);
}

void MixerMasterStrip::refreshSlots()
{
    slotsContainer.deleteAllChildren();
    slotButtons.clear();

    const int n = player.getNumMasterInserts();

    for (int i = 0; i < n; ++i)
    {
        auto* proc = player.getMasterInsert (i);
        if (proc == nullptr)
            continue;

        auto* b = new juce::TextButton (shortPluginName (proc->getName()));
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1E293B));
        b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        const int slot = i;
        b->onClick = [this, slot] { panel.openPluginEditor (*player.getMasterInsert (slot)); };
        b->addMouseListener (this, true);
        slotButtons.add (b);
        slotsContainer.addAndMakeVisible (b);
    }

    layoutSlots();
}

void MixerMasterStrip::layoutSlots()
{
    int y = 0;
    const int w = juce::jmax (40, slotsViewport.getWidth() - 8);

    for (auto* b : slotButtons)
    {
        b->setBounds (0, y, w, 24);
        y += 26;
    }

    slotsContainer.setSize (w, juce::jmax (y, 1));
    slotsViewport.setViewPosition (0, 0);
}

void MixerMasterStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    for (int i = 0; i < slotButtons.size(); ++i)
        if (e.eventComponent == slotButtons[i])
        {
            juce::PopupMenu m;
            m.addItem ("Remove plugin", [this, i] { panel.removeMasterPlugin (i); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotButtons[i]));
            return;
        }
}

void MixerMasterStrip::syncGainFromPlayer()
{
    if (! gainSlider.isMouseButtonDown())
    {
        gainSlider.setValue ((double) player.getMasterGain(), juce::dontSendNotification);
        updateGainDbLabel();
    }

    const float inst = player.getMasterOutputMeter();
    meterBallistic = juce::jmax (inst, meterBallistic * 0.88f);
    peakMeter.setLevel (meterBallistic);
}

//==============================================================================
MixerPanel::MixerPanel (MultitrackPlayer& p)
    : player (p)
{
    for (int i = 0; i < player.getNumTracks(); ++i)
    {
        auto* strip = new MixerChannelStrip (*this, player, i);
        channelStrips.add (strip);
        content.addAndMakeVisible (strip);
    }

    masterStrip = std::make_unique<MixerMasterStrip> (*this, player);
    content.addAndMakeVisible (*masterStrip);

    viewport.setViewedComponent (&content, false);
    addAndMakeVisible (viewport);

    startTimerHz (12);
}

MixerPanel::~MixerPanel()
{
    stopTimer();
    pluginPickerWindow.reset();
    dismissPluginEditor();
}

void MixerPanel::resized()
{
    viewport.setBounds (getLocalBounds());

    const int pad = 8;
    const int stripW = 120;
    const int masterW = 124;
    const int n = player.getNumTracks();
    const int totalW = pad * 2 + n * stripW + masterW + (n > 0 ? pad + 6 * (n - 1) : 0);
    const int h = juce::jmax (280, viewport.getHeight() - pad * 2);

    int x = pad;
    for (auto* s : channelStrips)
    {
        s->setBounds (x, pad, stripW, h);
        x += stripW + 6;
    }

    masterStrip->setBounds (x + pad, pad, masterW, h);

    content.setSize (juce::jmax (viewport.getWidth(), totalW), viewport.getHeight());
}

void MixerPanel::chooseAndAddTrackPlugin (int trackIndex)
{
    pluginPickerWindow.reset();

    const double sr = player.getHostSampleRate() > 0.0 ? player.getHostSampleRate() : 44100.0;
    const int    bs = player.getHostBlockSize() > 0 ? player.getHostBlockSize() : 512;

    pluginPickerWindow = std::make_unique<PluginPickerWindow> (
        AppPluginRegistry::get().getFormatManager(),
        AppPluginRegistry::get().getKnownList(),
        sr,
        bs,
        [this, trackIndex] (std::unique_ptr<juce::AudioProcessor> proc)
        {
            if (proc != nullptr)
                player.addTrackInsert (trackIndex, std::move (proc));

            refreshAllStrips();
        },
        [this]
        {
            pluginPickerWindow.reset();
        });

    pluginPickerWindow->toFront (true);
}

void MixerPanel::chooseAndAddMasterPlugin()
{
    pluginPickerWindow.reset();

    const double sr = player.getHostSampleRate() > 0.0 ? player.getHostSampleRate() : 44100.0;
    const int    bs = player.getHostBlockSize() > 0 ? player.getHostBlockSize() : 512;

    pluginPickerWindow = std::make_unique<PluginPickerWindow> (
        AppPluginRegistry::get().getFormatManager(),
        AppPluginRegistry::get().getKnownList(),
        sr,
        bs,
        [this] (std::unique_ptr<juce::AudioProcessor> proc)
        {
            if (proc != nullptr)
                player.addMasterInsert (std::move (proc));

            refreshAllStrips();
        },
        [this]
        {
            pluginPickerWindow.reset();
        });

    pluginPickerWindow->toFront (true);
}

void MixerPanel::removeTrackPlugin (int trackIndex, int insertIndex)
{
    dismissPluginEditor();
    player.removeTrackInsert (trackIndex, insertIndex);
    refreshAllStrips();
}

void MixerPanel::removeMasterPlugin (int insertIndex)
{
    dismissPluginEditor();
    player.removeMasterInsert (insertIndex);
    refreshAllStrips();
}

void MixerPanel::openPluginEditor (juce::AudioProcessor& proc)
{
    dismissPluginEditor();
    pluginEditorWindow = std::make_unique<PluginEditorHost> (
        proc,
        [this]
        { pluginEditorWindow.reset(); });
    pluginEditorWindow->setAlwaysOnTop (true);
    pluginEditorWindow->setVisible (true);
}

void MixerPanel::dismissPluginEditor()
{
    pluginEditorWindow.reset();
}

void MixerPanel::refreshAllStrips()
{
    for (auto* s : channelStrips)
        s->refreshSlots();

    if (masterStrip != nullptr)
        masterStrip->refreshSlots();
}

void MixerPanel::timerCallback()
{
    for (auto* s : channelStrips)
        s->syncGainFromPlayer();

    if (masterStrip != nullptr)
        masterStrip->syncGainFromPlayer();
}

//==============================================================================
MixerWindow::MixerWindow (MultitrackPlayer& p)
    : DocumentWindow ("Mixer",
                      juce::Colour (0xFF0B1220),
                      DocumentWindow::closeButton,
                      true)
    , player (p)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setResizeLimits (520, 320, 4000, 1200);

    setContentOwned (new MixerPanel (player), true);
    setSize (juce::jmax (640, p.getNumTracks() * 120 + 200), 420);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void MixerWindow::closeButtonPressed()
{
    setVisible (false);
}

} // namespace ezstemz
