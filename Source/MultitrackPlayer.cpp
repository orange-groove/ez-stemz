#include "MultitrackPlayer.h"

#include "AppPluginRegistry.h"
#include "signalsmith-stretch/signalsmith-stretch.h"

#include <cmath>
#include <memory>
#include <vector>

namespace ezstemz
{
namespace
{

float peakAbsLR (const juce::AudioBuffer<float>& buf, int numSamples)
{
    float       pk = 0.f;
    const int   nCh = juce::jmin (2, buf.getNumChannels());
    const int   n   = juce::jmin (numSamples, buf.getNumSamples());

    for (int c = 0; c < nCh; ++c)
    {
        const float* p = buf.getReadPointer (c);
        for (int s = 0; s < n; ++s)
            pk = juce::jmax (pk, std::abs (p[s]));
    }

    return pk;
}

} // namespace

MultitrackPlayer::MultitrackPlayer()
{
    formatManager.registerBasicFormats();
    startTimerHz (30);
}

juce::AudioPluginFormatManager& MultitrackPlayer::getPluginFormatManager() noexcept
{
    return AppPluginRegistry::get().getFormatManager();
}

void MultitrackPlayer::reconfigureTrackMeters (int numTracks)
{
    trackMeters.reset();
    trackMeterCount = numTracks;

    if (numTracks > 0)
    {
        trackMeters = std::make_unique<std::atomic<float>[]> ((size_t) numTracks);

        for (int i = 0; i < numTracks; ++i)
            trackMeters[(size_t) i].store (0.0f, std::memory_order_relaxed);
    }

    masterMeter.store (0.0f, std::memory_order_relaxed);
}

float MultitrackPlayer::getTrackPostFaderMeter (int trackIndex) const noexcept
{
    if (trackMeters == nullptr || trackIndex < 0 || trackIndex >= trackMeterCount)
        return 0.0f;

    return trackMeters[(size_t) trackIndex].load (std::memory_order_relaxed);
}

float MultitrackPlayer::getMasterOutputMeter() const noexcept
{
    return masterMeter.load (std::memory_order_relaxed);
}

MultitrackPlayer::~MultitrackPlayer()
{
    stopTimer();
    releaseResources();
}

bool MultitrackPlayer::isStretcherActive() const noexcept
{
    const float r = playbackRate.load();
    return preservePitch.load() && std::abs (r - 1.0f) > 1.0e-4f;
}

void MultitrackPlayer::applyRateSettingsToTrack (Track& t)
{
    if (t.reader == nullptr || t.resamplingSource == nullptr)
        return;

    const double fileRate = t.reader->sampleRate > 0 ? t.reader->sampleRate
                                                     : deviceSampleRate;
    const double rate     = (double) playbackRate.load();
    const bool   preserve = preservePitch.load();

    // Pitch-preserving path: resampler does file→device only, the global
    // stretcher handles the speed change downstream.
    // Vinyl path: resampler does both file→device AND speed/pitch shift.
    const double resamplerRatio = (fileRate / deviceSampleRate) * (preserve ? 1.0 : rate);
    t.resamplingSource->setResamplingRatio (resamplerRatio);
}

void MultitrackPlayer::prepareTrackForPlayback (Track& t)
{
    if (! prepared || t.resamplingSource == nullptr || t.reader == nullptr)
        return;

    t.resamplingSource->prepareToPlay (deviceBlockSize, deviceSampleRate);
    applyRateSettingsToTrack (t);
}

void MultitrackPlayer::setTrackPositionInFileSamples (Track& t, juce::int64 fileSamples)
{
    if (t.readerSource == nullptr)
        return;

    if (t.reader != nullptr)
        fileSamples = juce::jlimit ((juce::int64) 0,
                                    t.reader->lengthInSamples,
                                    fileSamples);

    t.readerSource->setNextReadPosition (fileSamples);

    if (t.resamplingSource != nullptr)
        t.resamplingSource->flushBuffers();
}

void MultitrackPlayer::recreateGlobalStretcher()
{
    // Caller must hold tracksLock.
    if (! prepared || tracks.isEmpty())
    {
        globalStretch.reset();
        stretchInBuf.setSize (0, 0);
        stretchOutBuf.setSize (0, 0);
        stretchNumChannels = 0;
        return;
    }

    stretchNumChannels = tracks.size() * 2;

    // Worst-case input is 4x the device block size (max playback rate).
    stretchInBuf .setSize (stretchNumChannels, deviceBlockSize * 5, false, true, true);
    stretchOutBuf.setSize (stretchNumChannels, deviceBlockSize,     false, true, true);
    stretchInBuf .clear();
    stretchOutBuf.clear();

    globalStretch = std::make_unique<Stretcher>();
    // splitComputation = true spreads the spectral compute across audio
    // blocks instead of bursting at spectral boundaries. Costs one extra
    // interval of latency but is much friendlier to the realtime deadline,
    // which matters because we run the stretcher across `numTracks * 2`
    // channels in a single process() call.
    globalStretch->presetDefault (stretchNumChannels, deviceSampleRate, /*splitComputation=*/ true);
    globalStretch->reset();
    stretchInputAccumulator = 0.0;
    stretchPendingReset.store (false);
}

bool MultitrackPlayer::loadTracks (const juce::Array<juce::File>& files,
                                   const juce::StringArray&       names)
{
    juce::OwnedArray<Track> next;
    juce::Array<TrackInfo>  nextInfos;

    for (int i = 0; i < files.size(); ++i)
    {
        auto& f = files.getReference (i);
        auto* reader = formatManager.createReaderFor (f);
        if (reader == nullptr)
            return false;

        auto* t = new Track();
        t->name   = i < names.size() ? names[i] : f.getFileNameWithoutExtension();
        t->file   = f;
        t->reader.reset (reader);
        t->readerSource     = std::make_unique<juce::AudioFormatReaderSource> (t->reader.get(), false);
        t->resamplingSource = std::make_unique<juce::ResamplingAudioSource>   (t->readerSource.get(), false, 2);
        next.add (t);

        TrackInfo info;
        info.name          = t->name;
        info.file          = t->file;
        info.lengthSeconds = reader->sampleRate > 0
                                ? (double) reader->lengthInSamples / reader->sampleRate
                                : 0.0;
        nextInfos.add (info);
    }

    {
        const juce::ScopedLock sl (tracksLock);
        releaseAllInsertProcessors();

        tracks.clear();
        for (auto* t : next)
            tracks.add (t);
        next.clearQuick (false);
        trackInfos = nextInfos;
        playheadSeconds.store (0.0);

        for (auto* t : tracks)
        {
            prepareTrackForPlayback (*t);
            setTrackPositionInFileSamples (*t, 0);
        }

        recreateGlobalStretcher();
        prepareAllInsertProcessors();
        reconfigureTrackMeters (tracks.size());
    }

    return true;
}

void MultitrackPlayer::clear()
{
    pause();
    const juce::ScopedLock sl (tracksLock);
    releaseAllInsertProcessors();

    for (auto* t : tracks)
        if (t->resamplingSource != nullptr)
            t->resamplingSource->releaseResources();

    tracks.clear();
    trackInfos.clear();
    playheadSeconds.store (0.0);

    globalStretch.reset();
    stretchInBuf.setSize (0, 0);
    stretchOutBuf.setSize (0, 0);
    stretchNumChannels = 0;
    reconfigureTrackMeters (0);
}

int MultitrackPlayer::getNumTracks() const noexcept
{
    return trackInfos.size();
}

const MultitrackPlayer::TrackInfo& MultitrackPlayer::getTrackInfo (int index) const
{
    return trackInfos.getReference (index);
}

void MultitrackPlayer::play()                 { playing.store (true); }
void MultitrackPlayer::pause()                { playing.store (false); }
void MultitrackPlayer::stop()                 { playing.store (false); setPositionSeconds (0.0); }
bool MultitrackPlayer::isPlaying() const noexcept { return playing.load(); }

double MultitrackPlayer::getLengthSeconds() const noexcept
{
    double maxLen = 0.0;
    for (auto& info : trackInfos)
        maxLen = juce::jmax (maxLen, info.lengthSeconds);
    return maxLen;
}

double MultitrackPlayer::getCurrentPositionSeconds() const noexcept
{
    return playheadSeconds.load();
}

void MultitrackPlayer::setPositionSeconds (double seconds)
{
    seconds = juce::jlimit (0.0, getLengthSeconds(), seconds);
    playheadSeconds.store (seconds);

    const juce::ScopedLock sl (tracksLock);
    for (auto* t : tracks)
    {
        if (t->reader != nullptr)
        {
            const auto fileSamples = (juce::int64) (seconds * t->reader->sampleRate);
            setTrackPositionInFileSamples (*t, fileSamples);
        }
    }

    stretchPendingReset.store (true);
}

void MultitrackPlayer::setTrackGain (int index, float linearGain)
{
    const juce::ScopedLock sl (tracksLock);
    if (auto* t = tracks[index]) t->gain.store (juce::jlimit (0.0f, 4.0f, linearGain));
}

float MultitrackPlayer::getTrackGain (int index) const
{
    const juce::ScopedLock sl (tracksLock);
    return tracks[index] != nullptr ? tracks[index]->gain.load() : 1.0f;
}

void MultitrackPlayer::setTrackMuted (int index, bool m)
{
    const juce::ScopedLock sl (tracksLock);
    if (auto* t = tracks[index]) t->muted.store (m);
}

bool MultitrackPlayer::isTrackMuted (int index) const
{
    const juce::ScopedLock sl (tracksLock);
    return tracks[index] != nullptr && tracks[index]->muted.load();
}

void MultitrackPlayer::setTrackSoloed (int index, bool s)
{
    const juce::ScopedLock sl (tracksLock);
    if (auto* t = tracks[index]) t->soloed.store (s);
}

bool MultitrackPlayer::isTrackSoloed (int index) const
{
    const juce::ScopedLock sl (tracksLock);
    return tracks[index] != nullptr && tracks[index]->soloed.load();
}

void MultitrackPlayer::setMasterGain (float g) { masterGain.store (juce::jlimit (0.0f, 4.0f, g)); }
float MultitrackPlayer::getMasterGain() const noexcept { return masterGain.load(); }

void MultitrackPlayer::setPlaybackRate (float r)
{
    playbackRate.store (juce::jlimit (0.25f, 4.0f, r));

    if (! prepared)
        return;

    const juce::ScopedLock sl (tracksLock);
    for (auto* t : tracks)
        applyRateSettingsToTrack (*t);
}

float MultitrackPlayer::getPlaybackRate() const noexcept { return playbackRate.load(); }

void MultitrackPlayer::setPreservePitch (bool shouldPreservePitch)
{
    if (preservePitch.exchange (shouldPreservePitch) == shouldPreservePitch)
        return;

    if (! prepared)
        return;

    const juce::ScopedLock sl (tracksLock);
    for (auto* t : tracks)
    {
        applyRateSettingsToTrack (*t);

        // Switching modes leaves residual state in the resampler that's no
        // longer valid; flush it so the next block starts fresh.
        if (t->resamplingSource != nullptr)
            t->resamplingSource->flushBuffers();
    }

    stretchPendingReset.store (true);
}

bool MultitrackPlayer::getPreservePitch() const noexcept { return preservePitch.load(); }

bool MultitrackPlayer::anyTrackSoloed() const noexcept
{
    for (auto* t : tracks)
        if (t->soloed.load())
            return true;
    return false;
}

void MultitrackPlayer::prepareToPlay (int samplesPerBlockExpected, double sr)
{
    deviceSampleRate = sr;
    deviceBlockSize  = samplesPerBlockExpected;
    prepared         = true;

    trackBuffer.setSize (2, samplesPerBlockExpected, false, true, true);

    const juce::ScopedLock sl (tracksLock);
    for (auto* t : tracks)
    {
        prepareTrackForPlayback (*t);
        if (t->reader != nullptr)
            setTrackPositionInFileSamples (*t,
                (juce::int64) (playheadSeconds.load() * t->reader->sampleRate));
    }

    recreateGlobalStretcher();
    prepareAllInsertProcessors();
}

void MultitrackPlayer::releaseResources()
{
    const juce::ScopedLock sl (tracksLock);
    releaseAllInsertProcessors();

    for (auto* t : tracks)
        if (t->resamplingSource != nullptr)
            t->resamplingSource->releaseResources();

    trackBuffer.setSize (0, 0);
    globalStretch.reset();
    stretchInBuf.setSize (0, 0);
    stretchOutBuf.setSize (0, 0);
    stretchNumChannels = 0;
    prepared = false;
}

void MultitrackPlayer::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    if (! playing.load())
    {
        const juce::ScopedLock sl (tracksLock);

        if (trackMeters != nullptr)
            for (int i = 0; i < trackMeterCount; ++i)
                trackMeters[(size_t) i].store (0.0f, std::memory_order_relaxed);

        masterMeter.store (0.0f, std::memory_order_relaxed);
        return;
    }

    const juce::ScopedLock sl (tracksLock);
    if (tracks.isEmpty())
    {
        masterMeter.store (0.0f, std::memory_order_relaxed);
        return;
    }

    const int   numSamples    = info.numSamples;
    const bool  soloActive    = anyTrackSoloed();
    const float masterTarget  = masterGain.load();
    const int   outChannels   = info.buffer->getNumChannels();
    const bool  useStretcher  = isStretcherActive() && globalStretch != nullptr
                                && stretchNumChannels == tracks.size() * 2;

    if (trackBuffer.getNumSamples() < numSamples)
        trackBuffer.setSize (2, numSamples, false, false, true);

    auto mixTrackInto = [&] (Track& t, const float* const* trackChans, int trackChanCount)
    {
        const bool  isAudible   = soloActive ? t.soloed.load() : ! t.muted.load();
        const float trackTarget = isAudible ? t.gain.load() : 0.0f;

        const float startGain = t.currentGainRamp.load() * currentMasterGainRamp;
        const float endGain   = trackTarget * masterTarget;

        for (int ch = 0; ch < outChannels; ++ch)
        {
            const int srcCh = juce::jmin (ch, trackChanCount - 1);
            if (srcCh < 0)
                continue;

            info.buffer->addFromWithRamp (ch,
                                          info.startSample,
                                          trackChans[srcCh],
                                          numSamples,
                                          startGain,
                                          endGain);
        }

        t.currentGainRamp.store (trackTarget);
    };

    if (useStretcher)
    {
        // Single global stretcher across every stem keeps the FFT windows
        // phase-coherent so summing the stems doesn't flutter / comb.
        const double rate = (double) playbackRate.load();
        stretchInputAccumulator += (double) numSamples * rate;
        const int inSamples = juce::jmax (1, (int) stretchInputAccumulator);
        stretchInputAccumulator -= (double) inSamples;

        if (stretchInBuf.getNumSamples() < inSamples)
            stretchInBuf.setSize (stretchNumChannels, inSamples, false, false, true);

        stretchInBuf.clear (0, inSamples);

        // Pull each stem into its own stereo channel pair within stretchInBuf.
        for (int i = 0; i < tracks.size(); ++i)
        {
            auto* t = tracks.getUnchecked (i);
            if (t->resamplingSource == nullptr)
                continue;

            float* chPtrs[2] = { stretchInBuf.getWritePointer (i * 2),
                                 stretchInBuf.getWritePointer (i * 2 + 1) };
            juce::AudioBuffer<float> sub (chPtrs, 2, inSamples);
            juce::AudioSourceChannelInfo subInfo (&sub, 0, inSamples);
            t->resamplingSource->getNextAudioBlock (subInfo);
        }

        if (stretchPendingReset.exchange (false))
        {
            globalStretch->reset();
            stretchInputAccumulator = 0.0;
        }

        // Build raw pointer arrays for the stretcher.
        std::vector<const float*> inPtrs  ((size_t) stretchNumChannels);
        std::vector<float*>       outPtrs ((size_t) stretchNumChannels);
        for (int k = 0; k < stretchNumChannels; ++k)
        {
            inPtrs[(size_t) k]  = stretchInBuf .getReadPointer  (k);
            outPtrs[(size_t) k] = stretchOutBuf.getWritePointer (k);
        }

        globalStretch->process (inPtrs.data(), inSamples,
                                outPtrs.data(), numSamples);

        for (int i = 0; i < tracks.size(); ++i)
        {
            auto* t = tracks.getUnchecked (i);
            trackBuffer.copyFrom (0, 0, stretchOutBuf, i * 2,     0, numSamples);
            trackBuffer.copyFrom (1, 0, stretchOutBuf, i * 2 + 1, 0, numSamples);
            processInsertChain (t->inserts, trackBuffer);

            if (trackMeters != nullptr && i < trackMeterCount)
            {
                const bool  isAudible   = soloActive ? t->soloed.load() : ! t->muted.load();
                const float trackTarget = isAudible ? t->gain.load() : 0.0f;
                const float pk          = peakAbsLR (trackBuffer, numSamples);
                trackMeters[(size_t) i].store (pk * trackTarget, std::memory_order_relaxed);
            }

            const float* trackChans[2] = { trackBuffer.getReadPointer (0),
                                           trackBuffer.getReadPointer (1) };
            mixTrackInto (*t, trackChans, 2);
        }
    }
    else
    {
        // Bypass path: pull each stem at the device rate directly. Zero
        // added latency / CPU; pitch and tempo move together when the
        // playback rate is not 1.0 (vinyl mode), or both stay native (1.0).
        for (int ti = 0; ti < tracks.size(); ++ti)
        {
            auto* t = tracks.getUnchecked (ti);
            if (t->resamplingSource == nullptr)
                continue;

            trackBuffer.clear (0, numSamples);
            juce::AudioSourceChannelInfo localInfo (&trackBuffer, 0, numSamples);
            t->resamplingSource->getNextAudioBlock (localInfo);

            if (t->reader != nullptr && t->reader->numChannels == 1)
                trackBuffer.copyFrom (1, 0, trackBuffer, 0, 0, numSamples);

            processInsertChain (t->inserts, trackBuffer);

            if (trackMeters != nullptr && ti < trackMeterCount)
            {
                const bool  isAudible   = soloActive ? t->soloed.load() : ! t->muted.load();
                const float trackTarget = isAudible ? t->gain.load() : 0.0f;
                const float pk          = peakAbsLR (trackBuffer, numSamples);
                trackMeters[(size_t) ti].store (pk * trackTarget, std::memory_order_relaxed);
            }

            const float* trackChans[2] = { trackBuffer.getReadPointer (0),
                                           trackBuffer.getNumChannels() > 1
                                               ? trackBuffer.getReadPointer (1)
                                               : trackBuffer.getReadPointer (0) };
            mixTrackInto (*t, trackChans, 2);
        }
    }

    if (outChannels >= 2 && ! masterInserts.empty())
    {
        float* ptrs[2] = { info.buffer->getWritePointer (0, info.startSample),
                           info.buffer->getWritePointer (1, info.startSample) };
        juce::AudioBuffer<float> masterBuf (ptrs, 2, numSamples);
        processInsertChain (masterInserts, masterBuf);
    }

    currentMasterGainRamp = masterTarget;

    {
        float       pk = 0.0f;
        const int   j0 = info.startSample;
        const int   nc = juce::jmax (1, juce::jmin (2, outChannels));

        for (int s = 0; s < numSamples; ++s)
            for (int c = 0; c < nc; ++c)
                pk = juce::jmax (pk, std::abs (info.buffer->getSample (c, j0 + s)));

        masterMeter.store (pk, std::memory_order_relaxed);
    }

    // Advance the master playhead by elapsed song-time. Both modes consume
    // input at `rate × deviceRate` samples/sec, so playhead bookkeeping is
    // identical.
    const double rate    = (double) playbackRate.load();
    const double elapsed = (double) numSamples / deviceSampleRate * rate;
    const double total   = getLengthSeconds();
    double pos = playheadSeconds.load() + elapsed;

    if (total > 0.0 && pos >= total)
    {
        pos = total;
        playing.store (false);
    }
    playheadSeconds.store (pos);
}

void MultitrackPlayer::releaseAllInsertProcessors()
{
    for (auto* t : tracks)
    {
        for (auto& p : t->inserts)
            if (p != nullptr)
                p->releaseResources();

        t->inserts.clear();
    }

    for (auto& p : masterInserts)
        if (p != nullptr)
            p->releaseResources();

    masterInserts.clear();
}

void MultitrackPlayer::prepareAllInsertProcessors()
{
    for (auto* t : tracks)
        for (auto& p : t->inserts)
            if (p != nullptr)
                p->prepareToPlay (deviceSampleRate, deviceBlockSize);

    for (auto& p : masterInserts)
        if (p != nullptr)
            p->prepareToPlay (deviceSampleRate, deviceBlockSize);
}

void MultitrackPlayer::processInsertChain (std::vector<std::unique_ptr<juce::AudioProcessor>>& chain,
                                           juce::AudioBuffer<float>& stereo)
{
    if (chain.empty() || stereo.getNumChannels() < 2)
        return;

    juce::MidiBuffer midi;

    for (auto& p : chain)
    {
        if (p == nullptr)
            continue;

        bool bypassed = false;
        if (auto* bp = p->getBypassParameter())
            bypassed = (bp->getValue() >= 0.5f);

        if (bypassed)
            p->processBlockBypassed (stereo, midi);
        else
            p->processBlock (stereo, midi);
    }
}

void MultitrackPlayer::addTrackInsert (int trackIndex, std::unique_ptr<juce::AudioProcessor> proc)
{
    if (proc == nullptr)
        return;

    const juce::ScopedLock sl (tracksLock);

    if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return;

    if (prepared)
        proc->prepareToPlay (deviceSampleRate, deviceBlockSize);

    tracks.getUnchecked (trackIndex)->inserts.push_back (std::move (proc));
}

void MultitrackPlayer::removeTrackInsert (int trackIndex, int insertIndex)
{
    const juce::ScopedLock sl (tracksLock);

    if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return;

    auto& ins = tracks.getUnchecked (trackIndex)->inserts;

    if (! juce::isPositiveAndBelow (insertIndex, (int) ins.size()))
        return;

    if (ins[(size_t) insertIndex] != nullptr)
        ins[(size_t) insertIndex]->releaseResources();

    ins.erase (ins.begin() + insertIndex);
}

int MultitrackPlayer::getNumTrackInserts (int trackIndex) const
{
    const juce::ScopedLock sl (tracksLock);

    if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return 0;

    return (int) tracks.getUnchecked (trackIndex)->inserts.size();
}

juce::AudioProcessor* MultitrackPlayer::getTrackInsert (int trackIndex, int insertIndex) const noexcept
{
    const juce::ScopedLock sl (tracksLock);

    if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return nullptr;

    const auto& ins = tracks.getUnchecked (trackIndex)->inserts;

    if (! juce::isPositiveAndBelow (insertIndex, (int) ins.size()))
        return nullptr;

    return ins[(size_t) insertIndex].get();
}

void MultitrackPlayer::addMasterInsert (std::unique_ptr<juce::AudioProcessor> proc)
{
    if (proc == nullptr)
        return;

    const juce::ScopedLock sl (tracksLock);

    if (prepared)
        proc->prepareToPlay (deviceSampleRate, deviceBlockSize);

    masterInserts.push_back (std::move (proc));
}

void MultitrackPlayer::removeMasterInsert (int insertIndex)
{
    const juce::ScopedLock sl (tracksLock);

    if (! juce::isPositiveAndBelow (insertIndex, (int) masterInserts.size()))
        return;

    if (masterInserts[(size_t) insertIndex] != nullptr)
        masterInserts[(size_t) insertIndex]->releaseResources();

    masterInserts.erase (masterInserts.begin() + insertIndex);
}

int MultitrackPlayer::getNumMasterInserts() const
{
    const juce::ScopedLock sl (tracksLock);
    return (int) masterInserts.size();
}

juce::AudioProcessor* MultitrackPlayer::getMasterInsert (int insertIndex) const noexcept
{
    const juce::ScopedLock sl (tracksLock);

    if (! juce::isPositiveAndBelow (insertIndex, (int) masterInserts.size()))
        return nullptr;

    return masterInserts[(size_t) insertIndex].get();
}

void MultitrackPlayer::timerCallback()
{
    if (onPlayheadUpdate)
        onPlayheadUpdate (getCurrentPositionSeconds(), getLengthSeconds());
}

} // namespace ezstemz
