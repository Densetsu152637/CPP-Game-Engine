return {
    on_create = function()
        print("script-started", 7)
    end,
    on_update = function()
        if input.pressed("move_right") then
            local position = self.get_position()
            self.set_position(position.x + 1.0, position.y, position.z)
        end
    end,
    on_destroy = function()
        engine.log("old-destroy")
        error("authored script teardown failed")
    end
}
