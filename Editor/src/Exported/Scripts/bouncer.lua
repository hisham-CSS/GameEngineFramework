-- Example: react to physics contacts and to input.
--
-- Needs a RigidBody + a collider on the same entity. Demonstrates the two
-- hooks that make scripting useful beyond animation: OnCollision (driven by
-- whichever physics backend is active -- Jolt, PhysX, or Simple) and input
-- by ACTION name, so the script survives rebinding.

local jumpImpulse = 6.0
local hits = 0

function OnCollision(c)
    -- c.other is the other entity, c.impulse is the impact strength.
    -- Triggers report c.isTrigger = true and carry no impulse.
    if c.phase == "begin" and not c.isTrigger then
        hits = hits + 1
        log(string.format("hit #%d with %s (impulse %.2f)",
                          hits, c.other:name(), c.impulse))
    end
end

function OnFixedUpdate(dt)
    -- Physics work belongs on the FIXED tick, not OnUpdate: applying an
    -- impulse once per rendered frame makes the force framerate-dependent.
    --
    -- input.pressed() is latched, so it fires exactly once per physical
    -- press even though the fixed tick runs zero times on some frames and
    -- several times on others. "Jump" is bound to Space (gamepad A) by
    -- default; rebind it with InputMap if you want a different key.
    if input.pressed("Jump") then
        self:applyImpulse(vec3.new(0, jumpImpulse, 0))
    end
end
