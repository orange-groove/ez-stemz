#pragma once

#include "Project.h"

namespace ezstemz
{

/**
    Pure on-disk operations for the projects folder. Stateless – everything
    takes the projects folder explicitly, so it's safe to call from any thread.
*/
namespace ProjectsLibrary
{
    /** Scans `projectsFolder` and returns one Project per subdirectory that
        looks like an EZStemz project (has a project.json or at least a
        source/ or stems/ subdirectory). Newest first. */
    juce::Array<Project> scan (const juce::File& projectsFolder);

    /** Loads a single project from its directory. The returned project is
        always populated (with the directory name as fallback for `name`),
        but `isValid()` will be false if the directory doesn't exist. */
    Project load (const juce::File& projectDir);

    /** Re-reads `<project.dir>/stems/` and refreshes the stem arrays. */
    void refreshStems (Project& p);

    /** Returns `<project.dir>/stems`, creating it if necessary. */
    juce::File stemsDir (const Project& p);

    /** Creates a new project by copying `sourceAudio` into a fresh subfolder
        of `projectsFolder`. On success returns true and fills `out`. */
    bool createNew (const juce::File& projectsFolder,
                    const juce::File& sourceAudio,
                    Project&          out,
                    juce::String&     errorOut);
}

} // namespace ezstemz
