#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>
#include <vector>

namespace signalsmith { namespace stretch {
    template <typename Sample, class RandomEngine> class SignalsmithStretch;
}}

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

    /** Peak level (linear, 0..1+) after track fader for the last processed
        audio block. For UI meters only; read from the message thread. */
    float getTrackPostFaderMeter (int trackIndex) const noexcept;

    /** Peak level (linear) of the master output for the last processed block. */
    float getMasterOutputMeter() const noexcept;

    /** Playback rate as a multiplier (1.0 = normal speed). When pitch
        preservation is on (the default), a phase-vocoder-style time
        stretcher is used; otherwise it falls back to vinyl-style
        resampling that also shifts pitch. Clamped to [0.25, 4.0]. */
    void  setPlaybackRate (float rate);
    float getPlaybackRate() const noexcept;

    /** Whether to keep the source pitch when the playback rate changes.
        Default true. When false, changing the rate shifts the pitch
        (vinyl mode) but uses zero added latency / CPU. */
    void  setPreservePitch (bool shouldPreservePitch);
    bool  getPreservePitch() const noexcept;

    double getHostSampleRate() const noexcept { return deviceSampleRate; }
    int    getHostBlockSize() const noexcept { return deviceBlockSize; }

    // ---------- inserts (FX) - processed on the audio thread; UI must use
    //       the add/remove accessors below (they take tracksLock). ----------

    juce::AudioPluginFormatManager& getPluginFormatManager() noexcept;

    void addTrackInsert (int trackIndex, std::unique_ptr<juce::AudioProcessor> proc);
    void removeTrackInsert (int trackIndex, int insertIndex);
    int  getNumTrackInserts (int trackIndex) const;
    juce::AudioProcessor* getTrackInsert (int trackIndex, int insertIndex) const noexcept;

    void addMasterInsert (std::unique_ptr<juce::AudioProcessor> proc);
    void removeMasterInsert (int insertIndex);
    int  getNumMasterInserts() const;
    juce::AudioProcessor* getMasterInsert (int insertIndex) const noexcept;

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

        std::vector<std::unique_ptr<juce::AudioProcessor>> inserts;
    };

    using Stretcher = signalsmith::stretch::SignalsmithStretch<float, void>;

    void timerCallback() override;
    bool anyTrackSoloed() const noexcept;
    void prepareTrackForPlayback (Track& t);
    void setTrackPositionInFileSamples (Track& t, juce::int64 fileSamples);
    void applyRateSettingsToTrack (Track& t);
    void recreateGlobalStretcher();
    bool isStretcherActive() const noexcept;

    void releaseAllInsertProcessors();
    void prepareAllInsertProcessors();
    static void processInsertChain (std::vector<std::unique_ptr<juce::AudioProcessor>>& chain,
                                     juce::AudioBuffer<float>& stereo);

    void reconfigureTrackMeters (int numTracks);

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

    std::atomic<float> playbackRate   { 1.0f };
    std::atomic<bool>  preservePitch  { true };

    juce::AudioBuffer<float> trackBuffer;

    // ---- shared time-stretcher ---------------------------------------------
    //
    // We feed every stem through a single SignalsmithStretch instance so that
    // all the stems share the same FFT window timing. Running independent
    // stretchers per-stem causes audible flutter / comb-filtering on mixdown,
    // because each stem's phase-vocoder window phase drifts independently and
    // the stems are heavily correlated (they came from the same source mix).
    //
    // The stretcher input is `numTracks * 2` channels (stereo per stem),
    // packed into stretchInBuf and stretchOutBuf at fixed channel offsets.
    std::unique_ptr<Stretcher> globalStretch;
    juce::AudioBuffer<float>   stretchInBuf;
    juce::AudioBuffer<float>   stretchOutBuf;
    int                        stretchNumChannels = 0;
    double                     stretchInputAccumulator = 0.0;
    std::atomic<bool>          stretchPendingReset { false };

    std::vector<std::unique_ptr<juce::AudioProcessor>> masterInserts;

    std::unique_ptr<std::atomic<float>[]> trackMeters;
    int                                 trackMeterCount = 0;
    std::atomic<float>                  masterMeter { 0.0f };

    juce::CriticalSection tracksLock;
};

} // namespace ezstemz
