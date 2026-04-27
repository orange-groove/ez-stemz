#include "LocalSeparator.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

// Eigen (pulled in transitively by demucs.cpp's model.hpp) trips a lot of
// pedantic warnings under JUCE's recommended flags. Those headers are out of
// our control, so silence them just for this translation unit.
#if defined(__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "model.hpp"

#if defined(__clang__)
 #pragma clang diagnostic pop
#elif defined(__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace ezstemz
{

namespace
{

constexpr int kDemucsSampleRate = 44100;

/** Decode `audioFile` with JUCE, downmix/upmix to stereo, resample to 44.1k,
    and return as an Eigen::MatrixXf with shape (2, N) — the layout demucs
    expects. Returns an empty matrix on failure. */
Eigen::MatrixXf loadAudioAsDemucsMatrix (const juce::File& audioFile,
                                          juce::String& errorOut)
{
    juce::AudioFormatManager fmtMgr;
    fmtMgr.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fmtMgr.createReaderFor (audioFile));
    if (reader == nullptr)
    {
        errorOut = "Could not decode audio file: " + audioFile.getFullPathName();
        return {};
    }

    const auto srcSampleRate = reader->sampleRate;
    const auto srcLength     = (int) reader->lengthInSamples;
    const int  srcChannels   = (int) reader->numChannels;

    if (srcSampleRate <= 0.0 || srcLength <= 0 || srcChannels <= 0)
    {
        errorOut = "Invalid audio file (zero length / unknown sample rate).";
        return {};
    }

    juce::AudioBuffer<float> srcBuffer ((int) juce::jmax (1, srcChannels), srcLength);
    if (! reader->read (&srcBuffer, 0, srcLength, 0, true, true))
    {
        errorOut = "Failed to read samples from " + audioFile.getFileName();
        return {};
    }

    // Force stereo: duplicate mono, average extras down to L/R.
    juce::AudioBuffer<float> stereo (2, srcLength);
    if (srcChannels == 1)
    {
        stereo.copyFrom (0, 0, srcBuffer, 0, 0, srcLength);
        stereo.copyFrom (1, 0, srcBuffer, 0, 0, srcLength);
    }
    else
    {
        stereo.copyFrom (0, 0, srcBuffer, 0, 0, srcLength);
        stereo.copyFrom (1, 0, srcBuffer, juce::jmin (1, srcChannels - 1), 0, srcLength);

        for (int extra = 2; extra < srcChannels; ++extra)
        {
            const float gain = 0.5f; // simple summing
            stereo.addFrom (extra % 2, 0, srcBuffer, extra, 0, srcLength, gain);
        }
    }

    // Resample to 44.1k if needed.
    juce::AudioBuffer<float> resampled;
    if (juce::approximatelyEqual (srcSampleRate, (double) kDemucsSampleRate))
    {
        resampled = std::move (stereo);
    }
    else
    {
        const double ratio  = srcSampleRate / (double) kDemucsSampleRate;
        const int    outLen = (int) std::ceil ((double) srcLength / ratio);
        resampled.setSize (2, outLen);
        resampled.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio,
                            stereo.getReadPointer (ch),
                            resampled.getWritePointer (ch),
                            outLen);
        }
    }

    const int outLen = resampled.getNumSamples();
    Eigen::MatrixXf out (2, outLen);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = resampled.getReadPointer (ch);
        for (int i = 0; i < outLen; ++i)
            out (ch, i) = src[i];
    }

    return out;
}

bool writeStereoMatrixAsWav (const Eigen::MatrixXf& matrix, const juce::File& dest)
{
    const int numChannels = (int) matrix.rows();
    const int numSamples  = (int) matrix.cols();

    if (numChannels != 2 || numSamples <= 0)
        return false;

    if (dest.existsAsFile())
        dest.deleteFile();
    dest.getParentDirectory().createDirectory();

    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> outStream (dest.createOutputStream());
    if (outStream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (outStream.get(), kDemucsSampleRate, 2,
                             16, {}, 0));
    if (writer == nullptr)
        return false;

    outStream.release(); // writer owns the stream

    juce::AudioBuffer<float> buf (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* dst = buf.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            dst[i] = matrix (ch, i);
    }

    if (! writer->writeFromAudioSampleBuffer (buf, 0, numSamples))
        return false;

    writer.reset(); // flush
    return true;
}

juce::StringArray namesFor (bool is4sources)
{
    if (is4sources)
        return { "drums", "bass", "other", "vocals" };
    return { "drums", "bass", "other", "vocals", "guitar", "piano" };
}

} // namespace

struct LocalSeparator::Impl
{
    std::unique_ptr<demucscpp::demucs_model> model;
    juce::File                               loadedFrom;
    bool                                     loaded = false;
};

LocalSeparator::LocalSeparator() : impl (std::make_unique<Impl>()) {}

LocalSeparator::~LocalSeparator() = default;

bool LocalSeparator::isLoaded() const noexcept
{
    return impl->loaded;
}

juce::StringArray LocalSeparator::getStemNames() const
{
    if (! impl->loaded || impl->model == nullptr)
        return {};
    return namesFor (impl->model->is_4sources);
}

bool LocalSeparator::loadModel (const juce::File& modelFile, juce::String& errorOut)
{
    if (! modelFile.existsAsFile())
    {
        errorOut = "Model file not found: " + modelFile.getFullPathName();
        return false;
    }

    if (impl->loaded && impl->loadedFrom == modelFile)
        return true;

    auto next = std::make_unique<demucscpp::demucs_model>();
    if (! demucscpp::load_demucs_model (modelFile.getFullPathName().toStdString(), next.get()))
    {
        errorOut = "Failed to load demucs model (wrong file or unsupported variant?).";
        return false;
    }

    impl->model      = std::move (next);
    impl->loadedFrom = modelFile;
    impl->loaded     = true;
    return true;
}

LocalSeparator::Result LocalSeparator::separate (const juce::File& inputAudio,
                                                 const juce::File& outputDir,
                                                 std::function<void (float, const juce::String&)> progress)
{
    Result r;

    if (! impl->loaded || impl->model == nullptr)
    {
        r.errorMessage = "Model is not loaded.";
        return r;
    }

    if (! inputAudio.existsAsFile())
    {
        r.errorMessage = "Input audio file not found.";
        return r;
    }

    if (progress) progress (0.01f, "Decoding audio...");

    juce::String decodeErr;
    Eigen::MatrixXf input = loadAudioAsDemucsMatrix (inputAudio, decodeErr);
    if (input.cols() <= 0)
    {
        r.errorMessage = decodeErr;
        return r;
    }

    if (progress) progress (0.05f, "Running demucs...");

    demucscpp::ProgressCallback cb;
    if (progress)
    {
        cb = [progress] (float p, const std::string& msg)
        {
            // Map demucs's [0,1] to [0.05, 0.95].
            progress (0.05f + 0.90f * juce::jlimit (0.0f, 1.0f, p),
                      juce::String (msg));
        };
    }
    else
    {
        cb = [] (float, const std::string&) {};
    }

    Eigen::Tensor3dXf out;
    try
    {
        out = demucscpp::demucs_inference (*impl->model, input, cb);
    }
    catch (const std::exception& e)
    {
        r.errorMessage = juce::String ("demucs inference failed: ") + e.what();
        return r;
    }

    const int numStems   = impl->model->is_4sources ? 4 : 6;
    const int numChannels = (int) out.dimension (1);
    const int numSamples  = (int) out.dimension (2);

    if (numChannels != 2 || numSamples <= 0)
    {
        r.errorMessage = "demucs returned an unexpected output shape.";
        return r;
    }

    auto names = namesFor (impl->model->is_4sources);

    outputDir.createDirectory();

    if (progress) progress (0.95f, "Writing WAVs...");

    for (int s = 0; s < numStems; ++s)
    {
        Eigen::MatrixXf stemMatrix (2, numSamples);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
                stemMatrix (ch, i) = out (s, ch, i);

        auto stemFile = outputDir.getChildFile (names[s] + ".wav");
        if (! writeStereoMatrixAsWav (stemMatrix, stemFile))
        {
            r.errorMessage = "Failed to write " + stemFile.getFileName();
            return r;
        }

        r.stemFiles.add (stemFile);
        r.stemNames.add (names[s]);
    }

    if (progress) progress (1.0f, "Done.");
    r.success = true;
    return r;
}

} // namespace ezstemz
