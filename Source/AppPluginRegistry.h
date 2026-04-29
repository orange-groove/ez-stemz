#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ezstemz
{

/**
    App-wide plugin formats + known plugin list.

    On initialise(), loads a cached XML list (if present) then incrementally
    scans default OS plugin locations (VST3, AU on macOS, etc.) on the message
    thread so the UI stays responsive. Results are saved when a scan finishes
    and from shutdownSave().
*/
class AppPluginRegistry  : private juce::Timer
{
public:
    static AppPluginRegistry& get();

    void initialise();
    void shutdownSave();

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList&          getKnownList() noexcept { return knownList; }

    bool isScanning() const noexcept { return scanning; }

private:
    AppPluginRegistry() = default;

    void timerCallback() override;
    void startNextFormatScanner();
    void saveList() const;
    static juce::File getCacheFile();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList          knownList;

    std::unique_ptr<juce::PluginDirectoryScanner> activeScanner;
    int                                            formatIndex = 0;
    bool                                           scanning = false;
};

} // namespace ezstemz
