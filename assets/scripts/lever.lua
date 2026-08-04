-- Pulls a door when the player presses the interact action nearby.
--
-- Props:
--   target  entity  the door to drive
--   range   number  how close the player must be, in metres (default 2.5)
--
-- Shows both ways to reach another entity: a direct method call through
-- :script(), and a message through :send(). The direct call is used here
-- because a lever genuinely knows it is driving a door. Prefer :send() when
-- the other end is anonymous -- a pressure plate that fires "whatever is
-- listening" should not care what it is talking to.
local Lever = {}

function Lever:start()
  -- Resolved once, not per frame: world.find is a linear scan of the Name view,
  -- and an Entity prop has already done the lookup for us anyway.
  self.target = self.props.target
  self.range = self.props.range or 2.5
  if not self.target then
    log.warn("lever '" .. tostring(self.entity.name) .. "' has no target")
  end
end

function Lever:update(dt)
  if not (self.target and self.target.valid) then return end
  if not input.pressed("interact") then return end

  local player = world.find("player")
  if not player then return end
  if (player.world_position - self.entity.world_position):length() > self.range then
    return
  end

  local door = self.target:script("scripts/door.lua")
  if door then
    door:toggle()
  else
    -- No door script on the target: fall back to the anonymous route.
    self.target:send("toggle")
  end
end

return Lever
