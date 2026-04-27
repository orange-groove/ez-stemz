#include "SeparationService.h"

#include "AppConfig.h"
#include "ProjectsLibrary.h"

#include <deque>
#include <map>

namespace ezstemz
{

namespace
{
    juce::String keyFor (const juce::File& dir)
    {
        return dir.getFullPathName();
    }
}

class SeparationService::Worker  : public juce::Thread
{
public:
    Worker (SeparationService& ownerIn)
        : juce::Thread ("ezstemz-separator"),
          owner (ownerIn)
    {
        startThread();
    }

    ~Worker() override
    {
        signalThreadShouldExit();
        {
            const juce::ScopedLock sl (lock);
            queueWaiter.signal();
        }
        stopThread (5000);
    }

    void enqueue (const Project& project)
    {
        const auto k = keyFor (project.dir);
        bool shouldNotify = false;

        {
            const juce::ScopedLock sl (lock);

            auto it = states.find (k);
            if (it != states.end())
            {
                const auto s = it->second.status;
                if (s == Status::Queued || s == Status::Running)
                    return; // already in flight
            }

            queue.push_back (project);

            State next;
            next.status   = Status::Queued;
            next.progress = 0.0f;
            next.message  = "Queued for separation...";
            states[k] = next;

            shouldNotify = true;
            queueWaiter.signal();
        }

        if (shouldNotify)
            postChange();
    }

    State getState (const juce::File& projectDir) const
    {
        const juce::ScopedLock sl (lock);
        auto it = states.find (keyFor (projectDir));
        if (it == states.end())
            return {};
        return it->second;
    }

    bool isAnythingActive() const
    {
        const juce::ScopedLock sl (lock);
        for (auto& kv : states)
            if (kv.second.status == Status::Queued || kv.second.status == Status::Running)
                return true;
        return false;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            Project job;
            bool haveJob = false;

            {
                const juce::ScopedLock sl (lock);
                if (! queue.empty())
                {
                    job = queue.front();
                    queue.pop_front();
                    haveJob = true;

                    State& st = states[keyFor (job.dir)];
                    st.status   = Status::Running;
                    st.progress = 0.0f;
                    st.message  = "Loading model...";
                }
            }

            if (! haveJob)
            {
                queueWaiter.wait (-1);
                continue;
            }

            postChange();
            runJob (job);
        }
    }

    void runJob (Project job)
    {
        const auto k = keyFor (job.dir);

        if (! AppConfig::get().hasValidModel())
        {
            failJob (k, "Bundled demucs model is missing.");
            return;
        }

        if (! separator.isLoaded())
        {
            juce::String err;
            if (! separator.loadModel (AppConfig::get().getModelFile(), err))
            {
                failJob (k, "Could not load model: " + err);
                return;
            }
        }

        if (! job.sourceFile.existsAsFile())
        {
            failJob (k, "Source audio is missing for this project.");
            return;
        }

        auto outputDir = ProjectsLibrary::stemsDir (job);

        auto progressFn = [this, k] (float p, const juce::String& msg)
        {
            updateProgress (k, p, msg);
        };

        auto result = separator.separate (job.sourceFile, outputDir, progressFn);

        {
            const juce::ScopedLock sl (lock);
            State& st = states[k];
            if (result.success)
            {
                st.status   = Status::Done;
                st.progress = 1.0f;
                st.message  = "Ready.";
                st.errorMessage.clear();
            }
            else
            {
                st.status       = Status::Error;
                st.progress     = 0.0f;
                st.message      = "Error.";
                st.errorMessage = result.errorMessage;
            }
        }

        postChange();
    }

    void updateProgress (const juce::String& key, float p, const juce::String& msg)
    {
        bool shouldNotify = false;
        {
            const juce::ScopedLock sl (lock);
            auto it = states.find (key);
            if (it == states.end())
                return;

            const float clamped = juce::jlimit (0.0f, 1.0f, p);
            // Throttle: only notify on perceptible changes.
            if (std::abs (clamped - it->second.progress) >= 0.005f
                || it->second.message != msg)
            {
                it->second.progress = clamped;
                it->second.message  = msg;
                shouldNotify = true;
            }
        }

        if (shouldNotify)
            postChange();
    }

    void failJob (const juce::String& key, const juce::String& errorMessage)
    {
        {
            const juce::ScopedLock sl (lock);
            State& st = states[key];
            st.status       = Status::Error;
            st.progress     = 0.0f;
            st.message      = "Error.";
            st.errorMessage = errorMessage;
        }
        postChange();
    }

    void postChange()
    {
        // ChangeBroadcaster::sendChangeMessage already posts to the message
        // thread, so this is safe to call from the worker thread.
        owner.sendChangeMessage();
    }

    SeparationService& owner;

    juce::CriticalSection lock;
    juce::WaitableEvent   queueWaiter;
    std::deque<Project>   queue;
    std::map<juce::String, State> states;

    LocalSeparator separator;
};

// ---------- service ----------

namespace
{
    SeparationService* gInstance = nullptr;
}

SeparationService& SeparationService::getInstance()
{
    if (gInstance == nullptr)
        gInstance = new SeparationService();
    return *gInstance;
}

void SeparationService::shutdown()
{
    delete gInstance;
    gInstance = nullptr;
}

SeparationService::SeparationService()
{
    worker = std::make_unique<Worker> (*this);
}

SeparationService::~SeparationService() = default;

void SeparationService::enqueue (const Project& project)
{
    worker->enqueue (project);
}

SeparationService::State SeparationService::getState (const juce::File& projectDir) const
{
    return worker->getState (projectDir);
}

bool SeparationService::isProcessing (const juce::File& projectDir) const
{
    auto s = getState (projectDir).status;
    return s == Status::Queued || s == Status::Running;
}

bool SeparationService::isAnythingActive() const
{
    return worker->isAnythingActive();
}

} // namespace ezstemz
