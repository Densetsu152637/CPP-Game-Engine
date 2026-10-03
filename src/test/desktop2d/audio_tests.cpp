#include "audio/audio.h"
#include "test/test_assertions.h"
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
namespace
{
    void near(float actual, float expected, const char* message) { test::require(std::abs(actual - expected) < 0.0001f, message); }
    std::vector<std::uint8_t> wav()
    {
        return {'R','I','F','F', 40,0,0,0, 'W','A','V','E', 'f','m','t',' ',16,0,0,0,
            1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0x40,0,0xc0};
    }
    void decoderRejectsTruncationAndUnsupportedFormats()
    {
        auto bytes = wav(); auto decoded = audio::decodeWav(bytes);
        test::require(decoded.has_value(), "PCM WAV should decode");
        test::require((*decoded)->channels == 1 && (*decoded)->sampleRate == 48000, "WAV format metadata");
        near((*decoded)->samples[0], .5f, "PCM positive sample"); near((*decoded)->samples[1], -.5f, "PCM negative sample");
        bytes.pop_back(); test::require(!audio::decodeWav(bytes), "truncated WAV rejected");
        bytes = wav(); bytes[20] = 3; auto unsupported = audio::decodeWav(bytes);
        test::require(!unsupported && unsupported.error().code == "audio.wav.unsupported", "float WAV rejected explicitly");
        bytes = wav(); bytes[32] = 4; test::require(!audio::decodeWav(bytes), "wrong block alignment rejected");
        bytes = wav(); bytes[44] = 0xff; bytes[45] = 0x7f;
        near((*audio::decodeWav(bytes))->samples[0], 32767.0f / 32768, "PCM signed maximum");
        test::require(!audio::loadWav("missing-service-audio.wav"), "missing WAV fails explicitly");
    }
    void voicesMixLoopPauseLimitAndCleanup()
    {
        const auto clip = *audio::decodeWav(wav()); audio::AudioSystem system({false, 48000, 2});
        test::require(!system.play(clip), "play before initialize rejected"); test::require(system.initialize().has_value(), "offline initialize");
        const auto one = system.play(clip); test::require(one.has_value(), "one shot voice");
        std::array<float, 6> buffer{}; system.mix(buffer);
        near(buffer[0], .5f, "mono duplicated left"); near(buffer[1], .5f, "mono duplicated right"); near(buffer[2], -.5f, "one shot second sample"); near(buffer[4], 0, "one shot completes with silence");
        test::require(system.voiceCount() == 0 && !system.stop(*one), "completed handle removed");
        const auto loop = system.play(clip, {true, audio::Bus::Music, .5f, "scene:a"});
        test::require(loop.has_value() && system.pause(*loop), "loop pause"); system.mix(buffer); near(buffer[0], 0, "paused silence");
        test::require(system.resume(*loop), "loop resume"); system.setBusGain(audio::Bus::Music, .5f); system.setBusGain(audio::Bus::Master, .5f);
        system.mix(buffer); near(buffer[0], .0625f, "master bus and voice gains multiply"); near(buffer[4], .0625f, "loop wraps continuously");
        system.setBusGain(audio::Bus::Master, 0); system.mix(buffer); near(buffer[0], 0, "mute");
        system.setBusGain(audio::Bus::Master, 99); system.setBusGain(audio::Bus::Music, 1);
        test::require(system.setGain(*loop, std::numeric_limits<float>::quiet_NaN()), "set gain NaN is handled"); system.mix(buffer); near(buffer[0], 0, "NaN gain clamps to silence");
        system.setGain(*loop, 1); const auto other = system.play(clip, {true, audio::Bus::Effects, 1, "scene:b"});
        test::require(other.has_value(), "second voice allowed"); auto rejected = system.play(clip);
        test::require(!rejected && rejected.error().code == "audio.voice.limit", "voice cap rejects cue spam");
        system.stopOwner("scene:a"); test::require(system.voiceCount() == 1 && !system.pause(*loop), "scene ownership cleanup");
        system.stopAll(); test::require(system.voiceCount() == 0, "all voices stopped");
        auto slow = std::make_shared<audio::Clip>(); slow->sampleRate = 24000; slow->channels = 1; slow->samples = {0, 1};
        test::require(system.play(slow).has_value(), "resampled voice"); std::array<float, 8> resampled{}; system.mix(resampled);
        near(resampled[0], 0, "resampling first"); near(resampled[2], .5f, "linear rate interpolation"); near(resampled[4], 1, "resampling second");
        system.shutdown(); test::require(system.voiceCount() == 0 && !system.play(clip), "shutdown drops voices and rejects playback");
        audio::AudioSystem bad({false, 0, 1}); test::require(!bad.initialize(), "invalid output rate rejected");
    }
    void actualDeviceOpenPlayStop()
    {
        audio::AudioSystem system({true, 48000, 4}); const auto initialized = system.initialize();
        if (!initialized)
        {
            std::cout << "[DEVICE UNAVAILABLE] " << initialized.error().code << ": " << initialized.error().message << '\n';
            test::require(!system.play(*audio::decodeWav(wav())), "unavailable device cannot claim playback"); return;
        }
        auto tone = std::make_shared<audio::Clip>(); tone->sampleRate = 48000; tone->channels = 1;
        for (unsigned n = 0; n < 4800; ++n) tone->samples.push_back(.02f * std::sin(float(n) * 440 * 6.2831853f / 48000));
        const auto voice = system.play(tone, {true, audio::Bus::Effects, 1, "device-test"});
        test::require(voice.has_value() && system.playbackEnabled(), "device backend opens and accepts actual audio");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        test::require(system.deviceError().empty(), "no device write failure"); test::require(system.stop(*voice), "device loop stopped");
        system.shutdown(); test::require(system.deviceError().empty(), "device closes without reported error");
        std::cout << "[DEVICE PASS] native device opened; PCM queued for 100 ms; voice stopped; device reset/closed (audibility unverified)\n";
    }
}
void runAudioTests()
{ decoderRejectsTruncationAndUnsupportedFormats(); voicesMixLoopPauseLimitAndCleanup(); actualDeviceOpenPlayStop(); }
#ifdef CPP_GAME_ENGINE_SERVICE_TEST_MAIN
int main() { try { runAudioTests(); std::cout << "[PASS] audio service tests\n"; return 0; } catch (const std::exception& e) { std::cerr << "[FAIL] " << e.what() << '\n'; return 1; } }
#endif
