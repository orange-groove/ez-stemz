#include "MixdownExporter.h"

namespace ezstemz
{
namespace MixdownExporter
{
namespace
{
bool anySoloed (const juce::Array<TrackMixSource>& tracks)
{
    for (const auto& t : tracks)
        if (t.soloed)
            return true;
    return false;
}

void clampStereo (juce::AudioBuffer<float>& buf, int numSamples)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        auto* p = buf.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            p[i] = juce::jlimit (-1.0f, 1.0f, p[i]);
    }
}
} // namespace

bool writeStereoMixWav (juce::AudioFormatManager& formatManager,
                        const juce::Array<TrackMixSource>& tracks,
                        float masterGainLinear,
                        const juce::File& destinationWav,
                        const ProgressFn& progress,
                        juce::String& errorOut)
{
    if (tracks.isEmpty())
    {
        errorOut = "No tracks to mix.";
        return false;
    }

    juce::OwnedArray<juce::AudioFormatReader> readers;
    readers.ensureStorageAllocated (tracks.size());

    double sampleRate = 0.0;
    juce::int64 maxSamples = 0;

    for (const auto& spec : tracks)
    {
        if (! spec.file.existsAsFile())
        {
            errorOut = "Missing stem file: " + spec.file.getFullPathName();
            return false;
        }

        std::unique_ptr<juce::AudioFormatReader> r (formatManager.createReaderFor (spec.file));
        if (r == nullptr)
        {
            errorOut = "Could not open: " + spec.file.getFileName();
            return false;
        }

        if (sampleRate <= 0.0)
            sampleRate = r->sampleRate;
        else if (std::abs (r->sampleRate - sampleRate) > 0.5)
        {
            errorOut = "All stems must share the same sample rate for export.";
            return false;
        }

        maxSamples = juce::jmax (maxSamples, r->lengthInSamples);
        readers.add (r.release());
    }

    const int numCh = 2;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> out (destinationWav.createOutputStream());
    if (out == nullptr || ! out->openedOk())
    {
        errorOut = "Could not write: " + destinationWav.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.get(), sampleRate, (unsigned int) numCh, 16, {}, 0));
    if (writer == nullptr)
    {
        errorOut = "Could not create WAV encoder.";
        return false;
    }
    out.release();

    const bool soloActive = anySoloed (tracks);
    constexpr int block = 8192;

    juce::AudioBuffer<float> mix (2, block);
    juce::AudioBuffer<float> stemBuf (2, block);

    juce::int64 written = 0;
    while (written < maxSamples)
    {
        const int n = (int) juce::jmin ((juce::int64) block, maxSamples - written);
        mix.clear (0, n);

        for (int ti = 0; ti < tracks.size(); ++ti)
        {
            auto* reader = readers.getUnchecked (ti);
            const auto& spec = tracks.getReference (ti);

            const bool audible = soloActive ? spec.soloed : ! spec.muted;
            if (! audible)
                continue;

            const float g = juce::jlimit (0.0f, 4.0f, spec.linearGain)
                          * juce::jlimit (0.0f, 4.0f, masterGainLinear);

            stemBuf.setSize (2, n, false, false, true);
            stemBuf.clear (0, n);

            juce::AudioBuffer<float> readView (stemBuf.getArrayOfWritePointers(),
                                               juce::jmin ((int) reader->numChannels, 2),
                                               n);
            if (! reader->read (&readView, 0, n, written, true, true))
            {
                errorOut = "Read error while mixing.";
                return false;
            }

            if (reader->numChannels == 1)
            {
                const float* m = stemBuf.getReadPointer (0);
                float* L = stemBuf.getWritePointer (0);
                float* R = stemBuf.getWritePointer (1);
                for (int i = 0; i < n; ++i)
                    L[i] = R[i] = m[i];
            }

            for (int ch = 0; ch < 2; ++ch)
                mix.addFrom (ch, 0, stemBuf, ch, 0, n, g);
        }

        clampStereo (mix, n);
        if (! writer->writeFromAudioSampleBuffer (mix, 0, n))
        {
            errorOut = "Write error while exporting mix.";
            return false;
        }

        written += n;
        if (progress != nullptr)
            progress ((float) written / (float) juce::jmax ((juce::int64) 1, maxSamples),
                      "Exporting mix..");
    }

    writer.reset();
    if (progress != nullptr)
        progress (1.0f, "Mix export finished.");
    return true;
}

bool writeStemWavWithGain (juce::AudioFormatManager& formatManager,
                           const juce::File& sourceWav,
                           float linearGain,
                           const juce::File& destinationWav,
                           const ProgressFn& progress,
                           juce::String& errorOut)
{
    if (! sourceWav.existsAsFile())
    {
        errorOut = "Missing file: " + sourceWav.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (sourceWav));
    if (reader == nullptr)
    {
        errorOut = "Could not open: " + sourceWav.getFileName();
        return false;
    }

    const double sampleRate = reader->sampleRate;
    const juce::int64 total = reader->lengthInSamples;
    const float g = juce::jlimit (0.0f, 4.0f, linearGain);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> out (destinationWav.createOutputStream());
    if (out == nullptr || ! out->openedOk())
    {
        errorOut = "Could not write: " + destinationWav.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.get(), sampleRate, 2u, 16, {}, 0));
    if (writer == nullptr)
    {
        errorOut = "Could not create WAV encoder.";
        return false;
    }
    out.release();

    constexpr int block = 8192;
    juce::AudioBuffer<float> buf (2, block);

    juce::int64 pos = 0;
    while (pos < total)
    {
        const int n = (int) juce::jmin ((juce::int64) block, total - pos);
        buf.clear (0, n);

        juce::AudioBuffer<float> readView (buf.getArrayOfWritePointers(),
                                           juce::jmin ((int) reader->numChannels, 2),
                                           n);
        if (! reader->read (&readView, 0, n, pos, true, true))
        {
            errorOut = "Read error while exporting stem.";
            return false;
        }

        if (reader->numChannels == 1)
        {
            const float* m = buf.getReadPointer (0);
            float* L = buf.getWritePointer (0);
            float* R = buf.getWritePointer (1);
            for (int i = 0; i < n; ++i)
                L[i] = R[i] = m[i];
        }

        for (int ch = 0; ch < 2; ++ch)
            buf.applyGain (ch, 0, n, g);

        clampStereo (buf, n);
        if (! writer->writeFromAudioSampleBuffer (buf, 0, n))
        {
            errorOut = "Write error while exporting stem.";
            return false;
        }

        pos += n;
        if (progress != nullptr)
            progress ((float) pos / (float) juce::jmax ((juce::int64) 1, total),
                      "Exporting stem...");
    }

    writer.reset();
    if (progress != nullptr)
        progress (1.0f, "Stem export finished.");
    return true;
}

} // namespace MixdownExporter
} // namespace ezstemz
