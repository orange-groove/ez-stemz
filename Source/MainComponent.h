#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Project.h"
#include "ProjectsListComponent.h"
#include "SongProcessorComponent.h"

namespace ezstemz
{

/**
    Top-level router.

    Owns the projects-list view and the per-project player view, and swaps
    between them. When the user picks a project we tear down the previous
    player (so its audio device is released cleanly) and bring up a new one.
*/
class MainComponent  : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void showProjectsList();
    void showPlayer (const Project& project);
    void promptForProjectsFolder();
    void handleAddSource (const juce::File& sourceAudio);

    std::unique_ptr<ProjectsListComponent> projectsList;
    std::unique_ptr<SongProcessorComponent> player;

    std::unique_ptr<juce::FileChooser> folderChooser;
};

} // namespace ezstemz
