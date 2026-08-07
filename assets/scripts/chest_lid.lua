-- A chest lid that swings on its hinge.
--
-- Goes on the LID entity, which is a child of the chest's base -- so the lid's
-- rotation is relative to the base and the whole chest can be moved, rotated or
-- instanced anywhere without this script knowing where it is.
--
-- The mesh is authored with its hinge at the origin (Chest_Lid.obj spans
-- z 0..1.6 from y 0), which is what makes "open" a single rotation about X and
-- why there is no pivot offset to maintain here.
--
-- Props:
--   closed_degrees  the shut pose, about the hinge axis (default 0)
--   open_degrees  the open pose (default -100; negative tips it backwards)
--   seconds       how long the swing takes (default 0.45)
--   start_open    begin open rather than closed (default false)
--   auto          swing open and shut forever, for a demo scene (default false)
--   auto_pause    seconds to hold at each end when auto (default 1.2)
--
-- Events, so a trigger volume or another script can drive it:
--   open, close, toggle
local Lid = {}

function Lid:start()
  -- Both poses are authored, not just the open one: which rotation counts as
  -- "shut" is a fact about how the lid mesh was modelled, and a script that
  -- assumed zero would only ever fit the one chest it was written for.
  self.closedDegrees = self.props.closed_degrees or 0
  self.openDegrees = self.props.open_degrees or -100
  self.seconds = math.max(self.props.seconds or 0.45, 0.01)
  self.auto = self.props.auto or false
  self.autoPause = self.props.auto_pause or 1.2
  self.wait = 0

  -- `t` is where the lid is now, 0 shut and 1 open; `target` is where it is
  -- going. Animating a number and deriving the angle from it -- rather than
  -- nudging the rotation each frame -- is what keeps the lid exactly shut when
  -- it is shut, however many times it has been opened.
  self.t = self.props.start_open and 1 or 0
  self.target = self.t
  self:apply()
end

function Lid:apply()
  -- Eased, so the lid slows into both ends instead of stopping dead. The same
  -- smoothstep the engine's own easing uses.
  local e = self.t * self.t * (3 - 2 * self.t)
  local angle = self.closedDegrees + (self.openDegrees - self.closedDegrees) * e
  self.entity.rotation = vec3(angle, 0, 0)
end

function Lid:update(dt)
  if self.auto and self.t == self.target then
    self.wait = self.wait - dt
    if self.wait <= 0 then
      self.target = 1 - self.target
      self.wait = self.autoPause
    end
  end

  if self.t == self.target then return end

  local step = dt / self.seconds
  if self.target > self.t then
    self.t = math.min(self.t + step, self.target)
  else
    self.t = math.max(self.t - step, self.target)
  end
  self:apply()
end

function Lid:on_event(name)
  if name == "open" then
    self.target = 1
  elseif name == "close" then
    self.target = 0
  elseif name == "toggle" then
    self.target = 1 - self.target
  end
end

-- A player walking into a sensor volume around the chest opens it.
function Lid:on_trigger(other)
  self.target = 1
end

return Lid
