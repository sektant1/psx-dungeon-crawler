-- Broadcasts an event the first time something enters this volume.
--
-- Put it on an entity whose Collider has `sensor` set -- that flag is what
-- makes the engine deliver on_trigger instead of on_collision. A trigger
-- volume and a wall are the same kind of object with one checkbox between
-- them.
--
-- Props:
--   event  string  what to broadcast (default "trigger_entered")
--   once   bool    fire only the first time (default true)
local Trigger = {}

function Trigger:start()
  self.event = self.props.event or "trigger_entered"
  -- `or true` would be wrong here: an authored `once = false` is a legitimate
  -- value that `or` would silently flip back to true.
  self.once = self.props.once
  if self.once == nil then self.once = true end
  self.fired = false
end

function Trigger:on_trigger(other)
  if self.once and self.fired then return end
  self.fired = true
  event.broadcast(self.event, { who = other, from = self.entity })
end

return Trigger
