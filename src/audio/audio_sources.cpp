#include "audio_sources.h"
#include <atomic>
#include <algorithm>
#include <limits>

namespace audio
{
    namespace
    {
        std::atomic<std::uint64_t> nextHandle{1};
        Result<std::uint64_t> allocateHandle()
        {
            auto next = nextHandle.load();
            while (next < std::uint64_t(std::numeric_limits<std::int64_t>::max()))
                if (nextHandle.compare_exchange_weak(next, next + 1)) return next;
            return std::unexpected(Error{"audio.handle.exhausted", "Audio handle space exhausted"});
        }
        bool validAction(SourceAction action) { return static_cast<unsigned>(action) <= 3; }
    }
    AudioSources::AudioSources(AudioSystem& mixer, std::string owner) : mixer(mixer), owner(std::move(owner)) {}
    AudioSources::~AudioSources() { clear(); }
    Result<SourceHandle> AudioSources::create(std::shared_ptr<const Clip> clip, PlayOptions options)
    {
        options.owner = owner;
        if (const auto valid = validatePlayOptions(options); !valid) return std::unexpected(valid.error());
        if (!clip || clip->samples.empty()) return std::unexpected(Error{"audio.clip.invalid", "Source requires a nonempty clip"});
        if (sources.size() >= 1024) return std::unexpected(Error{"audio.source.limit", "Source limit reached"});
        const auto handle = allocateHandle(); if (!handle) return std::unexpected(handle.error());
        sources.emplace(*handle, Source{std::move(clip), std::move(options)}); return *handle;
    }
    Result<void> AudioSources::configure(SourceHandle handle, PlayOptions options)
    {
        const auto found = sources.find(handle);
        if (found == sources.end()) return std::unexpected(Error{"audio.source.missing", "Unknown source"});
        options.owner = owner;
        if (const auto valid = validatePlayOptions(options); !valid) return valid;
        const auto updated = mixer.configure(found->second.voice, options);
        if (!updated) return std::unexpected(updated.error());
        found->second.options = std::move(options); return {};
    }
    Result<void> AudioSources::control(SourceHandle handle, SourceAction action)
    {
        const auto found = sources.find(handle);
        if (found == sources.end()) return std::unexpected(Error{"audio.source.missing", "Unknown source"});
        if (!validAction(action)) return std::unexpected(Error{"audio.action.invalid", "Unknown source action"});
        auto& source = found->second;
        if (action == SourceAction::Play)
        {
            // Retire the previous voice before allocating so restarting works
            // at the voice cap. Failed playback leaves the source stopped.
            mixer.stop(source.voice); source.voice = 0;
            const auto played = mixer.play(source.clip, source.options);
            if (!played) return std::unexpected(played.error());
            source.voice = *played;
        }
        else if (action == SourceAction::Pause) mixer.pause(source.voice);
        else if (action == SourceAction::Resume) mixer.resume(source.voice);
        else { mixer.stop(source.voice); source.voice = 0; }
        return {};
    }
    bool AudioSources::remove(SourceHandle handle)
    {
        const auto found = sources.find(handle); if (found == sources.end()) return false;
        mixer.stop(found->second.voice); sources.erase(found);
        std::erase_if(observers, [handle](const auto& entry) { return entry.second.source == handle; }); return true;
    }
    VoiceState AudioSources::state(SourceHandle handle) const
    { const auto found = sources.find(handle); return found == sources.end() ? VoiceState::Stopped : mixer.voiceState(found->second.voice); }
    bool AudioSources::contains(SourceHandle handle) const { return sources.contains(handle); }
    Result<ObserverHandle> AudioSources::observe(std::string event, SourceHandle source, SourceAction action)
    {
        if (event.empty() || event.size() > 512 || event.find('\0') != std::string::npos || !validAction(action))
            return std::unexpected(Error{"audio.observer.invalid", "Invalid event name or action"});
        if (!contains(source)) return std::unexpected(Error{"audio.source.missing", "Unknown source"});
        if (observers.size() >= 1024) return std::unexpected(Error{"audio.observer.limit", "Observer limit reached"});
        const auto handle = allocateHandle(); if (!handle) return std::unexpected(handle.error());
        observers.emplace(*handle, Observer{std::move(event), source, action}); return *handle;
    }
    bool AudioSources::unobserve(ObserverHandle handle) { return observers.erase(handle) != 0; }
    Result<std::size_t> AudioSources::notify(std::string_view event)
    {
        if (event.empty() || event.size() > 512 || event.find('\0') != std::string_view::npos)
            return std::unexpected(Error{"audio.observer.invalid", "Invalid event name"});
        std::size_t count = 0;
        for (const auto& [handle, observer] : observers)
            if (observer.event == event)
            {
                const auto result = control(observer.source, observer.action);
                if (!result) return std::unexpected(result.error());
                ++count;
            }
        return count;
    }
    void AudioSources::clear()
    {
        for (const auto& [handle, source] : sources) mixer.stop(source.voice);
        sources.clear(); observers.clear();
    }
}
