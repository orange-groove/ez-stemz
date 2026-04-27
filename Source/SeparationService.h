#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "LocalSeparator.h"
#include "Project.h"

namespace ezstemz
{

/**
    Background separation queue.

    A single worker thread that owns the demucs model and processes a queue
    of projects one at a time. Per-project state is kept so the UI can ask
    "is this project being separated right now? what's its progress?" at any
    moment. State changes (queued, started, progress, finished, error) are
    broadcast on the message thread via juce::ChangeBroadcaster, so any
    listener (the player view, the projects list) can refresh itself.

    Lifetime: this is a process-wide singleton. The first call to `getInstance`
    spins up the worker thread; `shutdown()` joins it on app exit.
*/
class SeparationService  : public juce::ChangeBroadcaster
{
public:
    static SeparationService& getInstance();
    static void shutdown();

    enum class Status
    {
        Idle,       // unknown / not in the system
        Queued,     // accepted, waiting for the worker
        Running,    // currently being processed
        Done,       // finished successfully
        Error       // finished with error
    };

    struct State
    {
        Status       status   = Status::Idle;
        float        progress = 0.0f;       // [0, 1]
        juce::String message;               // human-readable status
        juce::String errorMessage;          // populated when status == Error
    };

    /** Enqueue a project for separation. If it's already queued or running,
        returns immediately without re-queuing. Safe to call from any thread. */
    void enqueue (const Project& project);

    /** Returns the latest state for the given project directory. Returns
        Status::Idle if the service has no record of it. */
    State getState (const juce::File& projectDir) const;

    /** Convenience: true if the project is currently queued or running. */
    bool isProcessing (const juce::File& projectDir) const;

    /** True if any job is queued or running anywhere in the service. */
    bool isAnythingActive() const;

private:
    SeparationService();
    ~SeparationService() override;

    class Worker;
    std::unique_ptr<Worker> worker;
};

} // namespace ezstemz
