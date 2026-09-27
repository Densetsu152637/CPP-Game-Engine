-- Example replacement used to exercise safe-boundary script reload.
return {
    on_update = function()
        if input.pressed("move_right") then
            local position = self.get_position()
            self.set_position(position.x + 10.0, position.y, position.z)
        end
    end
}
