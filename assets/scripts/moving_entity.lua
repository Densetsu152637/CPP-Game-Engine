-- Scripts return a lifecycle table. Entity ids are opaque integers owned by the engine.
local entity_id

return {
    on_create = function()
        entity_id = engine.create_entity()
        engine.set_position(entity_id, 0.0, 0.0, 0.0)
        engine.log("moving_entity started")
    end,

    on_update = function(delta_seconds)
        if not engine.is_alive(entity_id) then
            return
        end

        local position = engine.get_position(entity_id)
        if position then
            engine.set_position(entity_id, position.x + delta_seconds, position.y, position.z)
        end
    end,

    on_destroy = function()
        if entity_id and engine.is_alive(entity_id) then
            engine.destroy_entity(entity_id)
        end
    end
}
