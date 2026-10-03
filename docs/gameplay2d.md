# Deterministic 2D gameplay mechanics

`project/gameplay2d.h` supplies value-based mechanics to the authored runtime.
Bodies use center positions plus collider offset, with full positive width/height.
Coordinates, sizes, offsets and displacement are finite and bounded to one million
units per axis. IDs are nonempty and at most 256 bytes; a world holds 1024 bodies.
Layer and mask are nonzero bitfields. Two bodies interact only when each body's
layer intersects the other's mask. Box contact alone does not count as overlap.

`World::move` sweeps the requested displacement against solid boxes, stopping
blocked axes and sliding on the remaining axis. It tests the full distance rather
than choosing a number of simulation substeps, so high speed does not tunnel.
Simultaneous corner hits block both axes. Starting overlaps use deterministic
minimum translations with a 32-iteration bound; unresolved starts fail without
mutating the body. Unrepresentable contact or out-of-range movement also fails
transactionally. Movement returns the resolved position and sorted contact IDs.
Other bodies are stationary during one move; callers define their fixed update
order. This is exploration movement, not a rigid-body impulse solver.

Trigger bodies are nonblocking, including when moved. A follower can use a trigger
body to avoid blocking the protagonist, and `isSafe`/`findSafe` to choose positions
that avoid solid walls. `query` and `overlaps` return sorted IDs after mutual mask
filtering. `findSafe` checks the preferred point, then eight fixed directions per
ring up to 64 rings. It returns no point when none of its samples are safe; it is
a bounded placement query, not an exhaustive navigation search.

Call `advanceTriggers` once after all final movement for a tick. It emits sorted
unordered ID pairs with Enter, Stay or Exit. Trigger crossings that begin and end
outside during one tick do not create overlap events. Removing a body causes
exits at the next call; reinsertion after that permits fresh enters. `clear`
for room replacement silently discards pairs and bodies so old-room exits cannot
leak into a new room. A runtime should use fresh identity/lifetime validation for
entity handles separately from these stable authored IDs.

`followVelocity` produces a capped velocity towards a target and stops inside the
requested separation radius. `flockTarget` produces stable circular slots; callers
choose a safe slot with `findSafe` and apply movement themselves. These helpers do
not choose narrative state, save success, healing, acquisition or flock size.

Behavior tests are exported by `runGameplay2dTests()` in
`src/test/project/gameplay2d_tests.cpp`. A standalone focused driver lives in
`src/test/desktop2d/mechanics_test_main.cpp`; the integrated project runner should
also call the exported suite.
