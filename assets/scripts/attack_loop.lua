-- A looping weapon swing, driven on the weapon entity itself.
--
-- The boss placeholder is a static .obj: there is no skeleton under it and no
-- clips beside it, so a skeletal attack animation is not something this asset
-- can have. What it *can* have -- now that "Unpack attachments" makes the sword
-- a real entity rather than a part baked in at cook time -- is a script on the
-- weapon that swings it.
--
-- Shaped like a strike rather than a metronome: anticipation, a fast cut, then
-- a slower settle and a beat of rest. Linear motion reads as a machine; the
-- windup is what makes the cut land.
--
-- Props:
--   windup      number  seconds drawing back      (default 0.45)
--   strike      number  seconds of the cut        (default 0.12)
--   recover     number  seconds settling back     (default 0.55)
--   rest        number  seconds held at rest      (default 0.60)
--   axis        vec3    swing axis                (default X, an overhead cut)
--   back        number  degrees of anticipation   (default -55)
--   through     number  degrees at the end of it  (default 95)
local AttackLoop = {}

-- Eases used per phase, each chosen for what that phase has to communicate.
local function ease_out_cubic(t)
  local u = 1 - t
  return 1 - u * u * u
end

local function ease_in_quart(t)
  return t * t * t * t
end

function AttackLoop:start()
  self.windup = self.props.windup or 0.45
  self.strike = self.props.strike or 0.12
  self.recover = self.props.recover or 0.55
  self.rest = self.props.rest or 0.60
  self.axis = self.props.axis or vec3(1, 0, 0)
  self.back = self.props.back or -55
  self.through = self.props.through or 95

  -- The pose the piece was authored in. Every angle below is relative to it,
  -- so the swing works wherever the author has seated the weapon in the hand
  -- rather than snapping it to some origin the script picked.
  self.rest_rotation = self.entity.rotation
  self.t = 0
end

function AttackLoop:update(dt)
  local cycle = self.windup + self.strike + self.recover + self.rest
  self.t = (self.t + dt) % cycle

  local angle
  local t = self.t
  if t < self.windup then
    -- Anticipation: slow away from rest, decelerating into the top of the
    -- swing so the pause before the cut reads as a held breath.
    angle = self.back * ease_out_cubic(t / self.windup)
  elseif t < self.windup + self.strike then
    -- The cut. Accelerating hard, and the shortest phase by a factor of four:
    -- what sells a strike is that you cannot follow it.
    local u = (t - self.windup) / self.strike
    angle = self.back + (self.through - self.back) * ease_in_quart(u)
  elseif t < self.windup + self.strike + self.recover then
    -- Settle back to rest, decelerating: weight, not a rewind.
    local u = (t - self.windup - self.strike) / self.recover
    angle = self.through * (1 - ease_out_cubic(u))
  else
    angle = 0
  end

  self.entity.rotation = self.rest_rotation + self.axis * angle
end

return AttackLoop
