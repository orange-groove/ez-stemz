#include "ProjectsListComponent.h"

#include "AppConfig.h"
#include "ProjectsLibrary.h"
#include "SeparationService.h"

namespace ezstemz
{

class ProjectsListComponent::Row  : public juce::Component
{
public:
    Row (const Project& p) : project (p)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setLiveStatus (SeparationService::Status s, float p, const juce::String& msg)
    {
        const bool changed = (s != liveStatus)
                          || ! juce::approximatelyEqual (p, liveProgress)
                          || (msg != liveMessage);
        liveStatus   = s;
        liveProgress = p;
        liveMessage  = msg;
        if (changed)
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bg = hover ? juce::Colour (0xFF1F2937) : juce::Colour (0xFF111827);
        g.fillAll (bg);

        g.setColour (juce::Colour (0xFF1E293B));
        g.fillRect (getLocalBounds().removeFromBottom (1));

        auto r = getLocalBounds().reduced (16, 10);

        // ---- status pill on the right ----
        using Status = SeparationService::Status;

        struct PillStyle { juce::String text; juce::Colour bg; juce::Colour fg; int width; };
        PillStyle style;

        if (liveStatus == Status::Queued)
            style = { "QUEUED", juce::Colour (0xFF334155), juce::Colour (0xFFCBD5E1), 90 };
        else if (liveStatus == Status::Running)
            style = { "PROCESSING " + juce::String ((int) (liveProgress * 100.0f)) + "%",
                       juce::Colour (0xFF1D4ED8), juce::Colour (0xFFDBEAFE), 150 };
        else if (liveStatus == Status::Error)
            style = { "ERROR", juce::Colour (0xFF7F1D1D), juce::Colour (0xFFFECACA), 80 };
        else if (project.hasStems() || liveStatus == Status::Done)
            style = { "READY", juce::Colour (0xFF166534), juce::Colour (0xFFBBF7D0), 80 };
        else
            style = { "NOT SEPARATED", juce::Colour (0xFF374151), juce::Colour (0xFFCBD5E1), 130 };

        g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
        auto pillRect = r.removeFromRight (style.width).withSizeKeepingCentre (style.width, 22);
        g.setColour (style.bg);
        g.fillRoundedRectangle (pillRect.toFloat(), 11.0f);
        g.setColour (style.fg);
        g.drawFittedText (style.text, pillRect, juce::Justification::centred, 1);

        r.removeFromRight (12);

        // ---- name + meta ----
        auto nameArea = r.removeFromTop (r.getHeight() / 2);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
        g.drawFittedText (project.name, nameArea, juce::Justification::centredLeft, 1);

        juce::String meta;
        if (liveStatus == Status::Running && liveMessage.isNotEmpty())
        {
            meta = liveMessage;
        }
        else
        {
            if (project.hasSource())
                meta << project.sourceFile.getFileName();
            if (project.createdAt.toMilliseconds() > 0)
            {
                if (meta.isNotEmpty())
                    meta << "   •   ";
                meta << project.createdAt.formatted ("%b %d, %Y %H:%M");
            }
            if (project.hasStems())
            {
                if (meta.isNotEmpty())
                    meta << "   •   ";
                meta << project.stemFiles.size() << " stems";
            }
        }

        g.setColour (juce::Colour (0xFF94A3B8));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText (meta, r, juce::Justification::centredLeft, 1);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick) onClick();
    }

    Project project;
    SeparationService::Status liveStatus = SeparationService::Status::Idle;
    float liveProgress = 0.0f;
    juce::String liveMessage;
    bool hover = false;
    std::function<void()> onClick;
};

ProjectsListComponent::ProjectsListComponent()
{
    headerLabel.setText ("Your Projects", juce::dontSendNotification);
    headerLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    headerLabel.setFont (juce::Font (juce::FontOptions (22.0f).withStyle ("Bold")));
    addAndMakeVisible (headerLabel);

    folderLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF94A3B8));
    folderLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    folderLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (folderLabel);

    chooseFolderButton.onClick = [this]
    {
        if (onChooseFolder) onChooseFolder();
    };
    addAndMakeVisible (chooseFolderButton);

    addProjectButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF6366F1));
    addProjectButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    addProjectButton.onClick = [this]
    {
        currentChooser = std::make_unique<juce::FileChooser> (
            "Choose an audio file to import",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.mp3;*.wav;*.flac;*.aiff;*.m4a;*.ogg");

        currentChooser->launchAsync (juce::FileBrowserComponent::openMode
                                     | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile() && onAddSourceFile)
                    onAddSourceFile (file);
            });
    };
    addAndMakeVisible (addProjectButton);

    emptyLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF64748B));
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
    addAndMakeVisible (emptyLabel);

    listContainer = std::make_unique<juce::Component>();
    listViewport.setViewedComponent (listContainer.get(), false);
    listViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (listViewport);

    SeparationService::getInstance().addChangeListener (this);
    refresh();
}

ProjectsListComponent::~ProjectsListComponent()
{
    SeparationService::getInstance().removeChangeListener (this);
}

void ProjectsListComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A project just transitioned. Refresh statuses; if any project finished,
    // also refresh the on-disk metadata so its row picks up the new stems.
    auto& service = SeparationService::getInstance();
    bool needsRescan = false;
    for (auto& p : projects)
    {
        if (service.getState (p.dir).status == SeparationService::Status::Done
            && ! p.hasStems())
        {
            needsRescan = true;
            break;
        }
    }

    if (needsRescan)
        refresh();
    else
        refreshRowStatuses();
}

void ProjectsListComponent::refreshRowStatuses()
{
    auto& service = SeparationService::getInstance();
    for (int i = 0; i < rows.size() && i < projects.size(); ++i)
    {
        auto state = service.getState (projects.getReference (i).dir);
        rows[i]->setLiveStatus (state.status, state.progress, state.message);
    }
}

void ProjectsListComponent::refresh()
{
    auto& cfg = AppConfig::get();

    if (! cfg.hasProjectsFolder())
    {
        folderLabel.setText ("No projects folder selected.", juce::dontSendNotification);
        folderLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFF87171));
        addProjectButton.setEnabled (false);
        emptyLabel.setText ("Pick a projects folder to get started.\n"
                            "EZStemz will create one subfolder per song.",
                            juce::dontSendNotification);
        projects.clear();
    }
    else
    {
        folderLabel.setText (cfg.getProjectsFolder().getFullPathName(),
                             juce::dontSendNotification);
        folderLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF94A3B8));
        addProjectButton.setEnabled (true);
        projects = ProjectsLibrary::scan (cfg.getProjectsFolder());

        if (projects.isEmpty())
            emptyLabel.setText ("No projects yet. Click \"+ New project\" to import one.",
                                juce::dontSendNotification);
        else
            emptyLabel.setText ({}, juce::dontSendNotification);
    }

    rebuildRows();
    refreshRowStatuses();
    resized();
    repaint();
}

void ProjectsListComponent::rebuildRows()
{
    rows.clear();
    for (auto& p : projects)
    {
        auto* r = new Row (p);
        r->onClick = [this, captured = p]
        {
            if (onProjectSelected) onProjectSelected (captured);
        };
        listContainer->addAndMakeVisible (r);
        rows.add (r);
    }
}

void ProjectsListComponent::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop (60).reduced (16, 10);
    headerLabel.setBounds (header.removeFromLeft (260));
    chooseFolderButton.setBounds (header.removeFromRight (140));
    header.removeFromRight (8);
    folderLabel.setBounds (header);

    auto actions = r.removeFromTop (44).reduced (16, 6);
    addProjectButton.setBounds (actions.removeFromLeft (160));

    auto listArea = r.reduced (16, 6);
    listViewport.setBounds (listArea);

    const int rowH = 64;
    const int contentW = listViewport.getMaximumVisibleWidth();
    listContainer->setBounds (0, 0, contentW, juce::jmax (rowH, rows.size() * rowH));

    int y = 0;
    for (auto* row : rows)
    {
        row->setBounds (0, y, contentW, rowH);
        y += rowH;
    }

    if (rows.isEmpty())
        emptyLabel.setBounds (listArea);
    else
        emptyLabel.setBounds ({});
}

void ProjectsListComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xFF0B1220));

    g.setColour (juce::Colour (0xFF1E293B));
    g.drawHorizontalLine (60, 0.0f, (float) getWidth());
}

} // namespace ezstemz
