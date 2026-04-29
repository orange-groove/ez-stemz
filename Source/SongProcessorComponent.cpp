#include "SongProcessorComponent.h"

#include "AppConfig.h"
#include "ProjectsLibrary.h"
#include "SeparationService.h"

namespace ezstemz
{

namespace
{

juce::StringArray sortedStemNames (const juce::StringArray& src)
{
    static const juce::StringArray order { "vocals", "guitar", "piano", "bass", "drums", "other" };
    auto getPriority = [] (const juce::String& name)
    {
        auto lc = name.toLowerCase();
        for (int i = 0; i < order.size(); ++i)
            if (lc.contains (order[i]))
                return i;
        return 999;
    };

    juce::StringArray copy = src;
    std::stable_sort (copy.begin(), copy.end(),
                      [&] (const juce::String& a, const juce::String& b)
                      { return getPriority (a) < getPriority (b); });
    return copy;
}

} // namespace

SongProcessorComponent::SpacebarShortcut::SpacebarShortcut (SongProcessorComponent& o)
    : owner (o)
{
    juce::Desktop::getInstance().addFocusChangeListener (this);

    // Hook into whatever already had focus when this page was created
    // (e.g. the desktop window itself).
    if (auto* current = juce::Component::getCurrentlyFocusedComponent())
        globalFocusChanged (current);
}

SongProcessorComponent::SpacebarShortcut::~SpacebarShortcut()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    if (currentFocused != nullptr)
        currentFocused->removeKeyListener (this);
}

void SongProcessorComponent::SpacebarShortcut::globalFocusChanged (juce::Component* c)
{
    if (currentFocused.getComponent() == c)
        return;

    if (currentFocused != nullptr)
        currentFocused->removeKeyListener (this);

    currentFocused = c;

    if (c != nullptr)
        c->addKeyListener (this);
}

bool SongProcessorComponent::SpacebarShortcut::keyPressed (const juce::KeyPress& key,
                                                           juce::Component* origin)
{
    if (key != juce::KeyPress::spaceKey)
        return false;

    // Don't hijack space while the user is typing into a text editor
    // (e.g. editing a slider's value text box).
    for (auto* c = origin; c != nullptr; c = c->getParentComponent())
        if (dynamic_cast<juce::TextEditor*> (c) != nullptr)
            return false;

    owner.togglePlayPause();
    return true;
}

SongProcessorComponent::SongProcessorComponent (Project p)
    : project (std::move (p))
{
    formatManager.registerBasicFormats();

    deviceManager.initialiseWithDefaultDevices (0, 2);
    sourcePlayer.setSource (&player);
    deviceManager.addAudioCallback (&sourcePlayer);

    backButton.onClick = [this] { if (onBackPressed) onBackPressed(); };
    addAndMakeVisible (backButton);

    headerLabel.setText (project.name, juce::dontSendNotification);
    headerLabel.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
    headerLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (headerLabel);

    auto styleZoomBtn = [] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF1F2937));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF374151));
        b.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    };
    styleZoomBtn (zoomOutButton);
    styleZoomBtn (zoomInButton);
    styleZoomBtn (zoomFitButton);

    zoomOutButton.onClick = [this] { zoomBy (1.0 / 1.5); };
    zoomInButton .onClick = [this] { zoomBy (1.5); };
    zoomFitButton.onClick = [this] { zoomToFit(); };

    addChildComponent (zoomOutButton);
    addChildComponent (zoomInButton);
    addChildComponent (zoomFitButton);

    hScrollBar.addListener (this);
    hScrollBar.setAutoHide (false);
    hScrollBar.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xFF6366F1));
    addChildComponent (hScrollBar);

    progressBar.setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0xFF111827));
    progressBar.setColour (juce::ProgressBar::foregroundColourId, juce::Colour (0xFF6366F1));
    addChildComponent (progressBar);

    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFCBD5E1));
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (statusLabel);

    transportBar = std::make_unique<TransportBar> (player);
    addAndMakeVisible (*transportBar);

    player.onPlayheadUpdate = [this] (double cur, double len)
    {
        if (transportBar != nullptr)
            transportBar->update (cur, len);

        for (auto* s : strips)
            s->setPlayheadSeconds (cur);

        autoScrollToPlayhead (cur);
        juce::ignoreUnused (len);
    };

    SeparationService::getInstance().addChangeListener (this);

    if (project.hasStems())
    {
        loadStems();
    }
    else if (project.hasSource())
    {
        if (! AppConfig::get().hasValidModel())
        {
            setProgress (0.0f,
                "Bundled demucs model is missing (expected at "
                + AppConfig::get().getModelFile().getFullPathName() + ").");
        }
        else
        {
            // Idempotent: if a worker is already on it, this is a no-op.
            SeparationService::getInstance().enqueue (project);
            refreshFromService();
        }
    }
    else
    {
        setProgress (0.0f, "This project has no source audio.");
    }
}

SongProcessorComponent::~SongProcessorComponent()
{
    SeparationService::getInstance().removeChangeListener (this);

    hScrollBar.removeListener (this);

    deviceManager.removeAudioCallback (&sourcePlayer);
    sourcePlayer.setSource (nullptr);
    player.stop();
    player.clear();
}

void SongProcessorComponent::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (56).reduced (12, 8);
    backButton.setBounds (header.removeFromLeft (90));
    header.removeFromLeft (12);

    // Zoom controls live in the top-right of the header.
    const int zoomBtnW = 36;
    zoomFitButton.setBounds (header.removeFromRight (52));
    header.removeFromRight (4);
    zoomInButton .setBounds (header.removeFromRight (zoomBtnW));
    header.removeFromRight (4);
    zoomOutButton.setBounds (header.removeFromRight (zoomBtnW));
    header.removeFromRight (12);

    headerLabel.setBounds (header);

    auto bottom = r.removeFromBottom (60);
    transportBar->setBounds (bottom);

    auto top = r.removeFromTop (60).reduced (12, 6);
    progressBar.setBounds (top.removeFromTop (16));
    statusLabel.setBounds (top.removeFromTop (24));

    auto stripsArea = r.reduced (8);

    // Reserve a row for the horizontal scrollbar, aligned with the
    // waveform region (skip past the controls panel on the left).
    auto scrollRow = stripsArea.removeFromBottom (16);
    const int waveX = stripsArea.getX() + TrackStripComponent::kControlsLeftWidth - 4;
    hScrollBar.setBounds (juce::Rectangle<int> (waveX,
                                                scrollRow.getY(),
                                                juce::jmax (0, stripsArea.getRight() - waveX),
                                                scrollRow.getHeight()));

    int y = stripsArea.getY();
    for (auto* s : strips)
    {
        s->setBounds (stripsArea.getX(), y, stripsArea.getWidth(), 80);
        y += 84;
    }

    // The constructor's loadStems() can run before we have a real size,
    // in which case zoomToFit() saw a zero-width waveform region. Retry
    // here once the strips have real bounds.
    if (stemsLoaded && pixelsPerSecond <= 0.0)
        zoomToFit();
    else
        applyZoomAndScroll();
}

void SongProcessorComponent::togglePlayPause()
{
    if (player.isPlaying())
        player.pause();
    else if (stemsLoaded)
        player.play();
}

void SongProcessorComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF0B1220));

    g.setColour (juce::Colour (0xFF1E293B));
    g.drawHorizontalLine (56, 0.0f, (float) getWidth());
}

void SongProcessorComponent::setProgress (float p, const juce::String& msg)
{
    progressValue = juce::jlimit (0.0, 1.0, (double) p);
    statusLabel.setText (msg, juce::dontSendNotification);
    progressBar.repaint();
}

void SongProcessorComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromService();
}

void SongProcessorComponent::refreshFromService()
{
    using Status = SeparationService::Status;
    const auto state = SeparationService::getInstance().getState (project.dir);

    switch (state.status)
    {
        case Status::Queued:
            progressBar.setVisible (true);
            setProgress (state.progress, state.message);
            break;

        case Status::Running:
            progressBar.setVisible (true);
            setProgress (state.progress, state.message);
            break;

        case Status::Done:
            progressBar.setVisible (false);
            if (! stemsLoaded)
            {
                ProjectsLibrary::refreshStems (project);
                loadStems();
            }
            break;

        case Status::Error:
            progressBar.setVisible (false);
            setProgress (0.0f, "Error: " + state.errorMessage);
            break;

        case Status::Idle:
        default:
            // Service has no record. If we already have stems on disk,
            // there's nothing to do; otherwise stay quiet.
            break;
    }
}

void SongProcessorComponent::loadStems()
{
    ProjectsLibrary::refreshStems (project);

    if (! project.hasStems())
    {
        setProgress (0.0f, "No stems available for this project.");
        return;
    }

    auto sortedNames = sortedStemNames (project.stemNames);

    juce::Array<juce::File> orderedFiles;
    juce::StringArray       orderedNames;
    for (auto& n : sortedNames)
    {
        auto idx = project.stemNames.indexOf (n);
        if (idx >= 0)
        {
            orderedFiles.add (project.stemFiles[idx]);
            orderedNames.add (n);
        }
    }

    if (! player.loadTracks (orderedFiles, orderedNames))
    {
        setProgress (0.0f, "Failed to open separated stems.");
        return;
    }

    rebuildTrackStrips();
    player.setPositionSeconds (0.0);
    setProgress (1.0f, "Ready. " + juce::String (player.getNumTracks()) + " stems loaded.");
    stemsLoaded = true;

    trackLength    = player.getLengthSeconds();
    scrollSeconds  = 0.0;
    setZoomControlsVisible (true);
    zoomToFit();
}

void SongProcessorComponent::rebuildTrackStrips()
{
    strips.clear();
    for (int i = 0; i < player.getNumTracks(); ++i)
    {
        auto* s = new TrackStripComponent (player, i, formatManager, thumbnailCache);
        s->onWheel = [this, s] (const juce::MouseEvent& e,
                                const juce::MouseWheelDetails& w)
        {
            handleWheel (e, w, s->getWaveformLeftX());
        };
        strips.add (s);
        addAndMakeVisible (s);
    }
    resized();
}

// ============================================================================
// Zoom + horizontal scroll
// ============================================================================

int SongProcessorComponent::getWaveformWidthPx() const
{
    if (strips.isEmpty())
        return 0;

    const auto stripBounds = strips[0]->getLocalBounds().reduced (4);
    return juce::jmax (0,
                       stripBounds.getWidth()
                       - (110 + 36 + 36 + 180 + 8));
}

void SongProcessorComponent::setZoomControlsVisible (bool v)
{
    zoomOutButton.setVisible (v);
    zoomInButton .setVisible (v);
    zoomFitButton.setVisible (v);
    hScrollBar   .setVisible (v);
}

void SongProcessorComponent::zoomToFit()
{
    if (trackLength <= 0.0)
        return;

    const int waveWidth = getWaveformWidthPx();
    if (waveWidth <= 0)
        return;

    pixelsPerSecond = (double) waveWidth / trackLength;
    scrollSeconds   = 0.0;
    applyZoomAndScroll();
}

void SongProcessorComponent::zoomBy (double factor)
{
    // Anchor the zoom around the current playhead so it stays put while
    // the user clicks the +/- buttons.
    if (trackLength <= 0.0 || pixelsPerSecond <= 0.0)
        return;

    const int waveWidth = getWaveformWidthPx();
    if (waveWidth <= 0)
        return;

    const double visibleSec = (double) waveWidth / pixelsPerSecond;
    const double anchorSec  = juce::jlimit (scrollSeconds,
                                            scrollSeconds + visibleSec,
                                            player.getCurrentPositionSeconds());
    zoomByAtSeconds (factor, anchorSec);
}

void SongProcessorComponent::zoomByAtSeconds (double factor, double anchorSec)
{
    if (trackLength <= 0.0)
        return;

    const int waveWidth = getWaveformWidthPx();
    if (waveWidth <= 0)
        return;

    const double minPps = (double) waveWidth / trackLength; // fit-to-width
    const double maxPps = 600.0;                            // sane cap

    const double currPps        = juce::jmax (1.0, pixelsPerSecond);
    const double anchorPxFromL  = (anchorSec - scrollSeconds) * currPps;

    const double newPps  = juce::jlimit (minPps, maxPps, currPps * factor);
    const double visible = (double) waveWidth / newPps;

    double newScroll = anchorSec - anchorPxFromL / newPps;
    newScroll = juce::jlimit (0.0,
                              juce::jmax (0.0, trackLength - visible),
                              newScroll);

    pixelsPerSecond = newPps;
    scrollSeconds   = newScroll;
    applyZoomAndScroll();
}

void SongProcessorComponent::handleWheel (const juce::MouseEvent& e,
                                          const juce::MouseWheelDetails& wheel,
                                          int waveformLeftX)
{
    if (! stemsLoaded || trackLength <= 0.0 || pixelsPerSecond <= 0.0)
        return;

    const int waveWidth = getWaveformWidthPx();
    if (waveWidth <= 0)
        return;

    const bool zoomMod = e.mods.isCommandDown() || e.mods.isCtrlDown();

    if (zoomMod)
    {
        // Cmd/Ctrl + wheel: zoom anchored at the time under the cursor.
        if (wheel.deltaY == 0.0f)
            return;

        const double mouseInWavePx = juce::jmax (0, e.x - waveformLeftX);
        const double anchorSec = juce::jlimit (0.0,
                                               trackLength,
                                               scrollSeconds + mouseInWavePx / pixelsPerSecond);
        const double factor = std::pow (1.2, (double) wheel.deltaY);
        zoomByAtSeconds (factor, anchorSec);
        return;
    }

    // Plain wheel: pan the timeline. Prefer horizontal trackpad gestures
    // when present, otherwise use vertical wheel scroll.
    const double rawDelta = (wheel.deltaX != 0.0f) ? (double) wheel.deltaX
                                                   : (double) wheel.deltaY;
    if (rawDelta == 0.0)
        return;

    const double visibleSec = (double) waveWidth / pixelsPerSecond;

    // Negative sign: scrolling "down" / swiping left advances the timeline.
    const double panSec = -rawDelta * visibleSec * 0.5;
    setScrollSeconds (scrollSeconds + panSec);
}

void SongProcessorComponent::setScrollSeconds (double s)
{
    if (trackLength <= 0.0)
        return;

    const int waveWidth = getWaveformWidthPx();
    if (waveWidth <= 0 || pixelsPerSecond <= 0.0)
        return;

    const double visible = (double) waveWidth / pixelsPerSecond;
    s = juce::jlimit (0.0, juce::jmax (0.0, trackLength - visible), s);

    if (! juce::approximatelyEqual (s, scrollSeconds))
    {
        scrollSeconds = s;
        applyZoomAndScroll();
    }
}

void SongProcessorComponent::applyZoomAndScroll()
{
    const int waveWidth = getWaveformWidthPx();
    if (waveWidth <= 0 || pixelsPerSecond <= 0.0 || trackLength <= 0.0)
        return;

    const double visible = juce::jmin ((double) waveWidth / pixelsPerSecond, trackLength);

    hScrollBar.setRangeLimits (0.0, trackLength, juce::dontSendNotification);
    hScrollBar.setCurrentRange (scrollSeconds, visible, juce::dontSendNotification);

    for (auto* s : strips)
        s->setView (pixelsPerSecond, scrollSeconds);
}

void SongProcessorComponent::autoScrollToPlayhead (double currentSec)
{
    if (! stemsLoaded || trackLength <= 0.0 || pixelsPerSecond <= 0.0)
        return;

    const int waveWidth = getWaveformWidthPx();
    if (waveWidth <= 0)
        return;

    const double visible = (double) waveWidth / pixelsPerSecond;
    if (visible >= trackLength)
        return; // everything's on screen, nothing to follow

    const double rightEdge = scrollSeconds + visible * 0.90;

    if (currentSec < scrollSeconds || currentSec > rightEdge)
    {
        // Snap so the playhead sits ~10% in from the left, giving the
        // user a steady viewport during playback (classic DAW page-flip).
        double target = currentSec - visible * 0.10;
        target = juce::jlimit (0.0, juce::jmax (0.0, trackLength - visible), target);

        if (! juce::approximatelyEqual (target, scrollSeconds))
        {
            scrollSeconds = target;
            applyZoomAndScroll();
        }
    }
}

void SongProcessorComponent::scrollBarMoved (juce::ScrollBar* sb, double newRangeStart)
{
    if (sb == &hScrollBar)
    {
        scrollSeconds = newRangeStart;
        for (auto* s : strips)
            s->setView (pixelsPerSecond, scrollSeconds);
    }
}

} // namespace ezstemz
