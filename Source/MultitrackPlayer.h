#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace ezstemz
{

/**
    Sample-accurate multitrack stem player.

    Each track is one of the demucs outputs (vocals, drums, bass, other, ...).
    A single master playhead (in seconds) drives every track so they stay in
    lock-step. Per-track gain / mute / solo / master gain are evaluated on the
    audio thread with linear ramps for click-free toggles.

    File-rate vs. device-rate is handled transparently: each track is wrapped
    in a juce::ResamplingAudioSource so 44.1 kHz stems play correctly even
    when the audio device is running at e.g. 48 kHz.

    Lifecycle:
        - construct
        - call `loadTracks(...)` from the message thread
        - call `prepareToPlay(...)` and feed it from an
          AudioSourcePlayer / AudioDeviceManager
        - drive playback via play() / pause() / setPosition()
*/
class MultitrackPlayer  : public juce::AudioSource,
                          private juce::Timer
{
public:
    MultitrackPlayer();
    ~MultitrackPlayer() override;

    struct TrackInfo
    {
        juce::String name;
        juce::File   file;
        double       lengthSeconds = 0.0;
    };

    /** Replaces the loaded tracks with the given audio files. Must be called
        from the message thread. Returns false if any file failed to open. */
    bool loadTracks (const juce::Array<juce::File>& files,
                     const juce::StringArray&       names);

    /** Removes all loaded tracks. */
    void clear();

    int  getNumTracks() const noexcept;
    const TrackInfo& getTrackInfo (int index) const;

    // ---------- transport ----------
    void   play();
    void   pause();
    void   stop(); // pause and rewind
    bool   isPlaying() const noexcept;

    /** Track length is the longest stem duration. */
    double getLengthSeconds() const noexcept;
    double getCurrentPositionSeconds() const noexcept;
    void   setPositionSeconds (double seconds);

    // ---------- mixer ----------
    void  setTrackGain   (int index, float linearGain);
    float getTrackGain   (int index) const;
    void  setTrackMuted  (int index, bool shouldBeMuted);
    bool  isTrackMuted   (int index) const;
    void  setTrackSoloed (int index, bool shouldBeSoloed);
    bool  isTrackSoloed  (int index) const;
    void  setMasterGain  (float linearGain);
    float getMasterGain() const noexcept;

    /** Playback rate as a multiplier (1.0 = normal speed). Implemented by
        scaling each track's resampling ratio, so it changes both speed and
        pitch (no time-stretching). Clamped to [0.25, 4.0]. */
    void  setPlaybackRate (float rate);
    float getPlaybackRate() const noexcept;

    // ---------- AudioSource ----------
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

    std::function<void (double currentSeconds, double lengthSeconds)> onPlayheadUpdate;

private:
    struct Track
    {
        juce::String name;
        juce::File   file;
        std::unique_ptr<juce::AudioFormatReader>       reader;
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        std::unique_ptr<juce::ResamplingAudioSource>   resamplingSource;
        std::atomic<float> gain  { 1.0f };
        std::atomic<bool>  muted { false };
        std::atomic<bool>  soloed{ false };
        std::atomic<float> currentGainRamp { 1.0f };
    };

    void timerCallback() override;
    bool anyTrackSoloed() const noexcept;
    void prepareTrackForPlayback (Track& t);
    void setTrackPositionInFileSamples (Track& t, juce::int64 fileSamples);

    juce::AudioFormatManager formatManager;
    juce::OwnedArray<Track>  tracks;
    juce::Array<TrackInfo>   trackInfos;

    std::atomic<bool>   playing { false };
    std::atomic<double> playheadSeconds { 0.0 };

    double deviceSampleRate = 44100.0;
    int    deviceBlockSize  = 512;
    bool   prepared         = false;

    std::atomic<float> masterGain { 1.0f };
    float              currentMasterGainRamp = 1.0f;

    std::atomic<float> playbackRate { 1.0f };

    juce::AudioBuffer<float> trackBuffer;

    juce::CriticalSection tracksLock;
};

} // namespace ezstemz
