-- A door that slides up when told to, and back down when told again.
--
-- Props:
--   height  number  how far it opens, in metres (default 3)
--   speed   number  how fast it moves, in metres per second (default 4)
--
-- It exposes toggle(), open() and close() as methods, which is what lets a
-- lever drive it directly through entity:script(). It also answers the
-- "toggle" event, so anything can drive it without holding a reference.
local Door = {}

function Door:start()
  self.closed_y = self.entity.position.y
  self.height = self.props.height or 3
  self.speed = self.props.speed or 4
  self.is_open = false
end

function Door:open()   self.is_open = true  end
function Door:close()  self.is_open = false end
function Door:toggle() self.is_open = not self.is_open end

function Door:on_event(name)
  if name == "toggle" then self:toggle() end
end

function Door:update(dt)
  local target = self.closed_y + (self.is_open and self.height or 0)
  local p = self.entity.position
  local delta = target - p.y
  local step = self.speed * dt

  -- Clamped rather than lerped. A lerp never quite arrives, and a door that
  -- sits 0.001 short of open is a door that writes its transform forever.
  if math.abs(delta) <= step then
    if delta == 0 then return end -- already there; do not touch the transform
    p.y = target
  else
    p.y = p.y + (delta > 0 and step or -step)
  end
  self.entity.position = p
end

return Door
