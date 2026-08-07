-- ScriptedCube
--
-- A script is a class table: methods are shared by every entity that carries
-- this file, and state lives on `self`, which is one table per entity. Never
-- put per-entity state at file scope -- see docs/scripting.md.
--
-- Every callback is optional. Delete what this does not need.

local ScriptedCube = {}

function ScriptedCube:start()
  -- Once, on the first tick after this script is attached. The scene is fully
  -- built by now, so it is safe to look things up.
  --
  -- `self.props` holds the values authored on this instance in the inspector.
  self.speed = self.props.speed or 1.0
end

function ScriptedCube:update(dt)
  -- Every frame, in game time: dt is already scaled and is zero while paused.
end

-- function ScriptedCube:fixed_update(dt) end   -- before each physics step
-- function ScriptedCube:on_collision(other, hit) end
-- function ScriptedCube:on_trigger(other) end  -- a sensor collider was entered
-- function ScriptedCube:on_event(name, data) end
-- function ScriptedCube:on_destroy() end

return ScriptedCube
