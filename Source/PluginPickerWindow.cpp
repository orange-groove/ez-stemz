#include "PluginPickerWindow.h"

#include "AppPluginRegistry.h"

namespace ezstemz
{
namespace
{

std::unique_ptr<juce::AudioProcessor> tryCreatePluginFromFile (juce::AudioPluginFormatManager& fm,
                                                                const juce::File&               f,
                                                                double                          sampleRate,
                                                                int                             blockSize,
                                                                juce::String&                   err)
{
    juce::OwnedArray<juce::PluginDescription> types;

    for (auto* format : fm.getFormats())
    {
        if (format->fileMightContainThisPluginType (f.getFullPathName()))
        {
            format->findAllTypesForFile (types, f.getFullPathName());
            if (types.size() > 0)
                break;
        }
    }

    if (types.isEmpty())
    {
        err = "Could not identify a plugin in that file.";
        return nullptr;
    }

    juce::String mferr;
    if (auto inst = fm.createPluginInstance (*types[0], sampleRate, blockSize, mferr))
        return std::unique_ptr<juce::AudioProcessor> (inst.release());

    err = mferr;
    return nullptr;
}

} // namespace

//==============================================================================
class PluginPickerWindow::ListModel  : public juce::ListBoxModel
{
public:
    std::function<void (int row)> onDoubleClickRow;

    void rebuild (const juce::KnownPluginList& known, const juce::String& filter)
    {
        rows.clear();
        const auto all = known.getTypes();
        const auto f = filter.trim().toLowerCase();

        for (const auto& d : all)
        {
            juce::String line = d.manufacturerName + " - " + d.name + " (" + d.pluginFormatName + ")";
            if (f.isEmpty() || line.toLowerCase().contains (f))
                rows.add (d);
        }
    }

    int getNumRows() override { return rows.size(); }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (onDoubleClickRow)
            onDoubleClickRow (row);
    }

    void paintListBoxItem (int rowNumber,
                           juce::Graphics& g,
                           int width,
                           int height,
                           bool rowIsSelected) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, rows.size()))
            return;

        if (rowIsSelected)
            g.fillAll (juce::Colour (0xFF334155));
        else
            g.fillAll (juce::Colour (0xFF111827));

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        const auto& d = rows.getReference (rowNumber);
        g.drawFittedText (d.manufacturerName + " - " + d.name + " (" + d.pluginFormatName + ")",
                          8,
                          0,
                          width - 16,
                          height,
                          juce::Justification::centredLeft,
                          2);
    }

    const juce::PluginDescription* getDescription (int row) const noexcept
    {
        if (juce::isPositiveAndBelow (row, rows.size()))
            return &rows.getReference (row);
        return nullptr;
    }

private:
    juce::Array<juce::PluginDescription> rows;
};

//==============================================================================
class PluginPickerWindow::Body  : public juce::Component,
                                  private juce::ChangeListener
{
public:
    explicit Body (PluginPickerWindow& ownerIn)
        : owner (ownerIn)
    {
        listModel = std::make_unique<ListModel>();
        listModel->onDoubleClickRow = [this] (int row)
        {
            listBox.selectRow (row);
            tryInsertSelection();
        };
        listBox.setModel (listModel.get());
        listBox.setRowHeight (28);
        addAndMakeVisible (listBox);

        searchEditor.setTextToShowWhenEmpty ("Search by name or manufacturer...", juce::Colour (0xFF64748B));
        searchEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xFF1E293B));
        searchEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        searchEditor.onTextChange = [this] { rebuildList(); };
        addAndMakeVisible (searchEditor);

        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF94A3B8));
        statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (statusLabel);

        for (auto* b : { &insertButton, &cancelButton, &browseButton })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF1F2937));
            b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            addAndMakeVisible (*b);
        }

        insertButton.onClick = [this] { tryInsertSelection(); };
        cancelButton.onClick = [this] { owner.closeButtonPressed(); };
        browseButton.onClick = [this] { owner.browseDisk(); };

        owner.getKnownList().addChangeListener (this);
        rebuildList();
    }

    ~Body() override
    {
        owner.getKnownList().removeChangeListener (this);
        listBox.setModel (nullptr);
        listModel.reset();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        statusLabel.setBounds (r.removeFromTop (22));
        r.removeFromTop (6);
        searchEditor.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);

        auto btnRow = r.removeFromBottom (36);
        browseButton.setBounds (btnRow.removeFromLeft (140));
        btnRow.removeFromLeft (8);
        cancelButton.setBounds (btnRow.removeFromRight (90));
        btnRow.removeFromRight (8);
        insertButton.setBounds (btnRow.removeFromRight (100));

        listBox.setBounds (r);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuildList(); }

    void rebuildList()
    {
        listModel->rebuild (owner.getKnownList(), searchEditor.getText());
        listBox.updateContent();
        listBox.repaint();

        statusLabel.setText (juce::String (owner.getKnownList().getNumTypes()) + " plug-ins known"
                                 + (AppPluginRegistry::get().isScanning() ? " (scanning...)" : ""),
                             juce::dontSendNotification);
    }

    void tryInsertSelection()
    {
        const int row = listBox.getSelectedRow();
        const auto* desc = listModel->getDescription (row);
        if (desc == nullptr)
            return;

        juce::String err;
        auto         proc = owner.getFormatManager().createPluginInstance (*desc,
                                                                           owner.getSampleRate(),
                                                                           owner.getBlockSize(),
                                                                           err);

        if (proc == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Plug-in", err);
            return;
        }

        owner.invokePickedAndClose (std::move (proc));
    }

    PluginPickerWindow& owner;

    std::unique_ptr<ListModel> listModel;
    juce::ListBox              listBox;
    juce::TextEditor           searchEditor;
    juce::Label                statusLabel;
    juce::TextButton           insertButton  { "Insert" };
    juce::TextButton           cancelButton  { "Cancel" };
    juce::TextButton           browseButton  { "Browse file..." };
};

//==============================================================================
PluginPickerWindow::PluginPickerWindow (juce::AudioPluginFormatManager& formatManagerIn,
                                        juce::KnownPluginList&          knownListIn,
                                        double                          sampleRateIn,
                                        int                             blockSizeIn,
                                        OnPicked                        onPickedIn,
                                        std::function<void()>          onCancelledIn)
    : DocumentWindow ("Add plug-in",
                        juce::Colour (0xFF0B1220),
                        juce::DocumentWindow::closeButton,
                        true)
    , formatManager (formatManagerIn)
    , knownList (knownListIn)
    , sampleRate (sampleRateIn)
    , blockSize (blockSizeIn)
    , onPicked (std::move (onPickedIn))
    , onCancelled (std::move (onCancelledIn))
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setResizeLimits (420, 360, 1200, 900);
    setContentOwned (new Body (*this), true);
    setSize (520, 480);
    setVisible (true);
}

PluginPickerWindow::~PluginPickerWindow()
{
    pendingFileChooser.reset();
}

void PluginPickerWindow::invokePickedAndClose (std::unique_ptr<juce::AudioProcessor> proc)
{
    if (onPicked)
        onPicked (std::move (proc));

    dismissAsync();
}

void PluginPickerWindow::dismissAsync()
{
    auto dismiss = onCancelled;
    juce::MessageManager::callAsync ([dismiss]
                                     {
                                         if (dismiss)
                                             dismiss();
                                     });
}

void PluginPickerWindow::browseDisk()
{
    pendingFileChooser = std::make_unique<juce::FileChooser> ("Select plug-in file",
                                                              juce::File(),
                                                              "*.vst3;*.component;*.vst");

    pendingFileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                         | juce::FileBrowserComponent::canSelectFiles,
                                     [this] (const juce::FileChooser& fc)
                                     {
                                         auto f = fc.getResult();
                                         pendingFileChooser.reset();

                                         if (f == juce::File())
                                             return;

                                         juce::String err;
                                         auto proc = tryCreatePluginFromFile (formatManager,
                                                                              f,
                                                                              sampleRate,
                                                                              blockSize,
                                                                              err);

                                         if (proc == nullptr)
                                         {
                                             juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                                     "Plug-in",
                                                                                     err);
                                             return;
                                         }

                                         invokePickedAndClose (std::move (proc));
                                     });
}

void PluginPickerWindow::closeButtonPressed()
{
    dismissAsync();
}

} // namespace ezstemz
