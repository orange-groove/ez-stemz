#pragma once

#include <functional>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace ezstemz
{

/**
    Offline WAV writers for stem / mix export.

    Mix export sums stems at their native sample rate with the same mute /
    solo / per-track gain / master gain rules as MultitrackPlayer at playback
    rate 1.0 (no time-stretch). Individual stem export applies only that
    track's fader gain to a stereo WAV.
*/
namespace MixdownExporter
{
    struct TrackMixSource
    {
        juce::File   file;
        float        linearGain = 1.0f;
        bool         muted      = false;
        bool         soloed    = false;
    };

    using ProgressFn = std::function<void (float progress01, const juce::String& message)>;

    /** Stereo 16-bit PCM WAV. `tracks` must share one sample rate. */
    bool writeStereoMixWav (juce::AudioFormatManager& formatManager,
                            const juce::Array<TrackMixSource>& tracks,
                            float masterGainLinear,
                            const juce::File& destinationWav,
                            const ProgressFn& progress,
                            juce::String& errorOut);

    /** One stem as stereo 16-bit PCM WAV with `linearGain` applied (mute/solo ignored). */
    bool writeStemWavWithGain (juce::AudioFormatManager& formatManager,
                               const juce::File& sourceWav,
                               float linearGain,
                               const juce::File& destinationWav,
                               const ProgressFn& progress,
                               juce::String& errorOut);
} // namespace MixdownExporter

} // namespace ezstemz
