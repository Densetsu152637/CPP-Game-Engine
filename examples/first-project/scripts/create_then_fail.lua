local spawned
return {
    on_create = function()
        spawned = engine.create_entity()
        assert(spawned >= 0, "runtime should provide a spawned entity handle")
        assert(engine.set_position(spawned, 8.0, 0.0, 0.0), "spawn should accept a position")
        engine.log("partial-on-create-effect")
        error("intentional setup failure after side effects")
    end,
    on_destroy = function()
        if spawned and engine.is_alive(spawned) then
            engine.destroy_entity(spawned)
            engine.log("spawned-entity-cleaned-by-script")
        end
    end
}
