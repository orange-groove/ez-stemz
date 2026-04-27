#pragma once

#include <juce_core/juce_core.h>

namespace ezstemz
{

/**
    On-disk representation of a single project.

    Layout inside `dir`:
        project.json          – metadata (name, source filename, created_at_ms)
        source/<filename>.ext – the original imported audio file
        stems/<stem>.wav      – one WAV per separated stem (created after
                                separation; the project is "ready to play"
                                once this directory has files in it)
*/
struct Project
{
    juce::File   dir;                   // absolute project directory
    juce::String name;                  // user-facing display name
    juce::File   sourceFile;            // copy of the original audio inside the project
    juce::Time   createdAt;             // when the project was first imported

    juce::Array<juce::File> stemFiles;  // discovered stem WAVs
    juce::StringArray       stemNames;  // parallel: filename-without-extension

    bool isValid()    const noexcept { return dir.isDirectory(); }
    bool hasStems()   const noexcept { return ! stemFiles.isEmpty(); }
    bool hasSource()  const noexcept { return sourceFile.existsAsFile(); }
};

} // namespace ezstemz
