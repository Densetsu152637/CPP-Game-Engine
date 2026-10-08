# Desktop audio and player persistence

These services are engine APIs. The host owns their lifetime, resource paths,
project identifier and user-data root; Lua integration exposes only declared
asset IDs and safe slot identifiers.

## PCM audio

`audio::loadWav` and `decodeWav` accept bounded RIFF WAVE resources containing
16-bit PCM mono or stereo, 8-192 kHz. Compressed, float and extensible WAV codecs
return an explicit unsupported error. Files are limited to 64 MiB. The stereo
mixer linearly resamples, sums active voices and clips its output to [-1,1].

Initialize `AudioSystem` before playing. Offline mode (`enablePlayback=false`)
has deterministic `mix` output without opening hardware. Playback mode uses
Windows WinMM `waveOut`, three 512-frame stereo PCM buffers and an owned worker
thread. Link `winmm.lib`. Other platforms return `audio.device.unavailable`
when hardware playback is requested; they still support offline decoding/mixing.
An accepted voice proves submission to the mixer, not physical audibility.
`deviceError()` reports an observed asynchronous backend failure.

Voice handles are monotonically increasing and expire when stopped/completed.
Pause holds sample position. Loops wrap interpolation to the first sample.
Voice, music/effects/dialogue, and master gains multiply; gains clamp to [0,1]
and nonfinite gains become zero. The default cap is 64 simultaneous voices;
excess cues fail explicitly. Voices own immutable shared clip references; callers
must not mutate clip storage through another alias. `stopOwner` clears a scene's
voices. `stopAll` clears every voice. Queued hardware buffers impose up to roughly
32 ms latency at 48 kHz; shutdown joins the worker, resets/unprepares/closes the
device and drops all voices. Initialize/shutdown belong to the lifecycle thread;
voice controls and mixing are synchronized. Use external `mix` only in offline
mode so it cannot consume samples intended for hardware playback.

## Configurable sources and 3D mixing

`PlayOptions` adds pitch (playback-rate multiplier in `[0.125,8]`) and
`SpatialOptions`. `AudioSystem::configure` replaces a voice's options atomically
without changing its cursor or pause state. Invalid options leave it unchanged.
`voiceState` reports playing, paused or stopped, including completed one-shots.
Existing nonspatial playback preserves stereo channels and its previous gains.

Enable `spatial.enabled` for a point emitter at `position`. Positions and
distances use the host's world units. The listener defaults to the origin,
forward `(0,0,-1)`, up `(0,1,0)` in a right-handed world. `setListener` accepts
finite positions and nonzero, nonparallel forward/up vectors; their magnitudes
do not change panning. Constant-power stereo panning uses the listener's right
axis. A centered emitter feeds each channel at `sqrt(0.5)`; spatial stereo clips
are averaged to mono before panning. This is stereo positional mixing without
HRTF, Doppler, obstruction, reverb or distance-dependent delay.

Attenuation is `Inverse` by default. `minDistance` defaults to 1, `maxDistance`
to 100, and `rolloff` to 1. Min distance must be positive, max must exceed min,
and rolloff must be nonnegative. With `d=max(distance,minDistance)`, inverse
gain is `minDistance/(minDistance+rolloff*(d-minDistance))`; linear gain is
`clamp(1-rolloff*(d-minDistance)/(maxDistance-minDistance),0,1)`. Both are silent
at and beyond max distance. `None` disables distance attenuation while retaining
panning. Spatial gain multiplies voice, bus and master gains. Nonfinite spatial
values, invalid enums and out-of-range pitch fail explicitly.

`AudioSources` is a lifecycle-thread controller over an initialized mixer.
`create(clip,options)` configures a reusable source without playing it;
`control(source,SourceAction::Play)` starts or restarts its single voice. Pause
and resume retain the cursor; stop leaves the source reusable. Restart first
stops the old voice, so it works at the voice cap; playback failure leaves the
source stopped. `configure` replaces all options, preserving active playback.
`remove` stops the source and removes its observers. Destruction/`clear` stop
all source voices and release subscriptions. Source and observer handles are
positive integers, never reused across controllers; source handles are distinct
from the legacy voice handles. The controller owns immutable shared clip
references and overrides the playback owner with its constructor's owner.

`observe(event,source,action)` subscribes source control to a named notification.
`notify(event)` executes matching observers in registration order and returns
the count. `unobserve(handle)` cancels a subscription. Unknown notifications are
successful no-ops. Event names are nonempty, at most 512 bytes, without NUL;
controllers permit at most 1024 sources and 1024 observers. A failed notification
returns the first playback error; earlier controls may already have run.

Project Runtime owns a controller per scene. Lua sources/listeners may attach to
a live positioned entity with an offset; positions are resolved at the completed
tick boundary and before explicit source control/notifications. Destroying an
attached entity removes its sources and subscriptions; a destroyed listener
attachment resets the default listener. Runtime retires voices/subscriptions on
stop, scene replacement and faults, resetting a listener it applied. The host
should let one Runtime drive a shared mixer's listener at a time.

Scene preparation can configure silent sources, listeners and subscriptions;
it cannot play, control or emit audio. A candidate's listener is applied only
after the scene commits. Failed preparation and candidate teardown preserve the
active scene's listener and sources. Collision trigger events automatically
notify `trigger:<persistentEntityId>:enter`, `:stay`, and `:exit` for both members
of each pair after simulation. Pair order follows the existing sorted trigger
stream. Stay notifications occur each tick and a play observer therefore restarts
each tick; use enter for a one-shot or loop start. NUL-containing persistent IDs
have no named audio notifications. Other full-size collider IDs are supported.

## Versioned player data

`PersistenceStore` takes host-selected `PersistenceOptions`. The conventional
root is `%LOCALAPPDATA%/CPPGameEngine` on Windows, or XDG data/home fallback on
POSIX. `defaultUserDataRoot` reports absence explicitly. Under that root each
safe project ID has separate `saves/<slot>.json` and
`settings/preferences.json` files. Settings are generic data: remaps, volume,
reduced motion and display preferences do not imply runtime changes by themselves.
Identifiers contain only ASCII letters, digits, underscore and hyphen, at most
64 bytes. Paths, traversal, links/junctions and linked temporary/backup/lock files
are rejected. This is an authored-content confinement boundary in a trusted
host user-data directory, not a security sandbox against another process
maliciously replacing directory entries during operations.

Each document is an envelope with positive integer `schema`, `content_version`,
and object `data`. Defaults are version 1, 1 MiB and depth 32. Data is generic;
the game's host decoder must validate content references and domain fields.
Nonfinite numbers and invalid/bounded JSON fail. Depth is checked before parser
recursion. Missing, malformed, oversized, inaccessible and newer saves are distinct
coded failures. A migration callback is an explicit old-to-current transform;
its returned object is revalidated. A migrated load never rewrites disk. To commit
that migration, pass the callback to `save` with the validated transformed data.
Valid version headers identify future snapshots before interpreting current payload
shape. Newer schema/content snapshots and encountered future backups are not overwritten.
Unrecognized, corrupt or unreadable backups also block replacement; a parser safety
refusal cannot cause destructive recovery of a possibly newer snapshot.

Writing takes a per-slot OS lock, writes a same-directory temporary, flushes it,
then atomically renames/replaces it. Windows uses `FlushFileBuffers` followed by
`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`; POSIX uses file
`fsync`, rename, and directory `fsync`. A previous validated primary is separately
committed to `.bak` before primary replacement. Failed replacement retains a
usable old primary or backup; success is reported only after the durability calls
succeed. Filesystem/hardware guarantees still determine survival of physical power
loss. Stale regular temporary files are discarded on a later write.

Corrupt primaries cannot be silently overwritten. `loadBackup` is a deliberate
read-only recovery choice. `recoverBackup` validates it, durably restores primary,
and retains the known-good backup; it cannot replace a newer primary. Settings
corruption does not affect progression and vice versa. Settings' own `.bak` is
preserved with the same write policy, though the slot recovery API addresses saves.
No unrestricted authored filesystem access is required.

## Validation

`runAudioTests()` in `src/test/desktop2d/audio_tests.cpp` covers bounded WAV decoding,
unsupported/truncated resources, mixer interpolation, one-shot/loop ownership,
pause, multiplied/muted/clamped gains, caps and shutdown. It also opens the actual
platform device, queues a generated low-gain PCM tone for 100 ms, stops/reset/closes,
and reports device unavailability distinctly. This does not verify audibility.
Additional checks cover listener translation/rotation, spatial stereo downmix,
attenuation curves/cutoff, pitch, invalid configurations, source reuse/completion,
observer cancellation, ownership and teardown. Runtime tests exercise the actual
Lua API, entity following, enter/exit control, full-size trigger IDs, candidate
isolation, listener commit/reset, and cleanup on destruction and faults.

`runPersistenceTests()` in `src/test/project/persistence_tests.cpp` covers missing
saves, restart, backup/corruption/recovery, independent settings, injected write,
flush and replacement failures, retry, explicit migration retaining stable IDs,
newer versions, byte/depth bounds, traversal and a real linked-directory escape.
Defining `CPP_GAME_ENGINE_SERVICE_TEST_MAIN` builds either file as a standalone
test executable; otherwise the engine's test runners call the named function.
