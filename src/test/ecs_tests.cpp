#include <exception>
#include <iostream>

#include "test/test_declarations.h"
void test_dynamic_component_storage_and_deferred_query_coherence();
void test_render_device_concurrency();

int main()
{
    try
    {
        test_entt_entity_generations_survive_reuse_and_clear();
        test_entt_storage_growth_and_view_membership();
        test_vulkan_uniform_registry_tracks_dirty_values();
        test_arraylist_serializes_for_gpu_buffers();
        test_std_vector_serializes_for_gpu_buffers();
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
        test_processor_can_use_separate_simulation_and_render_pools();
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
        test_dynamic_component_storage_and_deferred_query_coherence();
        test_render_device_concurrency();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }

    std::cout << "[PASS] ECS tests\n";
    return 0;
}
