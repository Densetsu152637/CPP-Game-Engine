#include <atomic>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "ecs/aliases/component_alias.h"
#include "ecs/processor.h"
#include "rendering/ecs_rendering.h"
#include "rendering/uniform_registry.h"
#include "structs/sparse_bit_field.h"
#include "test/test_declarations.h"

namespace
{
    struct PositionTag {};
    struct VelocityTag {};
    struct HealthTag {};
    struct ManaTag {};
    struct RenderableTag {};
    struct SelectedTag {};
    struct HiddenTag {};

    using Position = ecs::BufferedAlias<int, PositionTag>;
    using Velocity = ecs::Alias<int, VelocityTag>;
    using Health = ecs::Alias<int, HealthTag>;
    using Mana = ecs::Alias<int, ManaTag>;

    static_assert(
        std::is_same_v<
            decltype(std::declval<ECS&>().registerRenderArchetype<Velocity, Health>()),
            void
        >,
        "render archetype registration should not expose mutable render storage"
    );

    static_assert(
        std::is_const_v<
            std::remove_pointer_t<
                decltype(std::declval<ECS&>().renderArchetypePoolIfExists<Velocity, Health>())
            >
        >,
        "render archetype lookup should expose read-only storage only"
    );

    class CapturingLogger final : public Logger
    {
    public:
        ArrayList<std::string> lines;

        void log(const std::string& message) override
        {
            lines.append(message);
        }
    };

    class TestRenderer final : public IRenderer
    {
    protected:
        bool uploadUniformImpl(
            rendering::IShader& shader,
            const rendering::ShaderUniformUpload& upload
        ) override
        {
            writes.append(rendering::capture_uniform_write(upload, shader.name()));
            return true;
        }

        void renderImpl(rendering::IShader& shader) override
        {
            renderedShaders.append(shader.name());
            ++renderCalls;
        }

    public:
        ArrayList<rendering::ShaderUniformWrite> writes;
        ArrayList<std::string> renderedShaders;
        size_t renderCalls = 0;
    };

    class TestShader final : public rendering::IShader
    {
    public:
        explicit TestShader(std::string name)
            : IShader(std::move(name))
        {}

        std::string_view backendName() const override
        { return "test"; }
    };

    void require(const bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    template <typename Func>
    void require_throws(Func&& func, const std::string& message)
    {
        try
        {
            func();
        }
        catch (const std::exception&)
        {
            return;
        }

        throw std::runtime_error(message);
    }

    bool contains_line(const ArrayList<std::string>& lines, const std::string& token)
    {
        for (const std::string& line : lines)
        {
            if (line.find(token) != std::string::npos)
                return true;
        }

        return false;
    }

    int read_int_uniform(const rendering::ShaderUniformWrite& write)
    {
        int value = 0;
        std::memcpy(&value, write.bytes.data(), sizeof(value));
        return value;
    }

    void test_sparse_bit_field_tracks_sparse_pages()
    {
        SparseBitField bits;
        const size_t secondPageBit = SparseBitField::BITS_PER_PAGE + 1;

        require(bits.empty(), "new sparse bit field should be empty");
        require(!bits.at(secondPageBit), "missing sparse bit page should read false");

        bits.set(5, true);
        bits.set(secondPageBit, true);

        require(bits.size() == 2, "sparse bit field did not count set bits");
        require(bits.at(5), "sparse bit field missed bit on first page");
        require(bits.at(secondPageBit), "sparse bit field missed bit on later page");
        require(!bits.at(secondPageBit + 1), "sparse bit field reported unset bit as true");

        bits.set(5, false);
        require(bits.size() == 1, "sparse bit field did not clear first-page bit");

        bits.set(secondPageBit, false);
        require(bits.empty(), "sparse bit field did not release final set bit");
        require(!bits.at(secondPageBit), "cleared sparse bit should read false");
        require_throws(
            [&]()
            {
                (void)bits.at_packed(0);
            },
            "sparse bit field kept an empty page initialised"
        );
    }

    void test_sparse_bit_field_packed_and_bitwise_operations()
    {
        SparseBitField left;
        SparseBitField right;
        const size_t distantBit = SparseBitField::BITS_PER_PAGE * 3 + 2;

        left.set(1, true);
        left.set(distantBit, true);
        right.set(7, true);
        right.set(distantBit, true);

        SparseBitField both = left & right;
        require(both.size() == 1, "sparse bit field intersection had wrong size");
        require(both.at(distantBit), "sparse bit field intersection missed shared bit");
        require(!both.at(1), "sparse bit field intersection kept left-only bit");
        require(!both.at(7), "sparse bit field intersection kept right-only bit");

        SparseBitField either = left | right;
        require(either.size() == 3, "sparse bit field union had wrong size");

        ArrayList<size_t> indexes = either.trueIndexes();
        require(indexes.length() == 3, "sparse bit field true index enumeration had wrong size");
        require(indexes[0] == 1, "sparse bit field true index enumeration missed first bit");
        require(indexes[1] == 7, "sparse bit field true index enumeration missed second bit");
        require(indexes[2] == distantBit, "sparse bit field true index enumeration missed distant bit");

        SparseBitField packed;
        packed.setPacked(2, SparseBitField::PACKED_TYPE {0b101});
        require(packed.at(SparseBitField::PACKED_SIZE * 2), "packed sparse bit write missed low bit");
        require(packed.at(SparseBitField::PACKED_SIZE * 2 + 2), "packed sparse bit write missed high bit");
        require(packed.size() == 2, "packed sparse bit write had wrong size");

        packed.setPacked(2, 0);
        require(packed.empty(), "zero packed sparse bit write did not clear bits");

        packed.at_packed<true>(4) = SparseBitField::PACKED_TYPE {1} << 3;
        require(packed.at(SparseBitField::PACKED_SIZE * 4 + 3), "guaranteed packed access did not create page");
        packed.at_packed<true>(4) = 0;
        require(packed.empty(), "sparse bit field size did not reflect direct packed clear");
        packed.release_empty_pages();
        require_throws(
            [&]()
            {
                (void)packed.at_packed(4);
            },
            "sparse bit field did not release direct-zeroed page"
        );
    }

    void test_vulkan_uniform_registry_tracks_dirty_values()
    {
        rendering::UniformRegistry uniforms;
        uniforms.declare<int>("u_mode", {0, 0}, rendering::UniformKind::Int);

        require(uniforms.anyDirty(), "new uniform declarations should start dirty");
        require(uniforms.dirtyUploads().length() == 1, "dirty uniform upload enumeration missed declaration");

        uniforms.markAllUploaded(1);
        require(!uniforms.anyDirty(), "markAllUploaded did not clear dirty state");

        require(uniforms.set("u_mode", 7, 2), "changed uniform value did not mark dirty");
        require(uniforms.dirty("u_mode"), "changed uniform was not dirty");
        require(!uniforms.set("u_mode", 7, 3), "unchanged uniform value caused a false dirty write");
        require(uniforms.dirty("u_mode"), "unchanged write should not clear existing dirty state");

        const int* mode = uniforms.try_get<int>("u_mode");
        require(nullptr != mode && *mode == 7, "uniform registry returned the wrong stored value");

        uniforms.markUploaded("u_mode", 4);
        require(!uniforms.anyDirty(), "markUploaded did not clear dirty state");
        require_throws(
            [&]()
            {
                const double wrongSize = 1.0;
                uniforms.set("u_mode", wrongSize);
            },
            "uniform registry accepted a write with the wrong byte size"
        );
    }

    void test_renderer_template_uploads_alias_value()
    {
        TestRenderer renderer;
        TestShader shader("alias-shader");

        const Position position(42);
        require(
            renderer.upload<Position>(shader, "u_position", position),
            "renderer template upload returned false"
        );

        require(renderer.writes.length() == 1, "renderer did not capture the uniform upload");
        const rendering::ShaderUniformWrite& write = renderer.writes[0];
        require(write.shaderName == "alias-shader", "renderer upload did not use the active shader");
        require(write.uniformName == "u_position", "renderer upload used the wrong uniform name");
        require(write.size() == sizeof(int), "renderer uploaded the ECS alias wrapper instead of its value");
        require(read_int_uniform(write) == 42, "renderer uploaded the wrong alias value");
    }

    void test_queue_shader_rendering_uploads_filtered_render_components()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Position>(first, 3);
        ecs.emplaceComponent<Position>(second, 9);
        ecs.addTag<RenderableTag>(first);

        TestRenderer renderer;
        TestShader shader("position-shader");
        shader.bindComponent<Position>("u_position");

        rendering::queue_shader_rendering<Position, ecs::Tag<RenderableTag>>(
            processor,
            "draw-position",
            &renderer,
            &shader
        );

        processor.simulate();
        processor.render();
        renderer.writes.clear();
        renderer.renderedShaders.clear();
        renderer.renderCalls = 0;

        processor.render();
        require(renderer.writes.length() == 1, "shader rendering helper did not filter render entities");
        require(renderer.renderCalls == 1, "shader rendering helper did not issue one render call");
        require(renderer.renderedShaders[0] == "position-shader", "shader rendering helper rendered the wrong shader");

        const rendering::ShaderUniformWrite& write = renderer.writes[0];
        require(write.shaderName == "position-shader", "shader rendering helper used the wrong shader");
        require(write.uniformName == "u_position", "shader rendering helper used the wrong uniform");
        require(read_int_uniform(write) == 3, "shader rendering helper uploaded the wrong component value");
    }

    void test_queue_shader_rendering_uploads_multiple_components()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Position>(entity, 4);
        ecs.emplaceComponent<Velocity>(entity, 8);
        ecs.addTag<RenderableTag>(entity);

        TestRenderer renderer;
        TestShader shader("multi-component-shader");
        shader
            .bindComponent<Position>("u_position")
            .bindComponent<Velocity>("u_velocity");

        rendering::queue_shader_rendering<Position, Velocity, ecs::Tag<RenderableTag>>(
            processor,
            "draw-position-velocity",
            &renderer,
            &shader
        );

        processor.simulate();
        processor.render();
        renderer.writes.clear();
        renderer.renderedShaders.clear();
        renderer.renderCalls = 0;

        processor.render();
        require(renderer.writes.length() == 2, "shader rendering helper did not upload both components");
        require(renderer.renderCalls == 1, "shader rendering helper rendered more than once for one entity");
        require(renderer.renderedShaders[0] == "multi-component-shader", "multi-component helper rendered the wrong shader");

        require(renderer.writes[0].uniformName == "u_position", "first component used the wrong uniform");
        require(renderer.writes[1].uniformName == "u_velocity", "second component used the wrong uniform");
        require(read_int_uniform(renderer.writes[0]) == 4, "first component upload had the wrong value");
        require(read_int_uniform(renderer.writes[1]) == 8, "second component upload had the wrong value");
    }

    void test_ecs_tags_track_entities_and_cleanup()
    {
        ECS ecs;
        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();

        require(ecs.addTag<RenderableTag>(first), "adding a tag returned false");
        require(ecs.hasTag<RenderableTag>(first), "added tag was not visible");
        require(ecs.hasTag<ecs::Tag<RenderableTag>>(first), "tag wrapper lookup did not unwrap tag name");
        require(!ecs.hasTag<RenderableTag>(second), "untagged entity reported tag");
        require(!ecs.addTag<RenderableTag>(first), "adding an existing tag should return false");

        const TagPool* pool = ecs.tagPoolIfExists<RenderableTag>();
        require(nullptr != pool, "tag pool was not created");
        require(pool->size() == 1, "tag pool had wrong size after add");

        require(ecs.removeTag<RenderableTag>(first), "removing existing tag returned false");
        require(!ecs.hasTag<RenderableTag>(first), "removed tag was still visible");
        require(!ecs.removeTag<RenderableTag>(first), "removing missing tag should return false");

        ecs.addTag<SelectedTag>(second);
        ecs.destroyEntity(second);
        require(!ecs.hasTag<SelectedTag>(second), "destroyed entity retained tag");
        require(ecs.tagPoolIfExists<SelectedTag>()->empty(), "tag pool retained destroyed entity");
    }

    void test_tag_pool_keeps_dense_entities()
    {
        ECS ecs;
        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        const Entity third = ecs.createEntity();

        ecs.addTag<RenderableTag>(first);
        ecs.addTag<RenderableTag>(second);
        ecs.addTag<RenderableTag>(third);
        ecs.removeTag<RenderableTag>(second);

        const TagPool* pool = ecs.tagPoolIfExists<RenderableTag>();
        require(nullptr != pool, "dense tag pool was not created");
        require(pool->size() == 2, "dense tag pool had wrong size after removal");
        require(pool->contains(first.index), "dense tag pool lost first tagged entity");
        require(!pool->contains(second.index), "dense tag pool kept removed entity");
        require(pool->contains(third.index), "dense tag pool lost swapped tagged entity");

        const ArrayList<size_t>& entities = pool->entity_indices();
        require(entities.length() == 2, "dense tag entity list had wrong length");
        require(entities.contains(first.index), "dense tag entity list missed first entity");
        require(entities.contains(third.index), "dense tag entity list missed third entity");
    }

    void test_entities_can_be_created_with_tags()
    {
        ECS ecs;

        const Entity immediate = ecs.createEntityWithTags<RenderableTag, SelectedTag>();
        require(ecs.hasEntity(immediate), "tagged entity was not created immediately");
        require(ecs.hasTag<RenderableTag>(immediate), "createEntityWithTags missed first tag");
        require(ecs.hasTag<SelectedTag>(immediate), "createEntityWithTags missed second tag");

        const Entity overload = ecs.createEntity(ecs::Tag<HiddenTag>{});
        require(ecs.hasTag<HiddenTag>(overload), "createEntity(tag...) overload missed tag");

        ecs.beginStructuralDeferral();
        const Entity deferred = ecs.createEntityWithTags<HiddenTag>();
        require(!ecs.hasEntity(deferred), "deferred tagged entity became visible before flush");
        require(!ecs.hasTag<HiddenTag>(deferred), "deferred tag became visible before flush");
        ecs.endStructuralDeferral();
        ecs.flushDeferredStructuralChanges();

        require(ecs.hasEntity(deferred), "deferred tagged entity did not flush");
        require(ecs.hasTag<HiddenTag>(deferred), "deferred tagged entity missed tag after flush");
    }

    void test_deferred_tag_changes_flush_after_wall()
    {
        ECS ecs;
        const Entity entity = ecs.createEntity();

        ecs.beginStructuralDeferral();
        require(ecs.addTag<RenderableTag>(entity), "deferred add tag rejected known entity");
        require(!ecs.hasTag<RenderableTag>(entity), "deferred add tag became visible before flush");
        ecs.endStructuralDeferral();
        ecs.flushDeferredStructuralChanges();
        require(ecs.hasTag<RenderableTag>(entity), "deferred add tag did not flush");

        ecs.beginStructuralDeferral();
        require(ecs.removeTag<RenderableTag>(entity), "deferred remove tag rejected known entity");
        require(ecs.hasTag<RenderableTag>(entity), "deferred remove tag became visible before flush");
        ecs.endStructuralDeferral();
        ecs.flushDeferredStructuralChanges();
        require(!ecs.hasTag<RenderableTag>(entity), "deferred remove tag did not flush");
    }

    void test_filtered_query_uses_tags_and_excludes()
    {
        ECS ecs;
        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        const Entity third = ecs.createEntity();
        const Entity fourth = ecs.createEntity();

        ecs.emplaceComponent<Velocity>(first, 1);
        ecs.emplaceComponent<Velocity>(second, 2);
        ecs.emplaceComponent<Velocity>(third, 3);
        ecs.emplaceComponent<Velocity>(fourth, 4);
        ecs.emplaceComponent<Health>(third, 99);
        ecs.addTag<RenderableTag>(first);
        ecs.addTag<RenderableTag>(second);
        ecs.addTag<RenderableTag>(third);
        ecs.addTag<SelectedTag>(second);

        ArrayList<Entity> componentFiltered = ecs.matchingEntities<
            Velocity,
            ecs::Tag<RenderableTag>,
            ecs::Exclude<ecs::Tag<SelectedTag>>,
            ecs::Exclude<Health>
        >();
        require(componentFiltered.length() == 1, "matchingEntities did not combine tag and exclude filters");
        require(
            componentFiltered[0].index == first.index && componentFiltered[0].version == first.version,
            "matchingEntities returned the wrong filtered entity"
        );

        auto query = ecs.query<Velocity, ecs::Tag<RenderableTag>, ecs::Exclude<ecs::Tag<SelectedTag>>>();
        require(query.size() == 2, "filtered query had wrong initial size");

        int sum = 0;
        query.each([&](const Entity& entity, Velocity& velocity)
        {
            require(entity.index != second.index, "filtered query included excluded tag");
            sum += static_cast<int>(velocity);
        });
        require(sum == 4, "filtered query iterated wrong entities");

        ecs.addTag<SelectedTag>(third);
        require(query.size() == 1, "filtered query cache did not invalidate after tag change");
    }

    void test_tagged_sim_job_filters_component_iteration()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        const Entity third = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 1);
        ecs.emplaceComponent<Velocity>(second, 2);
        ecs.emplaceComponent<Velocity>(third, 3);
        ecs.addTag<RenderableTag>(first);
        ecs.addTag<RenderableTag>(third);

        processor.queue_into_sim<ecs::Tag<RenderableTag>, Velocity>(
            "tagged-velocity",
            [](Velocity& velocity)
        {
            velocity = static_cast<int>(velocity) + 10;
        });

        processor.simulate();

        require(static_cast<int>(*ecs.try_get<Velocity>(first)) == 11, "tagged sim job missed first tagged entity");
        require(static_cast<int>(*ecs.try_get<Velocity>(second)) == 2, "tagged sim job touched untagged entity");
        require(static_cast<int>(*ecs.try_get<Velocity>(third)) == 13, "tagged sim job missed later tagged entity");
    }

    void test_tagged_dirty_job_tracks_filtered_entity_count()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        for (int i = 0; i < 20; ++i)
        {
            const Entity entity = ecs.createEntity();
            ecs.emplaceComponent<Velocity>(entity, i);
            if (i < 3)
                ecs.addTag<RenderableTag>(entity);
        }

        processor.queue_into_rendering<Velocity>("noop-tagged-dirty", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<ecs::Tag<RenderableTag>, ecs::Dirty<Velocity>>(
            "tagged-dirty",
            [](Velocity& velocity)
        {
            velocity = static_cast<int>(velocity) + 100;
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=entities count=3"),
            "tagged dirty job did not use the filtered tagged entity count"
        );
    }

    void test_tag_only_sim_job_iterates_tagged_entities()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        const Entity third = ecs.createEntity();
        ecs.addTag<SelectedTag>(first);
        ecs.addTag<SelectedTag>(third);

        int count = 0;
        size_t indexSum = 0;
        processor.queue_into_sim<ecs::Tag<SelectedTag>, Entity>(
            "tag-only-entities",
            [&](const Entity entity)
        {
            ++count;
            indexSum += entity.index;
        });

        processor.simulate();

        require(count == 2, "tag-only sim job iterated wrong number of entities");
        require(indexSum == first.index + third.index, "tag-only sim job iterated wrong entities");
        require(!ecs.hasTag<SelectedTag>(second), "tag-only sim test accidentally tagged middle entity");
    }

    void test_exclude_filters_in_sim_jobs()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        const Entity third = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 1);
        ecs.emplaceComponent<Velocity>(second, 2);
        ecs.emplaceComponent<Velocity>(third, 3);
        ecs.emplaceComponent<Health>(third, 100);
        ecs.addTag<SelectedTag>(second);

        processor.queue_into_sim<Velocity, ecs::Exclude<ecs::Tag<SelectedTag>>, ecs::Exclude<Health>>(
            "exclude-sim",
            [](Velocity& velocity)
        {
            velocity = static_cast<int>(velocity) + 10;
        });

        processor.simulate();

        require(static_cast<int>(*ecs.try_get<Velocity>(first)) == 11, "exclude sim job missed included entity");
        require(static_cast<int>(*ecs.try_get<Velocity>(second)) == 2, "exclude sim job touched excluded tag");
        require(static_cast<int>(*ecs.try_get<Velocity>(third)) == 3, "exclude sim job touched excluded component");
    }

    void test_tagged_render_job_filters_render_iteration()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        const Entity third = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 1);
        ecs.emplaceComponent<Velocity>(second, 2);
        ecs.emplaceComponent<Velocity>(third, 3);
        ecs.addTag<RenderableTag>(first);
        ecs.addTag<RenderableTag>(third);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<ecs::Tag<RenderableTag>, Velocity>(
            "tagged-render",
            [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 4, "tagged render job did not filter render entities");

        ecs.removeTag<RenderableTag>(third);
        sum = 0;
        processor.render();
        require(sum.load() == 1, "tagged render job did not observe removed tag");
    }

    void test_exclude_filters_in_render_jobs()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        const Entity third = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 1);
        ecs.emplaceComponent<Velocity>(second, 2);
        ecs.emplaceComponent<Velocity>(third, 3);
        ecs.addTag<SelectedTag>(second);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity, ecs::Exclude<ecs::Tag<SelectedTag>>>(
            "exclude-render",
            [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();

        require(sum.load() == 4, "exclude render job did not filter excluded tag");
    }

    void test_sparse_tuple_component_storage_accesses_components()
    {
        SparseTupleComponentStorage<Velocity, Health> storage;

        storage.emplace(42, Velocity(3), Health(7));

        Velocity* velocity = storage.try_get_component<Velocity>(42);
        Health* health = storage.try_get_component<Health>(42);

        require(nullptr != velocity, "tuple component storage did not return Velocity by entity");
        require(nullptr != health, "tuple component storage did not return Health by entity");
        require(static_cast<int>(*velocity) == 3, "tuple component storage returned wrong Velocity value");
        require(static_cast<int>(*health) == 7, "tuple component storage returned wrong Health value");
        require(storage.entity_at(0) == 42, "tuple component storage did not preserve entity key");

        storage.dense_component_at<Velocity>(0) = 11;
        require(
            static_cast<int>(*storage.try_get_component<Velocity>(42)) == 11,
            "tuple component storage dense component access did not update the entity component"
        );
    }

    void test_archetype_registration_tracks_components()
    {
        ECS ecs;
        auto& pool = ecs.registerArchetype<Velocity, Health>();
        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);

        require(ecs.isArchetypedComponent<Velocity>(), "archetype registration did not track Velocity");
        require(ecs.isArchetypedComponent<Health>(), "archetype registration did not track Health");
        require(
            ecs.archetypePoolIfExists<Velocity, Health>() == &pool,
            "archetype pool lookup did not return the registered tuple pool"
        );

        require(
            static_cast<int>(*pool.try_get_component<Velocity>(entity.index)) == 3,
            "registered archetype pool did not store Velocity"
        );
        require(
            static_cast<int>(*pool.try_get_component<Health>(entity.index)) == 7,
            "registered archetype pool did not store Health"
        );
    }

    void test_archetype_registration_merges_superset_groups()
    {
        ECS ecs;
        auto& first = ecs.registerArchetype<Velocity, Health>();
        const Entity entity = ecs.createEntity();
        first.assignComponent<Velocity>(entity.index, Velocity(3));
        first.assignComponent<Health>(entity.index, Health(7));

        auto& merged = ecs.registerArchetype<Velocity, Health, Mana>();

        require(ecs.isArchetypedComponent<Mana>(), "merged archetype did not track the added component");
        require(
            nullptr == ecs.archetypePoolIfExists<Velocity, Health>(),
            "merged archetype left the old subset pool registered"
        );
        require(
            ecs.archetypePoolIfExists<Velocity, Health, Mana>() == &merged,
            "merged archetype pool lookup did not return the superset tuple pool"
        );
        require(
            static_cast<int>(*merged.try_get_component<Velocity>(entity.index)) == 3,
            "merged archetype did not preserve Velocity"
        );
        require(
            static_cast<int>(*merged.try_get_component<Health>(entity.index)) == 7,
            "merged archetype did not preserve Health"
        );
        require(
            nullptr == merged.try_get_component<Mana>(entity.index),
            "merged archetype marked the newly added component present without an explicit insert"
        );
    }

    void test_archetype_rows_track_partial_component_presence()
    {
        ECS ecs;
        ecs.registerArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);

        require(ecs.hasComponent<Velocity>(entity), "archetype row did not report inserted Velocity");
        require(!ecs.hasComponent<Health>(entity), "archetype row reported missing Health as present");
        require(ecs.view<Velocity>().size() == 1, "single-component archetype view missed present Velocity");
        require(ecs.view<Velocity, Health>().size() == 0, "pair archetype view matched a partial row");

        ecs.emplaceComponent<Health>(entity, 7);
        require(ecs.hasComponent<Health>(entity), "archetype row did not report inserted Health");
        require(ecs.view<Velocity, Health>().size() == 1, "pair archetype view missed completed row");
    }

    void test_archetype_component_removal_keeps_other_components()
    {
        ECS ecs;
        ecs.registerArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);

        require(ecs.removeComponent<Health>(entity), "archetype component removal returned false");
        require(ecs.hasComponent<Velocity>(entity), "removing Health erased Velocity from the archetype row");
        require(!ecs.hasComponent<Health>(entity), "removed Health was still present in the archetype row");
        require(ecs.view<Velocity>().size() == 1, "Velocity view missed row after removing Health");
        require(ecs.view<Velocity, Health>().size() == 0, "pair view matched row after removing Health");
    }

    void test_archetype_registration_migrates_existing_standalone_components()
    {
        ECS ecs;
        const Entity first = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 3);

        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(second, 5);
        ecs.emplaceComponent<Health>(second, 7);

        ecs.registerArchetype<Velocity, Health>();

        require(ecs.hasComponent<Velocity>(first), "late archetype registration lost standalone Velocity");
        require(!ecs.hasComponent<Health>(first), "late archetype registration defaulted missing Health");
        require(static_cast<int>(*ecs.try_get<Velocity>(first)) == 3, "late archetype migration changed Velocity");
        require(ecs.view<Velocity>().size() == 2, "late archetype migration lost Velocity rows");
        require(ecs.view<Velocity, Health>().size() == 1, "late archetype migration built wrong pair view");
        require(
            static_cast<int>(*ecs.try_get<Health>(second)) == 7,
            "late archetype migration changed Health"
        );
    }

    void test_archetype_registration_rejects_incomplete_overlap()
    {
        ECS ecs;
        ecs.registerArchetype<Velocity, Health>();

        require_throws(
            [&]()
            {
                ecs.registerArchetype<Health, Mana>();
            },
            "incomplete overlapping archetype registration was not rejected"
        );
    }

    void test_archetype_view_uses_tuple_pool_with_standalone_components()
    {
        ECS ecs;
        ecs.registerArchetype<Velocity, Health>();

        const Entity first = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 3);
        ecs.emplaceComponent<Health>(first, 7);
        ecs.emplaceComponent<Mana>(first, 11);

        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(second, 5);
        ecs.emplaceComponent<Health>(second, 13);

        int count = 0;
        int sum = 0;
        auto view = ecs.view<Velocity, Health, Mana>();
        view.each([&](Velocity& velocity, Health& health, Mana& mana)
        {
            ++count;
            sum += static_cast<int>(velocity);
            sum += static_cast<int>(health);
            sum += static_cast<int>(mana);
        });

        require(count == 1, "archetype-backed mixed view matched the wrong number of entities");
        require(sum == 21, "archetype-backed mixed view read incorrect component values");
    }

    void test_archetype_sim_job_mutates_tuple_components()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);
        ecs.emplaceComponent<Mana>(entity, 11);

        processor.queue_into_sim<Velocity, Health, Mana>(
            "archetype-mixed-update",
            [](Velocity& velocity, Health& health, Mana& mana)
        {
            velocity = static_cast<int>(velocity) + 1;
            health = static_cast<int>(health) + 2;
            mana = static_cast<int>(mana) + 3;
        });

        processor.simulate();

        require(static_cast<int>(*ecs.try_get<Velocity>(entity)) == 4, "sim job did not mutate archetyped Velocity");
        require(static_cast<int>(*ecs.try_get<Health>(entity)) == 9, "sim job did not mutate archetyped Health");
        require(static_cast<int>(*ecs.try_get<Mana>(entity)) == 14, "sim job did not mutate standalone Mana");
    }

    void test_archetype_view_of_iterates_requested_components()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        const Entity first = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 3);

        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(second, 5);
        ecs.emplaceComponent<Health>(second, 7);

        int sum = 0;
        processor.queue_into_sim<ecs::ViewOf<Velocity>>(
            "archetype-view-of",
            [&](const View<Velocity>& velocities)
        {
            for (const Velocity& velocity : velocities)
                sum += static_cast<int>(velocity);
        });

        processor.simulate();

        require(sum == 8, "ecs::ViewOf<T> did not iterate archetyped components");
    }

    void test_view_of_read_conflicts_with_nonbuffered_writer()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);

        processor.queue_into_sim<ecs::ViewOf<Velocity>>(
            "read-velocity-view",
            [](const View<Velocity>& velocities)
        {
            (void)velocities;
        });

        require_throws(
            [&]()
            {
                processor.queue_into_sim<Velocity>("write-velocity", [](Velocity& velocity)
                {
                    velocity = static_cast<int>(velocity) + 1;
                });
            },
            "ecs::ViewOf<T> read did not conflict with same-wall non-buffered writer"
        );
    }

    void test_view_of_can_be_used_in_component_job()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity target = ecs.createEntity();
        ecs.emplaceComponent<Health>(target, 0);

        const Entity first = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 3);

        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(second, 5);

        processor.queue_into_sim<Health, ecs::ViewOf<Velocity>>(
            "component-job-with-view-of",
            [](Health& health, const View<Velocity>& velocities)
        {
            int sum = 0;
            for (const Velocity& velocity : velocities)
                sum += static_cast<int>(velocity);

            health = sum;
        });

        processor.simulate();

        require(
            static_cast<int>(*ecs.try_get<Health>(target)) == 8,
            "ecs::ViewOf<T> did not work as a global argument in a component job"
        );
    }

    void test_view_of_const_multi_component_each()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        const Entity first = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 3);
        ecs.emplaceComponent<Health>(first, 7);

        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(second, 5);
        ecs.emplaceComponent<Health>(second, 11);

        int sum = 0;
        processor.queue_into_sim<ecs::ViewOf<Velocity, Health>>(
            "const-multi-view-of",
            [&](const View<Velocity, Health>& pairs)
        {
            pairs.each([&](const Velocity& velocity, const Health& health)
            {
                sum += static_cast<int>(velocity);
                sum += static_cast<int>(health);
            });
        });

        processor.simulate();

        require(sum == 26, "const multi-component ecs::ViewOf<T...> did not iterate read-only components");
    }

    void test_archetype_render_transfer_uses_tuple_pool_dirty_state()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 1);
        ecs.emplaceComponent<Velocity>(second, 2);
        ecs.emplaceComponent<Health>(second, 7);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum-archetype", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 3, "initial archetype render transfer failed");

        *ecs.try_get_mut<Velocity>(first) = 10;
        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 12, "dirty archetype render transfer failed after mutation");

        ecs.removeComponent<Velocity>(second);
        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 10, "archetype render transfer did not remove deleted component");
    }

    void test_sim_archetype_does_not_create_render_archetype()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum-render-sparse", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        require(
            !ecs.isRenderArchetypedComponent<Velocity>(),
            "simulation archetype implicitly registered Velocity as a render archetype"
        );

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 3, "sparse render pool did not receive sim archetype component transfer");
    }

    void test_explicit_render_archetype_transfers_and_renders()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();
        processor.registerRenderArchetype<Velocity, Health>();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 7);

        require(ecs.isRenderArchetypedComponent<Velocity>(), "Velocity render archetype was not explicit");
        require(ecs.isRenderArchetypedComponent<Health>(), "Health render archetype was not explicit");
        require(
            nullptr != ecs.renderArchetypePoolIfExists<Velocity, Health>(),
            "explicit render archetype pool was not registered"
        );

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity, Health>(
            "sum-render-archetype",
            [&](const Velocity& velocity, const Health& health)
        {
            sum.fetch_add(static_cast<int>(velocity) + static_cast<int>(health), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        require(sum.load() == 0, "explicit render archetype exposed write-buffer data before publish");
        sum = 0;
        processor.render();
        require(sum.load() == 10, "explicit render archetype did not render transferred components after publish");

        *ecs.try_get_mut<Velocity>(entity) = 5;
        processor.simulate();
        sum = 0;
        processor.render();
        require(sum.load() == 10, "explicit render archetype did not preserve the previous read snapshot");
        sum = 0;
        processor.render();
        require(sum.load() == 12, "explicit render archetype did not receive dirty component updates after publish");

        ecs.removeComponent<Velocity>(entity);
        processor.simulate();
        sum = 0;
        processor.render();
        require(sum.load() == 12, "explicit render archetype removal did not preserve the previous read snapshot");
        sum = 0;
        processor.render();
        require(sum.load() == 0, "explicit render archetype did not publish dirty component removal");
    }

    void test_direct_mutable_view_marks_components_dirty()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum-direct-view-write", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 3, "initial direct view dirty test render transfer failed");

        auto view = ecs.view<Velocity>();
        view.each([](Velocity& velocity)
        {
            velocity = 9;
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 9, "direct mutable View::each did not mark the component dirty");
    }

    void test_render_archetype_registration_migrates_existing_render_sparse_pool()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 4);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum-migrated-render", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 4, "initial sparse render transfer failed before render archetype migration");

        processor.registerRenderArchetype<Velocity, Health>();
        require(ecs.isRenderArchetypedComponent<Velocity>(), "render archetype registration did not track Velocity");

        sum = 0;
        processor.render();
        require(sum.load() == 4, "render archetype registration did not migrate existing render sparse data");
    }

    void test_render_archetype_registration_preserves_pending_sparse_publish()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 4);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum-pending-migrated-render", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        processor.registerRenderArchetype<Velocity, Health>();

        sum = 0;
        processor.render();
        require(sum.load() == 0, "pending sparse-to-archetype migration exposed unpublished write data");

        sum = 0;
        processor.render();
        require(sum.load() == 4, "pending sparse-to-archetype migration lost the pending publish");
    }

    void test_component_type_ids_are_stable_and_distinct()
    {
        const ecs::ComponentTypeId firstPositionId = ecs::component_type_id<Position>();
        const ecs::ComponentTypeId secondPositionId = ecs::component_type_id<Position>();
        const ecs::ComponentTypeId velocityId = ecs::component_type_id<Velocity>();

        require(firstPositionId == secondPositionId, "component type ID was not stable for the same type");
        require(firstPositionId != velocityId, "component type IDs were not distinct across component types");
    }

    void test_buffered_write_write_conflict()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);

        processor.queue_into_sim<Position>("writer-a", [](Position& position)
        {
            position = 1;
        });

        require_throws(
            [&]()
            {
                processor.queue_into_sim<Position>("writer-b", [](Position& position)
                {
                    position = 2;
                });
            },
            "buffered mutable/mutable same-wall conflict was not rejected"
        );
    }

    void test_buffered_read_write_is_allowed()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);

        processor.queue_into_sim<Position>("reader", [](const Position& position)
        {
            (void)position;
        });

        processor.queue_into_sim<Position>("writer", [](Position& position)
        {
            position = 2;
        });
    }

    void test_nonbuffered_read_write_conflict()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);

        processor.queue_into_sim<Velocity>("reader", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        require_throws(
            [&]()
            {
                processor.queue_into_sim<Velocity>("writer", [](Velocity& velocity)
                {
                    velocity = 2;
                });
            },
            "non-buffered read/write same-wall conflict was not rejected"
        );
    }

    void test_view_cache_invalidates_on_storage_changes()
    {
        ECS ecs;
        const Entity entity = ecs.createEntity();

        auto velocityView = ecs.view<Velocity>();
        require(velocityView.size() == 0, "new component view should be empty");

        ecs.emplaceComponent<Velocity>(entity, 1);
        require(velocityView.size() == 1, "component view did not observe later component insertion");

        auto pairView = ecs.view<Velocity, Health>();
        require(pairView.size() == 0, "pair view should not match before second component insertion");

        ecs.emplaceComponent<Health>(entity, 10);
        require(pairView.size() == 1, "pair view did not invalidate after component insertion");

        ecs.removeComponent<Health>(entity);
        require(pairView.size() == 0, "pair view did not invalidate after component removal");
    }

    void test_dirty_entity_render_transfer_preserves_unchanged_entities()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity first = ecs.createEntity();
        const Entity second = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(first, 1);
        ecs.emplaceComponent<Velocity>(second, 2);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Velocity>("sum", [&](const Velocity& velocity)
        {
            sum.fetch_add(static_cast<int>(velocity), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 3, "initial full render transfer failed");

        *ecs.try_get_mut<Velocity>(first) = 10;
        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 12, "dirty transfer failed after first entity update");

        *ecs.try_get_mut<Velocity>(second) = 20;
        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 30, "dirty transfer rolled back an unchanged render entity");
    }

    void test_buffered_simulation_write_transfers_to_render()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Position>(entity, 1);

        std::atomic<int> sum = 0;
        processor.queue_into_rendering<Position>("sum", [&](const Position& position)
        {
            sum.fetch_add(static_cast<int>(position), std::memory_order_relaxed);
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 1, "initial buffered render transfer failed");

        processor.queue_into_sim<Position>("increment", [](Position& position)
        {
            position = static_cast<int>(position) + 1;
        });

        processor.simulate();
        sum = 0;
        processor.render();
        sum = 0;
        processor.render();
        require(sum.load() == 2, "scheduled buffered write did not transfer to render");
    }

    void test_unwritten_buffered_pool_does_not_swap_on_unrelated_wall()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Position>(entity, 5);
        ecs.emplaceComponent<Velocity>(entity, 1);

        *ecs.try_get_mut<Position>(entity) = 6;
        processor.simulate();
        require(static_cast<int>(*ecs.try_get<Position>(entity)) == 6, "direct buffered mutation was not flushed");

        processor.queue_into_sim<Velocity>("velocity-only", [](Velocity& velocity)
        {
            velocity = static_cast<int>(velocity) + 1;
        });

        processor.simulate();
        require(
            static_cast<int>(*ecs.try_get<Position>(entity)) == 6,
            "unwritten buffered component rolled back after unrelated simulation job"
        );
    }

    void test_dirty_threshold_promotes_to_full_transfer()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        ArrayList<Entity> entities;
        for (int i = 0; i < 5; ++i)
        {
            const Entity entity = ecs.createEntity();
            entities.append(entity);
            ecs.emplaceComponent<Velocity>(entity, i);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);

        for (const Entity& entity : entities)
            *ecs.try_get_mut<Velocity>(entity) = 100;

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=full"),
            "dirty threshold did not promote broad dirty set to full transfer"
        );
    }

    void test_dirty_wrapper_marks_only_touched_entities()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        for (int i = 0; i < 100; ++i)
        {
            const Entity entity = ecs.createEntity();
            ecs.emplaceComponent<Velocity>(entity, i);
            if (i < 5)
                ecs.emplaceComponent<Health>(entity, 1);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<ecs::Dirty<Velocity>, Health>(
            "mark-velocity-dirty",
            [](Velocity& velocity, const Health& health)
        {
            (void)health;
            velocity = static_cast<int>(velocity) + 1000;
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=entities count=5"),
            "ecs::Dirty<T> wrapper did not track only touched entities"
        );
    }

    void test_dirty_wrapper_marks_only_touched_archetyped_entities()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        processor.registerArchetype<Velocity, Health>();

        for (int i = 0; i < 100; ++i)
        {
            const Entity entity = ecs.createEntity();
            ecs.emplaceComponent<Velocity>(entity, i);
            if (i < 5)
                ecs.emplaceComponent<Health>(entity, 1);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<ecs::Dirty<Velocity>, Health>(
            "mark-archetype-velocity-dirty",
            [](Velocity& velocity, const Health& health)
        {
            (void)health;
            velocity = static_cast<int>(velocity) + 1000;
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "archetype component type=") &&
                contains_line(logger.lines, "mode=entities count=5"),
            "ecs::Dirty<T> wrapper did not track only touched archetyped entities"
        );
    }

    void test_dirty_wrapper_marks_full_when_touched_count_reaches_threshold()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        for (int i = 0; i < 100; ++i)
        {
            const Entity entity = ecs.createEntity();
            ecs.emplaceComponent<Velocity>(entity, i);
            if (i < 64)
                ecs.emplaceComponent<Health>(entity, 1);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<ecs::Dirty<Velocity>, Health>(
            "mark-velocity-dirty",
            [](Velocity& velocity, const Health& health)
        {
            (void)health;
            velocity = static_cast<int>(velocity) + 1000;
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=full"),
            "ecs::Dirty<T> wrapper did not mark full when touched count reached the dirty threshold"
        );
    }

    void test_global_dirty_wrapper_matches_namespaced_dirty_wrapper()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        for (int i = 0; i < 100; ++i)
        {
            const Entity entity = ecs.createEntity();
            ecs.emplaceComponent<Velocity>(entity, i);
            if (i < 5)
                ecs.emplaceComponent<Health>(entity, 1);
        }

        processor.queue_into_rendering<Velocity>("noop", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        processor.render();

        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);
        processor.queue_into_sim<Dirty<Velocity>, Health>(
            "mark-velocity-dirty",
            [](Velocity& velocity, const Health& health)
        {
            (void)health;
            velocity = static_cast<int>(velocity) + 1000;
        });

        processor.simulate();
        logger.logHistory();

        require(
            contains_line(logger.lines, "mode=entities count=5"),
            "Dirty<T> wrapper did not match ecs::Dirty<T> dirty transfer behaviour"
        );
    }

    void test_structural_changes_are_deferred_until_wall_finishes()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();
        processor.createWall("AFTER_CREATE", 2);

        Entity created;
        int sameWallHealthCount = 0;
        int nextWallHealthCount = 0;

        processor.queue_into_sim<>("create-entity", [&]()
        {
            created = ecs.createEntity();
            Health& staged = ecs.emplaceComponent<Health>(created, 1);
            staged = 7;
            require(!ecs.hasEntity(created), "deferred entity became visible inside the creating wall");
            require(!ecs.hasComponent<Health>(created), "deferred component became visible inside the creating wall");
        });

        processor.queue_into_sim<Health>("same-wall-reader", [&](const Health& health)
        {
            (void)health;
            ++sameWallHealthCount;
        });

        processor.queue_into_sim<Health>("next-wall-reader", "AFTER_CREATE", [&](const Health& health)
        {
            (void)health;
            ++nextWallHealthCount;
        });

        processor.simulate();

        require(sameWallHealthCount == 0, "deferred component was observed by another job in the same wall");
        require(nextWallHealthCount == 1, "deferred component was not visible to the next wall");
        require(ecs.hasEntity(created), "deferred entity was not created after wall flush");
        require(ecs.hasComponent<Health>(created), "deferred component was not created after wall flush");
        require(static_cast<int>(*ecs.try_get<Health>(created)) == 7, "deferred component did not flush staged value");
    }

    void test_deferred_destroy_and_component_removal_are_invisible_until_wall_finishes()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        ECS& ecs = processor.ecs();

        const Entity entity = ecs.createEntity();
        ecs.emplaceComponent<Velocity>(entity, 3);
        ecs.emplaceComponent<Health>(entity, 9);

        int sameWallVelocityCount = 0;
        processor.queue_into_sim<>("destroy-and-remove", [&]()
        {
            ecs.removeComponent<Health>(entity);
            ecs.destroyEntity(entity);
            require(ecs.hasEntity(entity), "deferred destroy became visible inside the destroying wall");
            require(ecs.hasComponent<Health>(entity), "deferred component removal became visible inside the destroying wall");
        });

        processor.queue_into_sim<Velocity>("same-wall-velocity-reader", [&](const Velocity& velocity)
        {
            (void)velocity;
            ++sameWallVelocityCount;
        });

        processor.simulate();

        require(sameWallVelocityCount == 1, "deferred destroy hid the entity from another job in the same wall");
        require(!ecs.hasEntity(entity), "deferred destroy did not flush after wall");
        require(!ecs.hasComponent<Velocity>(entity), "deferred destroy did not remove component storage after wall");
    }

    void test_scheduler_logging()
    {
        Threadpool pool(1, std::string("ecs-test"));
        ECSProcessor processor(pool);
        CapturingLogger logger;
        processor.setSchedulerLogger(&logger);

        processor.queue_into_sim<Velocity>("move", [](Velocity& velocity)
        {
            velocity = 1;
        });
        processor.queue_into_rendering<Velocity>("draw", [](const Velocity& velocity)
        {
            (void)velocity;
        });

        processor.simulate();
        processor.render();
        logger.logHistory();

        require(contains_line(logger.lines, "queued simulation job"), "scheduler logger missed sim queue event");
        require(contains_line(logger.lines, "queued rendering job"), "scheduler logger missed render queue event");
        require(contains_line(logger.lines, "running simulation wall"), "scheduler logger missed wall execution event");
    }
}

int main()
{
    try
    {
        test_sparse_bit_field_tracks_sparse_pages();
        test_sparse_bit_field_packed_and_bitwise_operations();
        test_vulkan_uniform_registry_tracks_dirty_values();
        test_shader_component_bindings_create_uploads();
        test_shader_component_binding_redeclaration_updates_slot();
        test_shader_component_upload_requires_binding();
        test_renderer_template_uploads_alias_value();
        test_queue_shader_rendering_uploads_filtered_render_components();
        test_queue_shader_rendering_uploads_multiple_components();
        test_rendering_helper_uses_shader_owned_bindings();
        test_rendering_helper_supports_shared_mesh_batches();
        test_shared_render_jobs_group_by_unique_component();
        test_shared_sim_jobs_group_by_shared_alias();
        test_shared_render_jobs_require_only_one_shared_component();
        test_ecs_tags_track_entities_and_cleanup();
        test_tag_pool_keeps_dense_entities();
        test_entities_can_be_created_with_tags();
        test_deferred_tag_changes_flush_after_wall();
        test_filtered_query_uses_tags_and_excludes();
        test_tagged_sim_job_filters_component_iteration();
        test_tagged_dirty_job_tracks_filtered_entity_count();
        test_tag_only_sim_job_iterates_tagged_entities();
        test_exclude_filters_in_sim_jobs();
        test_tagged_render_job_filters_render_iteration();
        test_exclude_filters_in_render_jobs();
        test_sparse_tuple_component_storage_accesses_components();
        test_archetype_registration_tracks_components();
        test_archetype_registration_merges_superset_groups();
        test_archetype_rows_track_partial_component_presence();
        test_archetype_component_removal_keeps_other_components();
        test_archetype_registration_migrates_existing_standalone_components();
        test_archetype_registration_rejects_incomplete_overlap();
        test_archetype_view_uses_tuple_pool_with_standalone_components();
        test_archetype_sim_job_mutates_tuple_components();
        test_archetype_view_of_iterates_requested_components();
        test_view_of_read_conflicts_with_nonbuffered_writer();
        test_view_of_can_be_used_in_component_job();
        test_view_of_const_multi_component_each();
        test_archetype_render_transfer_uses_tuple_pool_dirty_state();
        test_sim_archetype_does_not_create_render_archetype();
        test_explicit_render_archetype_transfers_and_renders();
        test_direct_mutable_view_marks_components_dirty();
        test_render_archetype_registration_migrates_existing_render_sparse_pool();
        test_render_archetype_registration_preserves_pending_sparse_publish();
        test_component_type_ids_are_stable_and_distinct();
        test_buffered_write_write_conflict();
        test_buffered_read_write_is_allowed();
        test_nonbuffered_read_write_conflict();
        test_view_cache_invalidates_on_storage_changes();
        test_dirty_entity_render_transfer_preserves_unchanged_entities();
        test_buffered_simulation_write_transfers_to_render();
        test_unwritten_buffered_pool_does_not_swap_on_unrelated_wall();
        test_dirty_threshold_promotes_to_full_transfer();
        test_dirty_wrapper_marks_only_touched_entities();
        test_dirty_wrapper_marks_only_touched_archetyped_entities();
        test_dirty_wrapper_marks_full_when_touched_count_reaches_threshold();
        test_global_dirty_wrapper_matches_namespaced_dirty_wrapper();
        test_structural_changes_are_deferred_until_wall_finishes();
        test_deferred_destroy_and_component_removal_are_invisible_until_wall_finishes();
        test_scheduler_logging();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }

    std::cout << "[PASS] ECS tests\n";
    return 0;
}
