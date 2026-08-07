-- The cube in the starter scene.
--
-- Props:
--   speed  number  metres per second (default 5)
--
-- Action names, not keys: input.down takes an action from [bindings] in
-- project.toml, so remapping never touches a line of Lua.
local Cube = {}

function Cube:start()
  self.speed = self.props.speed or 5
end

function Cube:update(dt)
  local x, z = 0, 0
  if input.down("move_forward") then z = z - 1 end
  if input.down("move_back")    then z = z + 1 end
  if input.down("move_left")    then x = x - 1 end
  if input.down("move_right")   then x = x + 1 end
  if x == 0 and z == 0 then return end

  self.entity.position = self.entity.position + vec3(x, 0, z) * (self.speed * dt)
end

return Cube
