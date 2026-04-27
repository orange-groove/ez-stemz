#include <juce_gui_basics/juce_gui_basics.h>

#include "AppConfig.h"
#include "MainComponent.h"
#include "SeparationService.h"

namespace ezstemz
{

class EzStemzApplication  : public juce::JUCEApplication
{
public:
    EzStemzApplication() = default;

    const juce::String getApplicationName() override       { return "EZStemz"; }
    const juce::String getApplicationVersion() override    { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        AppConfig::get(); // load saved settings
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        SeparationService::shutdown();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

    class MainWindow  : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Colour (0xFF0B1220),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, true);
            setContentOwned (new MainComponent(), true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace ezstemz

START_JUCE_APPLICATION (ezstemz::EzStemzApplication)
