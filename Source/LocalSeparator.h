#pragma once

#include <juce_core/juce_core.h>

namespace ezstemz
{

/**
    Local source separation backed by demucs.cpp.

    Loads a single model file once and reuses it for every separation.
    Separation runs synchronously on the calling thread and is intended to be
    invoked from a background thread (it can take minutes on long songs).

    Output stems are written as 16-bit PCM stereo WAVs at 44.1 kHz to
    `outputDir / <stem_name>.wav`.

    Uses a pimpl pattern so that demucs.cpp's Eigen headers don't leak into
    the rest of the codebase.
*/
class LocalSeparator
{
public:
    LocalSeparator();
    ~LocalSeparator();

    /** Load (or reload) the model from `modelFile`. Returns false on failure. */
    bool loadModel (const juce::File& modelFile, juce::String& errorOut);

    bool isLoaded() const noexcept;

    /** Stem names produced by the loaded model, in canonical order
        (e.g. {"drums","bass","other","vocals"} or with "guitar","piano" added). */
    juce::StringArray getStemNames() const;

    struct Result
    {
        bool                    success = false;
        juce::String            errorMessage;
        juce::Array<juce::File> stemFiles; // parallel to getStemNames()
        juce::StringArray       stemNames;
    };

    /** Decode `inputAudio`, run demucs, write stems into `outputDir`.

        `progress` (optional) is invoked from the calling thread with values
        in [0, 1] and a short status string. */
    Result separate (const juce::File& inputAudio,
                     const juce::File& outputDir,
                     std::function<void (float, const juce::String&)> progress);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace ezstemz
