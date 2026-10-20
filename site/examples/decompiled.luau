-- Decompiled with Astra | Bytecode Version: 14
-- Time taken: 0.001021s

local Players = game:GetService("Players")
local UserInputService = game:GetService("UserInputService")
local UserGameSettings = UserSettings():GetService("UserGameSettings")

local CameraInput = require(script.Parent:WaitForChild("CameraInput"))
local CameraUI = require(script.Parent:WaitForChild("CameraUI"))
local CameraUtils = require(script.Parent:WaitForChild("CameraUtils"))

local Boolean1 = false
local Value1 = tick()
local Boolean2 = false
local Boolean3 = false
local Boolean4 = false

CameraUI.setCameraModeToastEnabled(false)

local function Function1(Argument1) -- Line: 22 | require(game.StarterGui.Idek)
    --[[
        Upvalues
        [1] CameraInput (copy)
        [2] Boolean1 (ref)
        [3] Boolean2 (ref)
        [4] Value1 (ref)
        [5] CameraUI (copy)
        [6] Boolean4 (ref)
        [7] Boolean3 (ref)
        [8] CameraUtils (copy)
    ]]--

    local Value2 = CameraInput.getTogglePan()

    if not ((not Argument1) or (Value2 == Boolean1)) then
        Boolean2 = true
    end

    if Boolean1 ~= Value2 or 3 < (((tick()) - Value1)) then
        local Value3 = (Value2) and ((((tick()) - Value1)) < 3)

        CameraUI.setCameraModeToastOpen(Value3)

        if Value2 then
            Boolean2 = false
        end

        Value1 = tick()

        Boolean1 = Value2
    end

    if Argument1 ~= Boolean4 then
        if Argument1 then
            Boolean3 = CameraInput.getTogglePan()

            CameraInput.setTogglePan(true)
        elseif not Boolean2 then
            CameraInput.setTogglePan(Boolean3)
        end
    end

    if Argument1 then
        if CameraInput.getTogglePan() then
            CameraUtils.setMouseIconOverride("rbxasset://textures/Cursors/CrossMouseIcon.png")
            CameraUtils.setMouseBehaviorOverride(Enum.MouseBehavior.LockCenter)
        else
            CameraUtils.restoreMouseIcon()
            CameraUtils.restoreMouseBehavior()
        end

        CameraUtils.setRotationTypeOverride(Enum.RotationType.CameraRelative)

        Boolean4 = Argument1

        return
    elseif CameraInput.getTogglePan() then
        CameraUtils.setMouseIconOverride("rbxasset://textures/Cursors/CrossMouseIcon.png")
        CameraUtils.setMouseBehaviorOverride(Enum.MouseBehavior.LockCenter)
        CameraUtils.setRotationTypeOverride(Enum.RotationType.MovementRelative)

        Boolean4 = Argument1

        return
    else
        CameraUtils.restoreMouseIcon()
        CameraUtils.restoreMouseBehavior()
        CameraUtils.setRotationTypeOverride(Enum.RotationType.MovementRelative)
    end

    Boolean4 = Argument1
end

return Function1
