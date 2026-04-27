#include "MultitrackPlayer.h"

namespace ezstemz
{

MultitrackPlayer::MultitrackPlayer()
{
    formatManager.registerBasicFormats();
    startTimerHz (30);
}

MultitrackPlayer::~MultitrackPlayer()
{
    stopTimer();
    releaseResources();
}

void MultitrackPlayer::prepareTrackForPlayback (Track& t)
{
    if (! prepared || t.resamplingSource == nullptr || t.reader == nullptr)
        return;

    const double fileRate = t.reader->sampleRate > 0 ? t.reader->sampleRate : deviceSampleRate;
    const double rate     = (double) playbackRate.load();
    t.resamplingSource->setResamplingRatio ((fileRate / deviceSampleRate) * rate);
    t.resamplingSource->prepareToPlay (deviceBlockSize, deviceSampleRate);
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
        t->readerSource = std::make_unique<juce::AudioFormatReaderSource> (t->reader.get(), false);
        t->resamplingSource = std::make_unique<juce::ResamplingAudioSource> (t->readerSource.get(), false, 2);
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
    }

    return true;
}

void MultitrackPlayer::clear()
{
    pause();
    const juce::ScopedLock sl (tracksLock);
    for (auto* t : tracks)
    {
        if (t->resamplingSource != nullptr)
            t->resamplingSource->releaseResources();
    }
    tracks.clear();
    trackInfos.clear();
    playheadSeconds.store (0.0);
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
    r = juce::jlimit (0.25f, 4.0f, r);
    playbackRate.store (r);

    if (! prepared)
        return;

    const juce::ScopedLock sl (tracksLock);
    for (auto* t : tracks)
    {
        if (t->resamplingSource == nullptr || t->reader == nullptr)
            continue;

        const double fileRate = t->reader->sampleRate > 0 ? t->reader->sampleRate : deviceSampleRate;
        t->resamplingSource->setResamplingRatio ((fileRate / deviceSampleRate) * (double) r);
    }
}

float MultitrackPlayer::getPlaybackRate() const noexcept { return playbackRate.load(); }

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
        // Reset the playhead so the first block reads from the right place
        // and the resampler starts with empty state.
        if (t->reader != nullptr)
            setTrackPositionInFileSamples (*t,
                (juce::int64) (playheadSeconds.load() * t->reader->sampleRate));
    }
}

void MultitrackPlayer::releaseResources()
{
    const juce::ScopedLock sl (tracksLock);
    for (auto* t : tracks)
        if (t->resamplingSource != nullptr)
            t->resamplingSource->releaseResources();

    trackBuffer.setSize (0, 0);
    prepared = false;
}

void MultitrackPlayer::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    if (! playing.load())
        return;

    const juce::ScopedLock sl (tracksLock);
    if (tracks.isEmpty())
        return;

    const int numSamples       = info.numSamples;
    const bool soloActive      = anyTrackSoloed();
    const float masterTarget   = masterGain.load();
    const int   outChannels    = info.buffer->getNumChannels();

    if (trackBuffer.getNumSamples() < numSamples)
        trackBuffer.setSize (2, numSamples, false, false, true);

    for (auto* t : tracks)
    {
        if (t->resamplingSource == nullptr)
            continue;

        trackBuffer.clear (0, numSamples);
        juce::AudioSourceChannelInfo localInfo (&trackBuffer, 0, numSamples);
        t->resamplingSource->getNextAudioBlock (localInfo);

        const bool isAudible = soloActive ? t->soloed.load() : ! t->muted.load();
        const float trackTarget = isAudible ? t->gain.load() : 0.0f;

        const float startGain = t->currentGainRamp.load() * currentMasterGainRamp;
        const float endGain   = trackTarget * masterTarget;

        for (int ch = 0; ch < outChannels; ++ch)
        {
            const int srcCh = juce::jmin (ch, trackBuffer.getNumChannels() - 1);
            if (srcCh < 0)
                continue;

            info.buffer->addFromWithRamp (ch,
                                          info.startSample,
                                          trackBuffer.getReadPointer (srcCh, 0),
                                          numSamples,
                                          startGain,
                                          endGain);
        }

        t->currentGainRamp.store (trackTarget);
    }

    currentMasterGainRamp = masterTarget;

    // Advance the master playhead by elapsed song-time (wall-clock scaled
    // by the current playback rate, so the playhead stays in sync with
    // what the resamplers are actually consuming from the source files).
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

void MultitrackPlayer::timerCallback()
{
    if (onPlayheadUpdate)
        onPlayheadUpdate (getCurrentPositionSeconds(), getLengthSeconds());
}

} // namespace ezstemz
