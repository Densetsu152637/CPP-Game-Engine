#pragma once
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace audio
{
    struct Error { std::string code; std::string message; };
    template<class T> using Result = std::expected<T, Error>;
    enum class Bus { Master, Music, Effects, Dialogue };
    struct Clip { unsigned sampleRate = 0; unsigned channels = 0; std::vector<float> samples; };
    Result<std::shared_ptr<const Clip>> decodeWav(std::span<const std::uint8_t> bytes);
    Result<std::shared_ptr<const Clip>> loadWav(const std::filesystem::path& file);
    using VoiceHandle = std::uint64_t;
    struct PlayOptions { bool loop = false; Bus bus = Bus::Effects; float gain = 1; std::string owner; };
    struct DeviceOptions { bool enablePlayback = false; unsigned sampleRate = 48000; unsigned maxVoices = 64; };
    // Offline mixing and device playback share the same stereo mixer. Control methods
    // are thread-safe. initialize/shutdown belong to the host lifecycle thread.
    class AudioSystem
    {
    public:
        explicit AudioSystem(DeviceOptions options = {});
        ~AudioSystem();
        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;
        Result<void> initialize();
        Result<VoiceHandle> play(std::shared_ptr<const Clip> clip, PlayOptions options = {});
        bool stop(VoiceHandle voice);
        bool pause(VoiceHandle voice);
        bool resume(VoiceHandle voice);
        bool setGain(VoiceHandle voice, float gain);
        void stopOwner(const std::string& owner);
        void stopAll();
        void setBusGain(Bus bus, float gain);
        std::size_t voiceCount() const;
        void mix(std::span<float> stereo);
        void shutdown();
        bool playbackEnabled() const;
        // A backend failure after initialize is visible here; it is never a
        // successful playback result. Empty means no observed device failure.
        std::string deviceError() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
