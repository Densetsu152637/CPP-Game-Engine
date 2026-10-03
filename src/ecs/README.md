# ECS

This folder contains the engine ECS runtime, job scheduler, views, component
pools, and simulation-to-render transfer layer.

The public entry points are:

- `ECS`: owns entities, simulation component storage, render component storage,
  tags, views, queries, and structural changes.
- `ECSProcessor`: owns an `ECS`, schedules simulation/render jobs, runs walls,
  transfers dirty simulation data into render storage, and executes render jobs.
- `View<T...>` and `FilteredView<...>`: typed component iteration helpers.
- `ecs::Alias<T, Tag>`, `ecs::BufferedAlias<T, Tag>`, and
  `ecs::SharedAlias<T, Tag>`: component alias wrappers used by engine code.

## EnTT Backend

The official EnTT dependency is pinned as the `third_party/entt` Git submodule.
Initialize it with `git submodule update --init --recursive`.

EnTT owns entity allocation and generation reuse (`entt::basic_registry<uint64_t>`),
component object storage (`entt::basic_storage`), tag and grouped-component
membership (`entt::basic_sparse_set`), and multi-component intersections
(`entt::basic_runtime_view`). Component type IDs use EnTT's type index. Builds
must define `ENTT_USE_ATOMIC` consistently for concurrent type registration.

`EntityRegistry` adapts the engine handle format to EnTT identity and iterates
live entities directly from EnTT. A scalar high-water index preserves deferred
reservation behavior; EnTT owns entity generations and live membership.
`Entity::packed()` / `Entity::fromPacked()` preserve the 32-bit index and 32-bit
engine generation for script handles. Clearing an ECS retains EnTT generations,
so old handles cannot revive when slots are reused.

Component pools use standalone EnTT storages because simulation values, both
render snapshots, and explicitly grouped tuple rows have different lifetimes.
The registry owns entity identity; these storages own their component objects.
They are intentionally private to the engine adapters rather than attached as
ordinary components to that identity registry. Engine APIs validate handles and
remove all memberships on destruction before an index can be reused. The
`EnTTStorage` facade replaces the ECS's custom sparse/dense component maps,
and EnTT views replace the manual multi-pool intersection implementation.

The remaining pool/registry classes implement engine policies that EnTT does
not supply: dirty transfer, double-buffer publication, shared-value interning,
explicit tuple grouping and migration, structural deferral, and job scheduling.
Shared-value pools intern a value once per equality group and keep the
entity-to-group mapping in EnTT. The retired general-purpose sparse-set, page,
and bit-field utilities are no longer part of the supported engine utility API.

EnTT uses paged component storage. `denseComponents<T>()` therefore returns a
lightweight range **by value**, not an `ArrayList&`; indexing and iteration still
refer to the stored components. Insertion preserves standalone component
addresses. Erasure, clear, and migration to explicit groups can invalidate
references. View iteration order is unspecified.

## Component Types

Components are normal C++ types. The project usually wraps them in alias types
so two components can store the same underlying value type without becoming the
same ECS component.

```cpp
struct PositionTag {};
struct VelocityTag {};
struct MeshTag {};
struct MeshStruct
{
    int id;
    bool operator==(const MeshStruct&) const = default;
};

using Position = ecs::BufferedAlias<Vector3f, PositionTag>;
using Velocity = ecs::Alias<Vector3f, VelocityTag>;
using Mesh = ecs::SharedAlias<MeshStruct, MeshTag>;
```

Alias types behave like their wrapped value for most call sites. Rendering
uploads use the alias value rather than the wrapper object.

There are three important alias storage policies:

- `ecs::Alias<T, Tag>`: direct simulation component. Reads and writes touch the
  same value.
- `ecs::BufferedAlias<T, Tag>`: double-buffered simulation component. Reads use
  the current read buffer and writes go to the write buffer. Buffers swap after a
  simulation wall.
- `ecs::SharedAlias<T, Tag>`: direct component when used as `Mesh`, but a shared
  batch key when wrapped as `ecs::Shared<Mesh>` in a job declaration.

`ecs::Shared<T>` is only valid for `ecs::SharedAlias` component types.

## Entity And Component API

`Entity` is a lightweight handle containing an index and version. Version checks
prevent stale handles from matching newly-created entities that reuse an index.

Common `ECS` operations:

```cpp
ECS ecs;

Entity entity = ecs.createEntity();
Entity tagged = ecs.createEntityWithTags<RenderableTag, SelectedTag>();

ecs.emplaceComponent<Position>(entity, Vector3f{});
ecs.setComponent<Velocity>(entity, Vector3f{1.0f, 0.0f, 0.0f});

bool hasPosition = ecs.hasComponent<Position>(entity);
const Position* position = ecs.try_get<Position>(entity);
Velocity* velocity = ecs.try_get_mut<Velocity>(entity);

ecs.addTag<RenderableTag>(entity);
ecs.removeTag<SelectedTag>(entity);
bool renderable = ecs.hasTag<RenderableTag>(entity);

ecs.removeComponent<Velocity>(entity);
ecs.destroyEntity(entity);
```

Mutable lookups mark the component dirty. Dirty state is used by the render
bridge to copy only changed simulation components into render storage.

`denseComponents<T>()` exposes standalone dense storage. It is not available for
archetyped components; use `ViewOf<T...>` or `view<T...>()` instead.

## Views And Queries

`view<T...>()` iterates entities that have all requested components. Components
are supplied as mutable references for simulation storage.

```cpp
auto view = ecs.view<Position, Velocity>();
view.each([](Entity entity, Position& position, Velocity& velocity)
{
    position = position + velocity;
});
```

`render_view<T...>()` reads render storage and supplies const component
references.

`query<Filters...>()` and `render_query<Filters...>()` use the same filter
pipeline as job declarations. Supported filters include:

- component types: include entities with the component.
- `ecs::Tag<T>`: include entities with the tag.
- `ecs::Exclude<T>`: exclude entities matching the component, tag, dirty, or
  shared filter.
- `ecs::Shared<T>`: include entities that have the shared alias component.

EnTT runtime views intersect native membership sets and start from their
smallest include set. Excludes are native runtime-view filters. Queries with no
include source enumerate live identities and check any exclusions by entity
index. Missing include sources produce no matches; missing exclude sources have
no effect.

`tagPoolIfExists<T>()` exposes the tag's const EnTT sparse set directly. Iterate
that range or use its native membership operations; tags do not maintain a
separate entity-index snapshot.

## ECSProcessor

Most game systems should use `ECSProcessor` rather than operating on `ECS`
directly. It owns the ECS instance and references the thread pools used for
simulation and render-side work.

```cpp
Threadpool simulationPool;
Threadpool renderPool;
ECSProcessor sim(simulationPool, renderPool);

sim.queue_into_sim<Velocity, ecs::Dirty<Position>>(
    "movement",
    [](const Velocity& velocity, Position& position)
    {
        position = position + velocity;
    }
);

sim.simulate();
```

Use `ECSProcessor sim(pool);` when simulation and rendering should share one
unified pool. Use `ECSProcessor sim(simulationPool, renderPool);` when simulation
jobs should dispatch to one pool and render transfer/render jobs should dispatch
to another.

Public processor API:

- `ecs()`: access the owned `ECS`.
- `createWall(name, position)`: create an ordered simulation wall.
- `queue_into_sim<Args...>(name, callable)`: queue into the default wall.
- `queue_into_sim<Args...>(name, wall, callable)`: queue into a named wall.
- `queue_into_rendering<Args...>(name, callable)`: queue a render job.
- `registerArchetype<Components...>()`: store simulation components in a tuple
  archetype pool.
- `registerRenderArchetype<Components...>()`: store render components in a
  tuple render archetype pool.
- `simulate()`: run simulation walls and transfer dirty data to render storage.
- `render()`: run the render job batch and publish render buffers.
- `setSchedulerLogger()` / `clearSchedulerLogger()`: inspect scheduling and
  transfer events.

## Simulation Job Arguments

Simulation job declarations are template arguments to `queue_into_sim`. The
callable parameters are derived from those arguments and must be inspectable
non-generic call operators.

Supported declaration arguments:

- `Component`: component argument.
- `const Component` in the callable: read access.
- `Component&` in the callable: write access.
- `ecs::Dirty<Component>`: component argument that guarantees dirty tracking for
  each matched entity.
- `ecs::ViewOf<Components...>`: global view argument, passed as
  `const View<Components...>&`.
- `Entity`: pass the entity handle.
- `ecs::Tag<T>`: filter, not passed to the callable.
- `ecs::Exclude<T>`: filter, not passed to the callable.
- `ecs::Shared<T>`: shared alias filter and component argument.

```cpp
sim.queue_into_sim<Velocity, ecs::Dirty<Position>>(
    "movement",
    [](const Velocity& velocity, Position& position)
    {
        position = position + velocity;
    }
);
```

`ecs::Dirty<T>` is useful when a job mutates `T` indirectly or when it should
force dirty tracking even though scheduling analysis is driven by callable
parameter constness.

## SharedAlias And `ecs::Shared`

`SharedAlias` exists for component values that are reused by many entities, such
as meshes or materials. It has two modes:

### Plain SharedAlias

Using the component type directly treats it like any other component. The job
iterates entities in parallel.

```cpp
sim.queue_into_sim<Velocity, ecs::Dirty<Position>, Mesh>(
    "entity_mesh_iteration",
    [](const Velocity& velocity, Position& position, Mesh& mesh)
    {
        // One parallel task chunk can contain any entities.
    }
);
```

### `ecs::Shared<T>`

Wrapping a `SharedAlias` in `ecs::Shared<T>` changes the iteration strategy.
Entities are grouped by unique shared value, then the job runs one parallel task
per unique shared value. Inside a shared batch, entities are visited
sequentially.

```cpp
sim.queue_into_sim<Velocity, ecs::Dirty<Position>, ecs::Shared<Mesh>>(
    "shared_mesh_iteration",
    [](const Velocity& velocity, Position& position, Mesh& mesh)
    {
        // Runs sequentially for entities using the same Mesh.
        // Different Mesh values can be processed in parallel.
    }
);
```

Only one `ecs::Shared<T>` argument is allowed in a job. The shared component is
also a filter: entities without the shared component do not match.

Simulation shared pools are entity-addressable so simulation jobs can still pass
the shared component as `T&`. Render jobs use shared values as batch keys and do
not pass the shared component to the render callable.

## Scheduling Behaviour

Simulation jobs are organized into walls. The default walls are:

- `ECSProcessor::FIRST_WALL` (`"__INIT__"`)
- `ECSProcessor::DEFAULT_WALL` (`"__RUNTIME__"`)
- `ECSProcessor::FINAL_WALL` (`"__CLEANUP__"`)

Walls run in ascending position order. Jobs in the same wall are intended to run
concurrently, subject to access conflict checks at queue time.

The scheduler derives access from callable parameters:

- `const Component&`: read.
- `Component&`: write.
- non-buffered write: exclusive write.
- buffered write: normal write, because readers read the old buffer.
- `ecs::ViewOf<T...>`: read access to the view components.
- `ecs::Shared<T>`: read access to the shared alias filter.

Conflicts rejected within the same wall:

- write/write to the same component type.
- non-buffered write/read on the same component type.

Buffered components allow read/write jobs in the same wall because reads and
writes use separate buffers. Two writers still conflict.

During each wall:

1. Structural changes are deferred.
2. Jobs execute concurrently.
3. Simulation buffers swap.
4. Structural changes are flushed.

If a job creates or destroys entities, adds/removes tags, or adds/removes
components, those structural changes are not visible to other jobs in the same
wall. They become visible after the wall finishes.

After all simulation walls finish, `ECSRenderBridge` transfers dirty simulation
components into render storage using the processor's render thread pool. Render
jobs then read from render storage during `render()`.

## Internal Parallelism

Jobs can be internally parallel:

- component simulation jobs iterate matched entities through `View`.
- tag/entity-only jobs map over matched entities.
- `ecs::Shared<T>` simulation jobs map over unique shared values and iterate
  each value's entities sequentially.
- render shared jobs map over unique shared values.

If a wall contains internally parallel jobs and the wall has enough jobs to fill
the thread pool, the executor runs the jobs on the caller thread to avoid
oversubscribing the pool with nested parallel work.

## Render Jobs And Shader-Owned Bindings

Render jobs are queued separately from simulation walls:

```cpp
sim.queue_into_rendering<Position, ecs::Tag<ExampleTag>>(
    "draw",
    [](const Position& position)
    {
        // Render storage, const component refs.
    }
);
```

Render job arguments support component types, `ecs::Tag<T>`,
`ecs::Exclude<T>`, and `ecs::Shared<T>`. Component references passed to render
callables are const.

`rendering::queue_shader_rendering` creates a render job that uploads the
components bound on the shader object:

```cpp
shader.bindComponent<Position>("u_position");
shader.bindComponent<Velocity>("u_velocity");

rendering::queue_shader_rendering<
    Position,
    Velocity,
    ecs::Shared<Mesh>,
    ecs::Tag<ExampleTag>
>(sim, "ENTITY_RENDERING_EXAMPLE", renderer, shader);
```

The shader owns the component-to-uniform mapping. The render job no longer needs
to specify binding names at the call site. With `ecs::Shared<Mesh>`, render
entities are grouped by unique mesh value; each mesh batch can run in parallel,
and entities inside a batch are rendered sequentially.

## Render Transfer And Dirty State

Simulation storage and render storage are separate. Render jobs see a stable
render snapshot and do not directly read mutable simulation storage.

Dirty transfer supports:

- standalone component pools.
- buffered simulation components.
- shared simulation component pools.
- simulation archetype pools.
- render archetype destinations.

Dirty state can be precise by entity or promoted to a full transfer when enough
entities are touched. This keeps small edits cheap while avoiding large dirty
entity lists for broad updates.

## Archetypes

Standalone component pools are the default. Archetypes can be registered when a
set of components should be stored together:

```cpp
sim.registerArchetype<Velocity, Health>();
sim.registerRenderArchetype<Velocity, Health>();
```

Archetype registration migrates existing standalone component data into the
archetype pool. Overlapping archetype registrations must name the full merged
component set. Archetype rows track partial component presence, so an entity can
have only some components from an archetype group.

Current archetype support is for direct components. Buffered archetype storage
needs a dedicated buffering policy.

## Practical Rules

- Prefer `ECSProcessor` jobs for systems; direct `ECS` mutation is best for
  setup, tests, and explicit immediate work.
- Use const references in job callables whenever possible. Constness is part of
  the scheduler's access model.
- Use `BufferedAlias` for values that need same-wall read/write separation.
- Use `ecs::Dirty<T>` when dirty tracking must be guaranteed for a component.
- Use `SharedAlias` plus `ecs::Shared<T>` when many entities share the same
  expensive resource and the work should batch by unique resource.
- Keep render callables read-only. Render jobs operate on render storage, not
  simulation storage.
