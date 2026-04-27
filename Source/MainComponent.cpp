#include "MainComponent.h"

#include "AppConfig.h"
#include "ProjectsLibrary.h"
#include "SeparationService.h"

namespace ezstemz
{

MainComponent::MainComponent()
{
    showProjectsList();
    setSize (1100, 720);
}

MainComponent::~MainComponent() = default;

void MainComponent::resized()
{
    auto r = getLocalBounds();

    if (projectsList != nullptr)
        projectsList->setBounds (r);

    if (player != nullptr)
        player->setBounds (r);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF0B1220));
}

void MainComponent::showProjectsList()
{
    player.reset();

    if (projectsList == nullptr)
    {
        projectsList = std::make_unique<ProjectsListComponent>();
        projectsList->onProjectSelected = [this] (const Project& p) { showPlayer (p); };
        projectsList->onChooseFolder    = [this] { promptForProjectsFolder(); };
        projectsList->onAddSourceFile   = [this] (const juce::File& f) { handleAddSource (f); };
        addAndMakeVisible (*projectsList);
    }
    else
    {
        projectsList->refresh();
        projectsList->setVisible (true);
    }

    if (! AppConfig::get().hasProjectsFolder())
        promptForProjectsFolder();

    resized();
}

void MainComponent::showPlayer (const Project& projectToOpen)
{
    if (projectsList != nullptr)
        projectsList->setVisible (false);

    player = std::make_unique<SongProcessorComponent> (projectToOpen);
    player->onBackPressed = [this] { showProjectsList(); };
    addAndMakeVisible (*player);
    resized();
}

void MainComponent::promptForProjectsFolder()
{
    auto& cfg = AppConfig::get();
    auto startDir = cfg.hasProjectsFolder()
                        ? cfg.getProjectsFolder()
                        : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    folderChooser = std::make_unique<juce::FileChooser> (
        "Choose your EZStemz projects folder", startDir);

    folderChooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File())
                return;

            if (! result.isDirectory())
                result.createDirectory();

            AppConfig::get().setProjectsFolder (result);

            if (projectsList != nullptr)
                projectsList->refresh();
        });
}

void MainComponent::handleAddSource (const juce::File& sourceAudio)
{
    auto& cfg = AppConfig::get();
    if (! cfg.hasProjectsFolder())
    {
        promptForProjectsFolder();
        return;
    }

    Project p;
    juce::String err;
    if (! ProjectsLibrary::createNew (cfg.getProjectsFolder(), sourceAudio, p, err))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Could not import audio",
                                                err);
        return;
    }

    // Kick off background separation immediately so progress is visible
    // whether the user stays on the player view or hops back to the list.
    SeparationService::getInstance().enqueue (p);

    if (projectsList != nullptr)
        projectsList->refresh();

    showPlayer (p);
}

} // namespace ezstemz
