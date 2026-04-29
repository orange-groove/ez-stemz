#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "MultitrackPlayer.h"
#include "Project.h"
#include "TrackStripComponent.h"
#include "TransportBar.h"

namespace ezstemz
{

/**
    Player + processor screen for a single project.

    The screen never owns the separator thread itself. It enqueues work on
    `SeparationService` and listens for state-change broadcasts so the user
    can hit Back at any time without cancelling separation.

    Once stems are loaded the strips are zoomable and horizontally
    scrollable; during playback the view auto-follows the playhead.
*/
class SongProcessorComponent  : public juce::Component,
                                public juce::ChangeListener,
                                private juce::ScrollBar::Listener
{
public:
    explicit SongProcessorComponent (Project project);
    ~SongProcessorComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onBackPressed;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    void togglePlayPause();

    /** App-global spacebar play/pause shortcut.

        Listens to focus changes on the JUCE Desktop and installs itself as a
        juce::KeyListener on whatever component is currently focused. KeyListeners
        run BEFORE the focused component's own keyPressed, which is why this
        beats `juce::Button`'s default "space triggers click" behaviour. The
        shortcut is suppressed while a TextEditor is focused so typing isn't
        broken. */
    class SpacebarShortcut  : public juce::FocusChangeListener,
                              public juce::KeyListener
    {
    public:
        explicit SpacebarShortcut (SongProcessorComponent& owner);
        ~SpacebarShortcut() override;

        void globalFocusChanged (juce::Component* focusedComponent) override;
        bool keyPressed (const juce::KeyPress& key,
                         juce::Component* originatingComponent) override;

    private:
        SongProcessorComponent& owner;
        juce::Component::SafePointer<juce::Component> currentFocused;
    };

    void refreshFromService();
    void loadStems();
    void rebuildTrackStrips();
    void setProgress (float p, const juce::String& msg);

    // ---- zoom / scroll ----
    int  getWaveformWidthPx() const;
    void zoomToFit();
    void zoomBy (double factor);
    void zoomByAtSeconds (double factor, double anchorSec);
    void setScrollSeconds (double s);
    void handleWheel (const juce::MouseEvent& e,
                      const juce::MouseWheelDetails& wheel,
                      int waveformLeftX);
    void applyZoomAndScroll();
    void autoScrollToPlayhead (double currentSec);
    void setZoomControlsVisible (bool v);

    Project project;
    bool stemsLoaded = false;

    juce::AudioFormatManager  formatManager;
    juce::AudioThumbnailCache thumbnailCache { 32 };

    MultitrackPlayer         player;
    juce::AudioSourcePlayer  sourcePlayer;
    juce::AudioDeviceManager deviceManager;

    juce::TextButton backButton    { "<- Back" };
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomInButton  { "+" };
    juce::TextButton zoomFitButton { "Fit" };
    juce::Label  headerLabel;

    juce::ProgressBar progressBar { progressValue };
    double progressValue = 0.0;

    juce::Label  statusLabel;

    juce::OwnedArray<TrackStripComponent> strips;
    std::unique_ptr<TransportBar>         transportBar;

    juce::ScrollBar hScrollBar { false }; // horizontal

    double pixelsPerSecond = 0.0;
    double scrollSeconds   = 0.0;
    double trackLength     = 0.0;

    SpacebarShortcut spacebarShortcut { *this };
};

} // namespace ezstemz
