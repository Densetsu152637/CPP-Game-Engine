assert(engine.register_component("TestState", {ticks = "number"}))
assert(engine.register_component("LateState", {value = "number"}))

local spawned
local createdLate = false

return {
    on_create = function()
        assert(self.set_component("TestState", {ticks = 0}))
        spawned = engine.create_entity()
        assert(engine.set_component(spawned, "TestState", {ticks = 0}))
    end,

    systems = {
        {
            name = "mutate-state",
            all = {"TestState"},
            reads = {"TestState"},
            writes = {"TestState", "LateState"},
            order = 0,
            update = function(entity, _dt)
                local state = engine.get_component(entity, "TestState")
                engine.log("state:" .. entity .. ":" .. state.ticks)
                if entity ~= spawned then
                    assert(engine.set_component(entity, "TestState", {ticks = state.ticks + 1}))
                    assert(engine.remove_component(spawned, "TestState"))
                    if not createdLate then
                        local late = engine.create_entity()
                        assert(engine.set_component(late, "LateState", {value = 1}))
                        createdLate = true
                    end
                end
            end
        },
        {
            name = "late-state",
            all = {"LateState"},
            reads = {"LateState"},
            writes = {},
            order = 1,
            update = function(entity, _dt)
                engine.log("late:" .. entity)
            end
        }
    }
}
