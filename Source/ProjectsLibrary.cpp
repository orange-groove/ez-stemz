#include "ProjectsLibrary.h"

namespace ezstemz
{
namespace ProjectsLibrary
{

namespace
{

const char* kAudioExtensions = "*.mp3;*.wav;*.flac;*.aiff;*.m4a;*.ogg";

juce::String sanitizeForFilename (const juce::String& src)
{
    auto cleaned = src.retainCharacters (
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    if (cleaned.isEmpty())
        cleaned = "project";
    if (cleaned.length() > 60)
        cleaned = cleaned.substring (0, 60);
    return cleaned;
}

juce::File pickFirstAudioFile (const juce::File& dir)
{
    if (! dir.isDirectory())
        return {};

    auto files = dir.findChildFiles (juce::File::findFiles, false, kAudioExtensions);
    if (files.isEmpty())
        return {};
    return files.getFirst();
}

void writeMetadata (const Project& p)
{
    juce::DynamicObject::Ptr obj (new juce::DynamicObject());
    obj->setProperty ("name", p.name);

    if (p.sourceFile.existsAsFile())
    {
        auto rel = p.sourceFile.getRelativePathFrom (p.dir);
        obj->setProperty ("source_relative_path", rel);
    }

    obj->setProperty ("created_at_ms", (juce::int64) p.createdAt.toMilliseconds());

    p.dir.getChildFile ("project.json")
        .replaceWithText (juce::JSON::toString (juce::var (obj.get()), true));
}

} // namespace

void refreshStems (Project& p)
{
    p.stemFiles.clear();
    p.stemNames.clear();

    auto sd = p.dir.getChildFile ("stems");
    if (! sd.isDirectory())
        return;

    auto wavs = sd.findChildFiles (juce::File::findFiles, false, "*.wav");
    for (auto& f : wavs)
    {
        p.stemFiles.add (f);
        p.stemNames.add (f.getFileNameWithoutExtension());
    }
}

juce::File stemsDir (const Project& p)
{
    auto sd = p.dir.getChildFile ("stems");
    if (! sd.isDirectory())
        sd.createDirectory();
    return sd;
}

Project load (const juce::File& projectDir)
{
    Project p;
    p.dir  = projectDir;
    p.name = projectDir.getFileName();
    p.createdAt = projectDir.getCreationTime();

    auto jsonFile = projectDir.getChildFile ("project.json");
    if (jsonFile.existsAsFile())
    {
        auto parsed = juce::JSON::parse (jsonFile.loadFileAsString());
        if (parsed.isObject())
        {
            auto nameVar = parsed.getProperty ("name", juce::var());
            if (nameVar.isString())
            {
                auto nameStr = nameVar.toString();
                if (nameStr.isNotEmpty())
                    p.name = nameStr;
            }

            auto rel = parsed.getProperty ("source_relative_path", juce::var()).toString();
            if (rel.isNotEmpty())
            {
                auto candidate = projectDir.getChildFile (rel);
                if (candidate.existsAsFile())
                    p.sourceFile = candidate;
            }

            auto createdMs = (juce::int64) parsed.getProperty ("created_at_ms", juce::var ((juce::int64) 0));
            if (createdMs > 0)
                p.createdAt = juce::Time (createdMs);
        }
    }

    if (! p.sourceFile.existsAsFile())
        p.sourceFile = pickFirstAudioFile (projectDir.getChildFile ("source"));

    refreshStems (p);
    return p;
}

juce::Array<Project> scan (const juce::File& projectsFolder)
{
    juce::Array<Project> out;
    if (! projectsFolder.isDirectory())
        return out;

    auto subdirs = projectsFolder.findChildFiles (juce::File::findDirectories, false);
    for (auto& d : subdirs)
    {
        const bool looksLikeProject = d.getChildFile ("project.json").existsAsFile()
                                   || d.getChildFile ("source").isDirectory()
                                   || d.getChildFile ("stems").isDirectory();
        if (! looksLikeProject)
            continue;

        out.add (load (d));
    }

    std::sort (out.begin(), out.end(),
               [] (const Project& a, const Project& b)
               { return a.createdAt.toMilliseconds() > b.createdAt.toMilliseconds(); });
    return out;
}

bool createNew (const juce::File& projectsFolder,
                const juce::File& sourceAudio,
                Project&          out,
                juce::String&     errorOut)
{
    if (! sourceAudio.existsAsFile())
    {
        errorOut = "Source audio file does not exist.";
        return false;
    }

    if (! projectsFolder.isDirectory())
    {
        auto res = projectsFolder.createDirectory();
        if (! res.wasOk())
        {
            errorOut = "Could not create projects folder: " + res.getErrorMessage();
            return false;
        }
    }

    auto baseName = sanitizeForFilename (sourceAudio.getFileNameWithoutExtension());

    juce::File projectDir;
    int suffix = 0;
    do
    {
        const juce::String candidateName = (suffix == 0)
                                            ? baseName
                                            : baseName + "-" + juce::String (suffix);
        projectDir = projectsFolder.getChildFile (candidateName);
        ++suffix;
    } while (projectDir.exists());

    auto res = projectDir.createDirectory();
    if (! res.wasOk())
    {
        errorOut = "Could not create project directory: " + res.getErrorMessage();
        return false;
    }

    auto sourceSubdir = projectDir.getChildFile ("source");
    sourceSubdir.createDirectory();

    auto destSrc = sourceSubdir.getChildFile (sourceAudio.getFileName());
    if (! sourceAudio.copyFileTo (destSrc))
    {
        errorOut = "Could not copy source audio into the project.";
        return false;
    }

    Project p;
    p.dir        = projectDir;
    p.name       = sourceAudio.getFileNameWithoutExtension();
    p.sourceFile = destSrc;
    p.createdAt  = juce::Time::getCurrentTime();

    writeMetadata (p);

    out = std::move (p);
    return true;
}

} // namespace ProjectsLibrary
} // namespace ezstemz
