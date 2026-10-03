#include "audio.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif
namespace audio
{
    namespace
    {
        constexpr std::size_t MaxWavBytes = 64 * 1024 * 1024;
        float gainValue(float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }
        bool validRate(unsigned rate) { return rate >= 8000 && rate <= 192000; }
        unsigned u16(std::span<const std::uint8_t> b, std::size_t p) { return b[p] | (unsigned(b[p + 1]) << 8); }
        std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t p)
        { return b[p] | (std::uint32_t(b[p + 1]) << 8) | (std::uint32_t(b[p + 2]) << 16) | (std::uint32_t(b[p + 3]) << 24); }
        bool tag(std::span<const std::uint8_t> b, std::size_t p, const char* s)
        { return b[p] == s[0] && b[p + 1] == s[1] && b[p + 2] == s[2] && b[p + 3] == s[3]; }
        Error error(const char* code, const char* message) { return {code, message}; }
    }
    Result<std::shared_ptr<const Clip>> decodeWav(std::span<const std::uint8_t> b)
    {
        if (b.size() < 12 || b.size() > MaxWavBytes || !tag(b, 0, "RIFF") || !tag(b, 8, "WAVE") || u32(b, 4) != b.size() - 8)
            return std::unexpected(error("audio.wav.invalid", "Invalid or truncated bounded RIFF WAVE file"));
        bool haveFormat = false, haveData = false;
        unsigned channels = 0, rate = 0;
        std::size_t dataOffset = 0, dataSize = 0;
        for (std::size_t p = 12; p < b.size();)
        {
            if (b.size() - p < 8) return std::unexpected(error("audio.wav.invalid", "Truncated chunk header"));
            const auto n = std::size_t(u32(b, p + 4));
            const auto begin = p + 8;
            if (n > b.size() - begin || n + (n & 1) > b.size() - begin)
                return std::unexpected(error("audio.wav.invalid", "Truncated chunk data"));
            if (tag(b, p, "fmt "))
            {
                if (haveFormat || n < 16) return std::unexpected(error("audio.wav.invalid", "Invalid format chunk"));
                haveFormat = true; channels = u16(b, begin + 2); rate = u32(b, begin + 4);
                if (u16(b, begin) != 1 || (channels != 1 && channels != 2) || !validRate(rate) || u16(b, begin + 14) != 16)
                    return std::unexpected(error("audio.wav.unsupported", "Only 16-bit PCM mono/stereo at 8-192 kHz is supported"));
                if (u16(b, begin + 12) != channels * 2 || u32(b, begin + 8) != rate * channels * 2)
                    return std::unexpected(error("audio.wav.invalid", "PCM block alignment or byte rate is inconsistent"));
            }
            else if (tag(b, p, "data"))
            {
                if (haveData) return std::unexpected(error("audio.wav.invalid", "Duplicate data chunk"));
                haveData = true; dataOffset = begin; dataSize = n;
            }
            p = begin + n + (n & 1);
        }
        if (!haveFormat || !haveData || !dataSize || dataSize % (channels * 2))
            return std::unexpected(error("audio.wav.invalid", "Missing format/data or partial PCM frame"));
        auto clip = std::make_shared<Clip>(); clip->channels = channels; clip->sampleRate = rate;
        clip->samples.reserve(dataSize / 2);
        for (std::size_t p = dataOffset; p < dataOffset + dataSize; p += 2)
        {
            const auto raw = u16(b, p);
            const int sample = raw >= 32768 ? int(raw) - 65536 : int(raw);
            clip->samples.push_back(float(sample) / 32768.0f);
        }
        return std::shared_ptr<const Clip>(std::move(clip));
    }
    Result<std::shared_ptr<const Clip>> loadWav(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input) return std::unexpected(error("audio.file.unreadable", "Cannot open audio resource"));
        const auto size = input.tellg();
        if (size < 0 || static_cast<std::uint64_t>(size) > MaxWavBytes) return std::unexpected(error("audio.file.size", "Audio resource exceeds size limit"));
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size)); input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return std::unexpected(error("audio.file.unreadable", "Cannot read complete audio resource"));
        return decodeWav(bytes);
    }
    struct AudioSystem::Impl
    {
        struct Voice { VoiceHandle handle; std::shared_ptr<const Clip> clip; PlayOptions options; double position = 0; bool paused = false; };
        DeviceOptions options;
        mutable std::mutex mutex;
        std::vector<Voice> voices;
        std::array<float, 4> gains{1, 1, 1, 1};
        VoiceHandle next = 1;
        bool initialized = false;
        std::atomic<bool> stopping{false};
        std::string failure;
#ifdef _WIN32
        HWAVEOUT device = nullptr;
        std::thread thread;
        struct Buffer { std::array<short, 1024> samples{}; WAVEHDR header{}; bool prepared = false; bool queued = false; };
        std::array<Buffer, 3> buffers;
#endif
        explicit Impl(DeviceOptions value) : options(value) {}
        void mix(std::span<float> output)
        {
            std::lock_guard lock(mutex); std::fill(output.begin(), output.end(), 0.0f);
            for (auto& voice : voices)
            {
                if (voice.paused) continue;
                const auto& clip = *voice.clip; const auto frames = clip.samples.size() / clip.channels;
                const auto gain = voice.options.gain * gains[0] * gains[static_cast<unsigned>(voice.options.bus)];
                const double step = double(clip.sampleRate) / options.sampleRate;
                for (std::size_t frame = 0; frame < output.size() / 2; ++frame)
                {
                    if (voice.position >= frames)
                    {
                        if (!voice.options.loop) break;
                        voice.position = std::fmod(voice.position, double(frames));
                    }
                    const auto first = static_cast<std::size_t>(voice.position);
                    const auto second = first + 1 < frames ? first + 1 : (voice.options.loop ? 0 : first);
                    const auto alpha = float(voice.position - first);
                    for (unsigned channel = 0; channel < 2; ++channel)
                    {
                        const auto source = clip.channels == 1 ? 0 : channel;
                        const auto a = clip.samples[first * clip.channels + source];
                        const auto b = clip.samples[second * clip.channels + source];
                        output[frame * 2 + channel] += (a + (b - a) * alpha) * gain;
                    }
                    voice.position += step;
                }
            }
            std::erase_if(voices, [](const Voice& v) { return !v.options.loop && v.position >= v.clip->samples.size() / v.clip->channels; });
            for (auto& value : output) value = std::clamp(value, -1.0f, 1.0f);
        }
#ifdef _WIN32
        void pump()
        {
            std::array<float, 1024> floating{};
            while (!stopping.load())
            {
                for (auto& buffer : buffers)
                {
                    if (buffer.queued && !(buffer.header.dwFlags & WHDR_DONE)) continue;
                    mix(floating);
                    for (std::size_t i = 0; i < floating.size(); ++i)
                        buffer.samples[i] = static_cast<short>(floating[i] * 32767.0f);
                    const auto status = waveOutWrite(device, &buffer.header, sizeof(WAVEHDR));
                    if (status != MMSYSERR_NOERROR)
                    {
                        std::lock_guard lock(mutex); failure = "waveOutWrite failed: " + std::to_string(status); stopping = true; break;
                    }
                    buffer.queued = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
#endif
    };
    AudioSystem::AudioSystem(DeviceOptions options) : impl(std::make_unique<Impl>(options)) {}
    AudioSystem::~AudioSystem() { shutdown(); }
    Result<void> AudioSystem::initialize()
    {
        if (!validRate(impl->options.sampleRate) || impl->options.maxVoices == 0 || impl->options.maxVoices > 1024)
            return std::unexpected(error("audio.options.invalid", "Invalid output sample rate or voice limit"));
        { std::lock_guard lock(impl->mutex); if (impl->initialized) return {}; impl->failure.clear(); }
        if (impl->options.enablePlayback)
        {
#ifdef _WIN32
            WAVEFORMATEX format{}; format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2;
            format.nSamplesPerSec = impl->options.sampleRate; format.wBitsPerSample = 16; format.nBlockAlign = 4;
            format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
            const auto result = waveOutOpen(&impl->device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
            if (result != MMSYSERR_NOERROR) return std::unexpected(Error{"audio.device.unavailable", "waveOutOpen failed: " + std::to_string(result)});
            for (auto& buffer : impl->buffers)
            {
                buffer.header = {}; buffer.header.lpData = reinterpret_cast<char*>(buffer.samples.data());
                buffer.header.dwBufferLength = static_cast<DWORD>(buffer.samples.size() * sizeof(short));
                const auto prepared = waveOutPrepareHeader(impl->device, &buffer.header, sizeof(WAVEHDR));
                if (prepared != MMSYSERR_NOERROR) { shutdown(); return std::unexpected(error("audio.device.prepare", "waveOutPrepareHeader failed")); }
                buffer.prepared = true; buffer.queued = false;
            }
            impl->stopping = false;
            try { impl->thread = std::thread([this] { impl->pump(); }); }
            catch (...) { shutdown(); return std::unexpected(error("audio.device.thread", "Cannot start playback worker")); }
#else
            return std::unexpected(error("audio.device.unavailable", "Device playback backend is unavailable on this platform"));
#endif
        }
        std::lock_guard lock(impl->mutex); impl->initialized = true; return {};
    }
    Result<VoiceHandle> AudioSystem::play(std::shared_ptr<const Clip> clip, PlayOptions options)
    {
        if (!clip || !validRate(clip->sampleRate) || (clip->channels != 1 && clip->channels != 2) || clip->samples.empty() ||
            clip->samples.size() > MaxWavBytes / 2 || clip->samples.size() % clip->channels ||
            std::any_of(clip->samples.begin(), clip->samples.end(), [](float f) { return !std::isfinite(f) || f < -1 || f > 1; }))
            return std::unexpected(error("audio.clip.invalid", "Invalid PCM clip"));
        if (options.bus == Bus::Master || static_cast<unsigned>(options.bus) > 3 || options.owner.size() > 256)
            return std::unexpected(error("audio.play.invalid", "Voice bus or owner is invalid"));
        std::lock_guard lock(impl->mutex);
        if (!impl->initialized) return std::unexpected(error("audio.not_initialized", "Initialize audio before playback"));
        if (!impl->failure.empty()) return std::unexpected(Error{"audio.device.failed", impl->failure});
        if (impl->voices.size() >= impl->options.maxVoices) return std::unexpected(error("audio.voice.limit", "Concurrent voice limit reached"));
        if (!impl->next) return std::unexpected(error("audio.voice.exhausted", "Voice handle space exhausted"));
        options.gain = gainValue(options.gain); const auto handle = impl->next++;
        impl->voices.push_back({handle, std::move(clip), std::move(options)}); return handle;
    }
    bool AudioSystem::stop(VoiceHandle handle)
    { std::lock_guard lock(impl->mutex); return std::erase_if(impl->voices, [handle](const auto& v) { return v.handle == handle; }) != 0; }
    bool AudioSystem::pause(VoiceHandle handle)
    { std::lock_guard lock(impl->mutex); for (auto& v : impl->voices) if (v.handle == handle) { v.paused = true; return true; } return false; }
    bool AudioSystem::resume(VoiceHandle handle)
    { std::lock_guard lock(impl->mutex); for (auto& v : impl->voices) if (v.handle == handle) { v.paused = false; return true; } return false; }
    bool AudioSystem::setGain(VoiceHandle handle, float gain)
    { std::lock_guard lock(impl->mutex); for (auto& v : impl->voices) if (v.handle == handle) { v.options.gain = gainValue(gain); return true; } return false; }
    void AudioSystem::stopOwner(const std::string& owner)
    { std::lock_guard lock(impl->mutex); std::erase_if(impl->voices, [&](const auto& v) { return v.options.owner == owner; }); }
    void AudioSystem::stopAll() { std::lock_guard lock(impl->mutex); impl->voices.clear(); }
    void AudioSystem::setBusGain(Bus bus, float gain)
    { std::lock_guard lock(impl->mutex); if (static_cast<unsigned>(bus) < 4) impl->gains[static_cast<unsigned>(bus)] = gainValue(gain); }
    std::size_t AudioSystem::voiceCount() const { std::lock_guard lock(impl->mutex); return impl->voices.size(); }
    void AudioSystem::mix(std::span<float> stereo) { impl->mix(stereo); }
    bool AudioSystem::playbackEnabled() const { std::lock_guard lock(impl->mutex); return impl->initialized && impl->options.enablePlayback && impl->failure.empty(); }
    std::string AudioSystem::deviceError() const { std::lock_guard lock(impl->mutex); return impl->failure; }
    void AudioSystem::shutdown()
    {
        impl->stopping = true;
#ifdef _WIN32
        if (impl->thread.joinable()) impl->thread.join();
        if (impl->device)
        {
            const auto reset = waveOutReset(impl->device);
            for (auto& buffer : impl->buffers) if (buffer.prepared)
            { waveOutUnprepareHeader(impl->device, &buffer.header, sizeof(WAVEHDR)); buffer.prepared = false; buffer.queued = false; }
            const auto close = waveOutClose(impl->device); impl->device = nullptr;
            if (reset != MMSYSERR_NOERROR || close != MMSYSERR_NOERROR)
            { std::lock_guard lock(impl->mutex); impl->failure = "waveOut shutdown failed"; }
        }
#endif
        std::lock_guard lock(impl->mutex); impl->voices.clear(); impl->initialized = false;
    }
}
