-- Deterministic callback fixture: mutable captures, register reuse, and methods.
local Runner = {}
function Runner:Run(start, speed, steps)
    local scaled = speed * 20
    local acceleration = -self.Gravity * 2
    local elapsed = 0
    local previous = start
    local result
    local done = false
    local function Step(delta)
        if done then return end
        elapsed += delta
        local position = start + scaled * elapsed + acceleration * 0.5 * elapsed ^ 2
        result = position - previous
        previous = position
        if elapsed >= steps then done = true end
    end
    while not done do Step(1) end
    return previous, result, elapsed
end
Runner.Gravity = 4
return Runner:Run(3, 2, 5)
