-- Press the mapped move_right action to move the authored Player by one unit.
-- This script controls its owner through `self`; it does not create a duplicate.
return {
    on_update = function(_dt)
        if input.pressed("move_right") then
            local position = self.get_position()
            self.set_position(position.x + 1.0, position.y, position.z)
        end
    end
}
