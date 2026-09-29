assert(engine.register_component("AbortState", {value = "number"}))

return {
    on_create = function()
        assert(self.set_component("AbortState", {value = 0}))
    end,
    on_destroy = function()
        local state = self.get_component("AbortState")
        engine.log("abort-state:" .. (state and state.value or -1))
    end,
    systems = {{
        name = "fail-after-staging",
        all = {"AbortState"},
        reads = {"AbortState"},
        writes = {"AbortState"},
        update = function(entity, _dt)
            assert(engine.set_component(entity, "AbortState", {value = 9}))
            local spawned = engine.create_entity()
            assert(engine.set_component(spawned, "AbortState", {value = 1}))
            error("intentional system failure")
        end
    }}
}
