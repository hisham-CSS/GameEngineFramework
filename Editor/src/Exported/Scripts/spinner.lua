-- Example: rotate an entity in place.
--
-- Attach with Inspector > Add Component > Script, then set File to
-- "spinner.lua". Scripts resolve relative to Exported/Scripts.
--
-- This file replaces what used to be hardcoded C++ demo rotation: the same
-- effect, now authored per-entity and editable without a rebuild.

local degreesPerSecond = 45

function OnStart()
    log("spinner ready on " .. self:name())
end

function OnUpdate(dt)
    -- `self` is this entity. Every instance gets its OWN globals, so two
    -- objects running this file keep independent state.
    self:rotate(vec3.new(0, degreesPerSecond * dt, 0))
end
