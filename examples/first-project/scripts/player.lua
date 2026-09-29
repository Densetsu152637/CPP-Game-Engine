-- Press the mapped move_right action to move the authored Player by one unit.
-- The Lua system also keeps typed, project-owned runtime state on that entity.
assert(engine.register_component("PlayerStats", {
    ticks = "number",
    moves = "number",
    enabled = "boolean"
}), "PlayerStats schema conflicts with another project script")

return {
    on_create = function()
        assert(self.set_component("PlayerStats", {
            ticks = 0,
            moves = 0,
            enabled = true
        }), "unable to initialize PlayerStats on the authored owner")
    end,

    on_update = function(_dt)
        if input.pressed("move_right") then
            local position = self.get_position()
            self.set_position(position.x + 1.0, position.y, position.z)
        end
    end,

    systems = {{
        name = "PlayerStatsTick",
        all = {"PlayerStats"},
        reads = {"PlayerStats"},
        writes = {"PlayerStats"},
        update = function(entity, _dt)
            local stats = engine.get_component(entity, "PlayerStats")
            if stats and stats.enabled then
                stats.ticks = stats.ticks + 1
                if input.pressed("move_right") then
                    stats.moves = stats.moves + 1
                end
                engine.set_component(entity, "PlayerStats", stats)
            end
        end
    }}
}
