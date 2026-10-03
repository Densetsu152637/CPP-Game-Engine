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

`runPersistenceTests()` in `src/test/project/persistence_tests.cpp` covers missing
saves, restart, backup/corruption/recovery, independent settings, injected write,
flush and replacement failures, retry, explicit migration retaining stable IDs,
newer versions, byte/depth bounds, traversal and a real linked-directory escape.
Defining `CPP_GAME_ENGINE_SERVICE_TEST_MAIN` builds either file as a standalone
test executable; otherwise the engine's test runners call the named function.
