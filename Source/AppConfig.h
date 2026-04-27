#pragma once

#include <juce_core/juce_core.h>

namespace ezstemz
{

/**
    Resolves runtime paths for the app.

    The demucs model is bundled with the application, so there's nothing for
    the user to configure for inference. The user *does* pick a "projects
    folder" — a directory on disk that holds one subfolder per song / project
    (with its source audio and separated stems). That choice is persisted to
    `~/Library/Application Support/ezstemz/settings.json` (and the
    OS-equivalent locations on other platforms).
*/
class AppConfig
{
public:
    static AppConfig& get();

    // ---- bundled demucs model ----
    juce::File getModelFile() const;
    bool       hasValidModel() const;

    // ---- user-chosen projects folder ----
    juce::File getProjectsFolder() const;
    bool       hasProjectsFolder() const;
    void       setProjectsFolder (const juce::File& folder);

private:
    AppConfig();
    void load();
    void save() const;
    static juce::File getSettingsFile();

    juce::File projectsFolder;
};

} // namespace ezstemz
