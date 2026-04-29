#include "AppPluginRegistry.h"

namespace ezstemz
{

AppPluginRegistry& AppPluginRegistry::get()
{
    static AppPluginRegistry instance;
    return instance;
}

juce::File AppPluginRegistry::getCacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("ezstemz")
        .getChildFile ("known_plugins.xml");
}

void AppPluginRegistry::initialise()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    formatManager.addDefaultFormats();

    if (auto f = getCacheFile(); f.existsAsFile())
        if (auto xml = juce::parseXML (f))
            knownList.recreateFromXml (*xml);

    formatIndex   = 0;
    scanning      = true;
    activeScanner.reset();

    startNextFormatScanner();

    if (scanning)
        startTimerHz (25);
}

void AppPluginRegistry::startNextFormatScanner()
{
    activeScanner.reset();

    while (formatIndex < formatManager.getNumFormats())
    {
        auto* fmt = formatManager.getFormat (formatIndex);
        if (fmt == nullptr)
        {
            ++formatIndex;
            continue;
        }

        juce::FileSearchPath path (fmt->getDefaultLocationsToSearch());
        if (path.getNumPaths() == 0)
        {
            ++formatIndex;
            continue;
        }

        activeScanner = std::make_unique<juce::PluginDirectoryScanner> (
            knownList,
            *fmt,
            path,
            true,
            juce::File());
        return;
    }

    scanning = false;
    stopTimer();
    saveList();
    knownList.sendChangeMessage();
}

void AppPluginRegistry::timerCallback()
{
    if (! scanning)
        return;

    if (activeScanner != nullptr)
    {
        juce::String nameTried;

        for (int step = 0; step < 12; ++step)
        {
            if (! activeScanner->scanNextFile (true, nameTried))
            {
                activeScanner.reset();
                ++formatIndex;
                startNextFormatScanner();
                return;
            }
        }

        knownList.sendChangeMessage();
        return;
    }

    startNextFormatScanner();
}

void AppPluginRegistry::saveList() const
{
    auto f = getCacheFile();
    f.getParentDirectory().createDirectory();

    if (auto xml = knownList.createXml())
        xml->writeTo (f, {});
}

void AppPluginRegistry::shutdownSave()
{
    stopTimer();
    saveList();
}

} // namespace ezstemz
