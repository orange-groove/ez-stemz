#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace ezstemz
{

/** Lets the user pick an installed plug-in from the app-wide KnownPluginList. */
class PluginPickerWindow  : public juce::DocumentWindow
{
public:
    using OnPicked = std::function<void (std::unique_ptr<juce::AudioProcessor>)>;

    PluginPickerWindow (juce::AudioPluginFormatManager& formatManager,
                        juce::KnownPluginList&          knownList,
                        double                          sampleRate,
                        int                             blockSize,
                        OnPicked                        onPicked,
                        std::function<void()>          onCancelled);

    ~PluginPickerWindow() override;

    void closeButtonPressed() override;

    /** Schedules `onCancelled` on the message thread (safe when closing from nested callbacks). */
    void dismissAsync();

    void browseDisk();

    /** Invokes `onPicked` then `dismissAsync()`. */
    void invokePickedAndClose (std::unique_ptr<juce::AudioProcessor> proc);

    juce::AudioPluginFormatManager& getFormatManager() const noexcept { return formatManager; }
    juce::KnownPluginList&          getKnownList() const noexcept { return knownList; }
    double                          getSampleRate() const noexcept { return sampleRate; }
    int                             getBlockSize() const noexcept { return blockSize; }

private:
    class Body;
    class ListModel;

    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList&          knownList;
    const double                    sampleRate;
    const int                       blockSize;
    OnPicked                        onPicked;
    std::function<void()>           onCancelled;

    std::unique_ptr<juce::FileChooser> pendingFileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPickerWindow)
};

} // namespace ezstemz
