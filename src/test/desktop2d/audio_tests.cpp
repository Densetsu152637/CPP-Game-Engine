#include "audio/audio.h"
#include "audio/audio_sources.h"
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
    void spatialSourcesFollowListenerAndAttenuate()
    {
        audio::AudioSystem system; test::require(system.initialize().has_value(), "spatial offline initialize");
        auto clip = std::make_shared<audio::Clip>(); clip->sampleRate = 48000; clip->channels = 1; clip->samples = {1,1,1,1};
        audio::PlayOptions options; options.loop = true; options.spatial.enabled = true;
        options.spatial.position = {1,0,0};
        const auto voice = system.play(clip, options); test::require(voice.has_value(), "spatial voice created");
        std::array<float,2> frame{}; system.mix(frame); near(frame[0], 0, "right emitter silent left"); near(frame[1], 1, "right emitter full right");
        options.spatial.position = {-2,0,0}; test::require(system.configure(*voice, options).has_value(), "moving source configuration");
        system.mix(frame); near(frame[0], .5f, "inverse distance gain"); near(frame[1], 0, "left emitter silent right");
        audio::Listener listener; listener.forward = {0,0,1}; test::require(system.setListener(listener).has_value(), "listener rotation");
        system.mix(frame); near(frame[0], 0, "rotation swaps left"); near(frame[1], .5f, "rotation swaps right");
        listener.position = {-2,0,0}; test::require(system.setListener(listener).has_value(), "listener translation");
        system.mix(frame); near(frame[0], std::sqrt(.5f), "coincident source centered"); near(frame[1], frame[0], "coincident stereo balance");
        options.spatial.attenuation = audio::Attenuation::Linear; options.spatial.minDistance = 1; options.spatial.maxDistance = 3;
        options.spatial.position = {0,0,0}; test::require(system.configure(*voice, options).has_value(), "linear configuration");
        system.mix(frame); near(frame[0], .5f, "linear attenuation at midpoint");
        options.spatial.position = {1,0,0}; test::require(system.configure(*voice, options).has_value(), "max distance configuration");
        system.mix(frame); near(frame[0], 0, "max distance silence"); near(frame[1], 0, "max distance silence right");
        options.spatial.attenuation = audio::Attenuation::None; test::require(system.configure(*voice, options).has_value(), "distance disabled");
        system.mix(frame); near(frame[0], 1, "no attenuation beyond max");
        options.pitch = 0; test::require(!system.configure(*voice, options), "invalid pitch rejected atomically");
        options.pitch = 1; options.spatial.maxDistance = 0; test::require(!system.play(clip, options), "invalid distances rejected");
        listener.up = listener.forward; test::require(!system.setListener(listener), "parallel listener rejected");
        listener = {}; listener.position[0] = std::numeric_limits<float>::infinity(); test::require(!system.setListener(listener), "nonfinite listener rejected");
        system.stopAll();
        clip->samples = {0,.25f,.5f,.75f}; options = {}; options.pitch = 2;
        test::require(system.play(clip, options).has_value(), "pitch voice"); std::array<float,6> pitched{}; system.mix(pitched);
        near(pitched[2], .5f, "pitch changes cursor step"); near(pitched[4], 0, "pitched one shot completes");
        auto stereo = std::make_shared<audio::Clip>(); stereo->sampleRate = 48000; stereo->channels = 2; stereo->samples = {1,0};
        options = {}; options.spatial.enabled = true; options.spatial.position = {1,0,0};
        test::require(system.setListener({}).has_value() && system.play(stereo, options).has_value(), "spatial stereo voice");
        system.mix(frame); near(frame[0], 0, "spatial stereo panned left"); near(frame[1], .5f, "stereo downmixed to emitter");
    }
    void reusableSourcesObserveEventsAndReleaseOwnership()
    {
        audio::AudioSystem mixer({false,48000,1}); test::require(mixer.initialize().has_value(), "source mixer initialized");
        audio::SourceHandle stale = 0;
        {
            audio::AudioSources sources(mixer, "room"); audio::PlayOptions options; options.loop = true;
            const auto source = sources.create(*audio::decodeWav(wav()), options); test::require(source.has_value(), "source configured without playback");
            stale = *source; test::require(mixer.voiceCount() == 0, "creation does not play");
            const auto observer = sources.observe("door", *source, audio::SourceAction::Play);
            test::require(observer.has_value() && *sources.notify("door") == 1, "observer plays source");
            test::require(sources.state(*source) == audio::VoiceState::Playing && mixer.voiceCount() == 1, "source reports playing");
            test::require(sources.notify("door").has_value() && mixer.voiceCount() == 1, "repeated play restarts at cap");
            test::require(sources.control(*source, audio::SourceAction::Pause).has_value() && sources.state(*source) == audio::VoiceState::Paused, "source pause state");
            std::array<float,2> frame{}; mixer.mix(frame); near(frame[0], 0, "paused source silent");
            test::require(sources.control(*source, audio::SourceAction::Resume).has_value(), "source resumes"); mixer.mix(frame); near(frame[0], .5f, "resume retains cursor");
            options.gain = .5f; test::require(sources.configure(*source, options).has_value(), "live source configure"); mixer.mix(frame); near(frame[0], -.25f, "live configure retains cursor and changes gain");
            test::require(!sources.observe("", *source, audio::SourceAction::Play), "empty event rejected");
            test::require(!sources.control(0, audio::SourceAction::Play), "unknown source rejected");
            test::require(sources.unobserve(*observer) && *sources.notify("door") == 0, "unobserve cancels future notifications");
            const auto removedObserver = sources.observe("door", *source, audio::SourceAction::Stop);
            test::require(removedObserver.has_value() && sources.remove(*source) && mixer.voiceCount() == 0, "removal stops voice");
            test::require(!sources.unobserve(*removedObserver), "removal releases observer");
            const auto one = sources.create(*audio::decodeWav(wav())); test::require(one.has_value() && *one != stale, "source handle never reused");
            test::require(sources.control(*one, audio::SourceAction::Play).has_value(), "one shot source");
            std::array<float,6> complete{}; mixer.mix(complete); test::require(sources.state(*one) == audio::VoiceState::Stopped, "completion reflected by source");
            test::require(sources.control(*one, audio::SourceAction::Play).has_value(), "completed source reusable");
        }
        test::require(mixer.voiceCount() == 0, "controller destruction stops owned sources");
        audio::AudioSources next(mixer, "next"); test::require(!next.contains(stale), "stale source invalid in next scene");
    }
}
void runAudioTests()
{ decoderRejectsTruncationAndUnsupportedFormats(); voicesMixLoopPauseLimitAndCleanup(); spatialSourcesFollowListenerAndAttenuate(); reusableSourcesObserveEventsAndReleaseOwnership(); actualDeviceOpenPlayStop(); }
#ifdef CPP_GAME_ENGINE_SERVICE_TEST_MAIN
int main() { try { runAudioTests(); std::cout << "[PASS] audio service tests\n"; return 0; } catch (const std::exception& e) { std::cerr << "[FAIL] " << e.what() << '\n'; return 1; } }
#endif
