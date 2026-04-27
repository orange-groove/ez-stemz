#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Project.h"

namespace ezstemz
{

/**
    The "library" screen.

    Shows the list of projects in `AppConfig::getProjectsFolder()`. From here
    the user can:
        - click a project to open the multitrack player
        - import a new audio file (creates a new project)
        - choose a different projects folder
*/
class ProjectsListComponent  : public juce::Component,
                               public juce::ChangeListener
{
public:
    ProjectsListComponent();
    ~ProjectsListComponent() override;

    /** Re-scans the projects folder and rebuilds the row list. */
    void refresh();

    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void (const Project&)> onProjectSelected;
    std::function<void (const juce::File&)> onAddSourceFile;
    std::function<void()> onChooseFolder;

private:
    class Row;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildRows();
    void refreshRowStatuses();

    juce::Label  headerLabel;
    juce::Label  folderLabel;
    juce::Label  emptyLabel;
    juce::TextButton chooseFolderButton { "Change folder..." };
    juce::TextButton addProjectButton   { "+ New project" };

    juce::Viewport               listViewport;
    std::unique_ptr<juce::Component> listContainer;

    juce::OwnedArray<Row> rows;
    juce::Array<Project>  projects;

    std::unique_ptr<juce::FileChooser> currentChooser;
};

} // namespace ezstemz
