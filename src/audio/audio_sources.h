#pragma once
#include "audio.h"
#include <map>
#include <string_view>

namespace audio
{
    using SourceHandle = std::uint64_t;
    using ObserverHandle = std::uint64_t;
    enum class SourceAction { Play, Pause, Resume, Stop };
    // Lifecycle-thread controller. One voice per reusable source; play restarts
    // it. Named notifications execute subscriptions in registration order.
    // The mixer must outlive this controller. Destruction stops owned sources.
    class AudioSources
    {
    public:
        explicit AudioSources(AudioSystem& mixer, std::string owner);
        ~AudioSources();
        AudioSources(const AudioSources&) = delete;
        AudioSources& operator=(const AudioSources&) = delete;
        Result<SourceHandle> create(std::shared_ptr<const Clip> clip, PlayOptions options = {});
        Result<void> configure(SourceHandle source, PlayOptions options);
        Result<void> control(SourceHandle source, SourceAction action);
        bool remove(SourceHandle source);
        VoiceState state(SourceHandle source) const;
        bool contains(SourceHandle source) const;
        Result<ObserverHandle> observe(std::string event, SourceHandle source, SourceAction action);
        bool unobserve(ObserverHandle observer);
        // Unknown events are a successful no-op. On failure earlier observers
        // may have run; callers must not treat notifications as transactional.
        Result<std::size_t> notify(std::string_view event);
        void clear();
    private:
        struct Source { std::shared_ptr<const Clip> clip; PlayOptions options; VoiceHandle voice = 0; };
        struct Observer { std::string event; SourceHandle source; SourceAction action; };
        AudioSystem& mixer;
        std::string owner;
        std::map<SourceHandle, Source> sources;
        std::map<ObserverHandle, Observer> observers;
    };
}
