#include "AppConfig.h"

#ifndef EZSTEMZ_BUNDLED_MODEL_NAME
 #define EZSTEMZ_BUNDLED_MODEL_NAME "ggml-model-htdemucs-6s-f16.bin"
#endif

namespace ezstemz
{

AppConfig& AppConfig::get()
{
    static AppConfig instance;
    return instance;
}

AppConfig::AppConfig()
{
    load();
}

juce::File AppConfig::getModelFile() const
{
    const auto appFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile);

   #if JUCE_MAC
    return appFile.getChildFile ("Contents/Resources/" EZSTEMZ_BUNDLED_MODEL_NAME);
   #else
    return appFile.getSiblingFile (EZSTEMZ_BUNDLED_MODEL_NAME);
   #endif
}

bool AppConfig::hasValidModel() const
{
    return getModelFile().existsAsFile();
}

juce::File AppConfig::getProjectsFolder() const
{
    return projectsFolder;
}

bool AppConfig::hasProjectsFolder() const
{
    return projectsFolder.isDirectory();
}

void AppConfig::setProjectsFolder (const juce::File& folder)
{
    projectsFolder = folder;
    save();
}

juce::File AppConfig::getSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("ezstemz")
        .getChildFile ("settings.json");
}

void AppConfig::load()
{
    auto f = getSettingsFile();
    if (! f.existsAsFile())
        return;

    auto parsed = juce::JSON::parse (f.loadFileAsString());
    if (! parsed.isObject())
        return;

    auto pf = parsed.getProperty ("projects_folder", juce::var()).toString();
    if (pf.isNotEmpty())
        projectsFolder = juce::File (pf);
}

void AppConfig::save() const
{
    auto f = getSettingsFile();
    f.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty ("projects_folder", projectsFolder.getFullPathName());
    f.replaceWithText (juce::JSON::toString (juce::var (obj.get()), true));
}

} // namespace ezstemz
