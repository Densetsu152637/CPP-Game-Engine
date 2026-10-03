# ECS and legacy utility cleanup

This note records the removal of engine-owned APIs that had no engine callsites,
plus the older sparse-storage helpers that had no runtime consumers. These
removals reduce unused code and test surface; they make no performance claim.

## Removed APIs and files

| Removed path or symbol | Reason |
| --- | --- |
| `src/structs/sparse_set.h`: `SparseSet` | No runtime consumer. |
| `src/structs/page_set.h`: `Page<T>`, `PageStorage<T>`, `PaginatedSet<T>` | Only supported the unused sparse set and bit field helpers. |
| `src/structs/sparse_bit_field.h/.cpp`: `SparseBitField` | No runtime consumer; its isolated bit operations tests were removed. The component type-ID test remains. |
| `EntityRecord` / `ECS::entity_records()` snapshot | Live entity iteration now reads EnTT's registry directly; the copied record list was unused. |
| `src/ecs/pools/tag_pool.h`: `TagPool<T>` and its `entity_indices()` snapshot | Tag membership is stored in EnTT's native sparse set and exposed through `ECS::tagPoolIfExists<T>()`. |
| `src/rendering/uniform.h`: `Uniform<T>` | No includes or callsites. The active rendering path uses shader bindings and `UniformRegistry`. |
| `src/components/systems.h/.cpp`: example system functions | No engine callsites. The ECS guide carries the useful scheduling and rendering examples directly. |
| `src/components/alias.h`: `Velocity3D`, `Mesh`, `CollisionSurface`, `Surface3D`, `MeshStruct`, and their tag types | These were only used by the uncalled example systems or an empty collision placeholder. `Position3D` remains for project runtime and scripting. |
| `src/structs/vector.h`: `Vector8i`, `Vector8f` | No pre-existing engine, test, or documentation references. The used 2, 3, and 4 element vector types remain. |
| `src/async/thread_timer.h`: `ThreadTimer<T>` | No callers; timeout detaches an uncancellable worker. |
| `src/structs/packed_flags.h/.cpp`: `PackedFlags` | No callers. |
| `src/structs/sorted_array.h`: `SortedArray<T>` | No callers. |
| `src/util/fader.h`: `Fader` | No callers; depended on the unused stopwatch class and referenced a nonexistent `Toolbox` object. |
| `src/util/toolbox.h/.cpp`: legacy file helpers and templates | No external callers. The project has its own project, shader, and asset loading paths. |
| `Notifier` in `src/async/lock.h` | No callers. `Waiter` and `Syncronized` remain in active engine paths. |
| `StopWatch` in `src/util/stopwatch.h` | No callers after `Fader` removal. `get_time_ns()` and `formatDateTime()` remain because logging uses the latter. |
| `src/functional/result.cpp` | Empty translation unit; the `Result<T>` implementation remains in `result.h`. |

The mobile source list no longer names removed translation units. The Makefile's
source wildcards naturally stop compiling deleted `.cpp` files. Test declarations,
fixture includes, and runner calls for the removed sparse-bit-field tests were
also deleted.

The reference audit accepts removed names in this history guide and deliberate
documentation examples; it requires no active include, callsite, or
build-source reference to removed APIs.

## Current engine APIs to use

Game and engine code should use `ECS` operations rather than owning a separate
entity-to-index map or component membership structure. EnTT remains the backend
for entity identity and component storage; the engine facade owns lifecycle,
buffering, dirty transfer, tags, and job rules.

```cpp
struct PositionTag {};
using Position = ecs::BufferedAlias<Vector3f, PositionTag>;

ECS world;
Entity player = world.createEntity();
world.emplaceComponent<Position>(player, Vector3f{0.0f, 0.0f, 0.0f});

for (Entity entity : world.matchingEntities<Position>())
{
    const Position* position = world.try_get<Position>(entity);
    // Read or update the component through the ECS API.
}
```

Use `world.view<Component...>()` for typed iteration and `world.hasComponent`,
`try_get`, `setComponent`, `removeComponent`, and tag operations for individual
entities. Use `ECSProcessor` job declarations when work needs simulation walls,
access analysis, buffering, or render transfer. `EnTTStorage` and EnTT registry
types are implementation details of the engine adapters, not a replacement
public component API.

### Entity and tag iteration

`EntityRecord` and `entity_records()` copied the live entity set for callers.
Entity creation, generation reuse, and live enumeration are now backed by
`EntityRegistry` and EnTT. Use `ECS::eachEntity`, `matchingEntities`, or typed
views when engine `Entity` handles are needed.

`TagPool<T>` and its entity-index snapshot are removed. The existing
`tagPoolIfExists<T>()` accessor now returns `const ecs::BackendSet*`, a pointer
to the tag's native EnTT sparse set. Iterate the returned set directly or use
its native membership operations; it does not allocate an intermediate
`entity_indices()` list. Values from this low-level accessor are EnTT-native
entity IDs, so use the ECS query APIs when engine handles are required.

### Runtime-defined component membership

Dynamic component columns use `entt::basic_sparse_set<std::uint64_t>` for native
entity membership and row lookup. A runtime view intersects those sets for
multi-component queries. Typed values remain in row-aligned `std::vector`
columns (`double`, `uint8_t`, and `std::string`); removing a row moves the last
typed row into its place and erases the matching EnTT membership.

Engine `Entity` versions start at 1. Adapters encode the corresponding EnTT
version as `engine version - 1` and restore that offset when returning engine
handles, preserving the version-1 public invariant and existing packed-handle
format. Project scene, Lua schema, and script APIs remain unchanged.

Standalone `DynamicComponentStorage` validates its input domain: invalid handles,
version 0, and indices at or above `UINT32_MAX` are rejected. A component column
also rejects a different generation at an index while the previous generation's
row remains; after that row is erased, the index can be reused. Normal `ECS`
operations validate that handles are live before reaching the storage layer.

There is no engine-level replacement for the removed generic `SparseBitField`
and `PaginatedSet` utilities because no engine feature depended on them. A new
consumer needing generic flags or sparse mappings should choose a representation
that matches its own domain and lifetime rather than depending on deleted
internal headers.

## Preserved engine features

Entity creation and generation-safe handles, typed and dynamic components,
buffered components, tags and filters, structural deferral, EnTT-backed storage
and views, dirty render transfer, shared-value batching, tuple archetypes, Lua
script access, project runtime, Vulkan rendering, and mobile project execution
remain in place. The ECS feature and API guide is [here](../src/ecs/README.md).

Engine-specific adapter behavior remains necessary: dirty tracking and render
transfer, double-buffer publication, shared-value interning, partial tuple
archetype migration, deferred structural changes, and scheduler access rules are
policies EnTT does not provide for this engine.

## Acceptance and validation matrix

| Check | Acceptance evidence |
| --- | --- |
| Reference and source-list audit | No active includes, calls, or explicit build-source entries refer to removed APIs or translation units. Intentional mentions in this guide or retained ECS documentation do not count as active dependencies. |
| `make -j2 VULKAN=0 all test` | Build the default engine and run the complete native test suite, including the preserved component type-ID test. |
| `powershell -File tools/test-cli.ps1` | Exercise CLI JSON, diagnostics, deterministic input, project initialization, and standalone packaging against the built engine. |
| Vulkan smoke | Not run locally because the Vulkan SDK/GPU are unavailable. Run `make VULKAN=1 smoke` in a configured environment when that check is in scope. The current Windows workflow is headless; mobile CI separately exercises Vulkan. |
| Mobile Android and iOS builds | Mobile CI verification for the final pushed revision is pending; the mobile source list and runtime scope are described in [mobile validation scope](../mobile/README.md). |
| `git diff --check` | No whitespace errors in the integrated changes. |

On the integrated local working tree, `make -j2 VULKAN=0 all test` passed all
seven test executables, `powershell -File tools/test-cli.ps1` passed, and
`git diff --check` passed. These checks ran before publication on an uncommitted
tree. Mobile CI for the final pushed revision remains pending. No benchmark is
part of this cleanup.
