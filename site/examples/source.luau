--!nocheck
local Players = game:GetService("Players")
local UserInputService = game:GetService("UserInputService")
local GameSettings = UserSettings():GetService("UserGameSettings")

local Input = require(script.Parent:WaitForChild("CameraInput"))
local CameraUI = require(script.Parent:WaitForChild("CameraUI"))
local CameraUtils = require(script.Parent:WaitForChild("CameraUtils"))

local LastTogglePan = false
local LastTogglePanChange = tick()

local CrossMouseIcon = "rbxasset://textures/Cursors/CrossMouseIcon.png"

local LockStateDirty = false
local WasTogglePanOnTheLastTimeYouWentIntoFirstPerson = false
local LastFirstPerson = false

CameraUI.setCameraModeToastEnabled(false)

return function(IsFirstPerson: boolean)
	local TogglePan = Input.getTogglePan()
	local ToastTimeout = 3

	if IsFirstPerson and TogglePan ~= LastTogglePan then
		LockStateDirty = true
	end

	if LastTogglePan ~= TogglePan or tick() - LastTogglePanChange > ToastTimeout then
		local DoShow = TogglePan and tick() - LastTogglePanChange < ToastTimeout

		CameraUI.setCameraModeToastOpen(DoShow)

		if TogglePan then
			LockStateDirty = false
		end

		LastTogglePanChange = tick()
		LastTogglePan = TogglePan
	end

	if IsFirstPerson ~= LastFirstPerson then
		if IsFirstPerson then
			WasTogglePanOnTheLastTimeYouWentIntoFirstPerson = Input.getTogglePan()
			Input.setTogglePan(true)
		elseif not LockStateDirty then
			Input.setTogglePan(WasTogglePanOnTheLastTimeYouWentIntoFirstPerson)
		end
	end

	if IsFirstPerson then
		if Input.getTogglePan() then
			CameraUtils.setMouseIconOverride(CrossMouseIcon)
			CameraUtils.setMouseBehaviorOverride(Enum.MouseBehavior.LockCenter)
			CameraUtils.setRotationTypeOverride(Enum.RotationType.CameraRelative)
		else
			CameraUtils.restoreMouseIcon()
			CameraUtils.restoreMouseBehavior()
			CameraUtils.setRotationTypeOverride(Enum.RotationType.CameraRelative)
		end
	elseif Input.getTogglePan() then
		CameraUtils.setMouseIconOverride(CrossMouseIcon)
		CameraUtils.setMouseBehaviorOverride(Enum.MouseBehavior.LockCenter)
		CameraUtils.setRotationTypeOverride(Enum.RotationType.MovementRelative)
	else
		CameraUtils.restoreMouseIcon()
		CameraUtils.restoreMouseBehavior()
		CameraUtils.setRotationTypeOverride(Enum.RotationType.MovementRelative)
	end

	LastFirstPerson = IsFirstPerson
end
