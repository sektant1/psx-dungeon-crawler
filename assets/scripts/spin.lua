-- The minimum useful script: turn, forever.
--
-- Props:
--   degrees_per_second  number  how fast (default 90)
--   axis                vec3    which way (default Y)
--
-- The engine already has a Spin component that does this without a line of
-- Lua. This exists as the smallest complete example of the shape every script
-- takes, and as something visible to point a screenshot at.
--
-- Note that it keeps its own angle on self rather than reading the entity's
-- rotation back each frame. That is the recommended pattern: Euler triples are
-- not unique, so accumulating through a read-back drifts. See docs/scripting.md.
local Spin = {}

function Spin:start()
  self.speed = self.props.degrees_per_second or 90
  self.axis = self.props.axis or vec3(0, 1, 0)
  self.angle = 0
end

function Spin:update(dt)
  self.angle = (self.angle + self.speed * dt) % 360
  self.entity.rotation = self.axis * self.angle
end

return Spin
