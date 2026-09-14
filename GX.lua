--[[
	GX - Gold Export
	-----------------
	Simple account-wide gold tracker.

	Purpose:
	  - Stores each character's current gold in the shared SavedVariables table
	    (GX_DB), so values from all characters on one account are aggregated.
	  - "/gx show"     shows total account gold in a modern Blizzard-themed frame.
	  - "/gx export"   shows a copyable text box with the current total.
	  - "/gx autosave" periodically reloads the UI so SavedVariables get written
	    to disk even without a manual reload (a reload is the only moment the
	    WoW client flushes SavedVariables to disk).

	Why this design?
	  The WoW Lua sandbox has NO io/os file API. An addon can never write a
	  plain .txt file - the only thing it writes is SavedVariables, and the
	  client only flushes those on reload/logout. That is why /gx autosave
	  exists (periodic ReloadUI) and why a small external script
	  (watcher.py) is used to read GX.lua and produce a clean text file for
	  OBS.

	UI reference (wow-ui-source):
	  - PortraitFrameFlatTemplate  (Blizzard_SharedXML/Mainline/SharedUIPanelTemplates.xml)
	  - DialogBorderTemplate & DialogHeaderTemplate
	                               (Blizzard_SharedXML/Shared/Dialog/DialogTemplates.xml)
	  - UIPanelCloseButton         (Blizzard_SharedXML/Mainline/SharedUIPanelTemplates.xml)
	  - InputBoxTemplate           (Blizzard_SharedXML/Shared/InputBox/InputBoxTemplates.xml)
	  Money display is done with the current GetMoneyString(...) from
	  Blizzard_SharedXML/FormattingUtil.lua.
]]

-- Chat prefix helper. Defined first so every function below can use it.
-- Do not remove - it's referenced by the autosave and slash command code.
local function addonLabel()
	return "|cff00c8ffGX|r: "
end

-- ===========================================================================
-- SavedVariables
-- GX_DB --- written to WTF\Account\<account>\SavedVariables\<folder>.lua
-- ===========================================================================

local function InitDB()
	if type(GX_DB) ~= "table" then
		GX_DB = {}
	end
	GX_DB.version = GX_DB.version or 1
	if type(GX_DB.characters) ~= "table" then
		GX_DB.characters = {}
	end
	if type(GX_DB.warband) ~= "table" then
		GX_DB.warband = { gold = 0, lastSeen = 0 }
	end
	if GX_DB.warband.gold == nil then
		GX_DB.warband.gold = 0
	end
	if type(GX_DB.minimap) ~= "table" then
		GX_DB.minimap = {}
	end
	if GX_DB.minimap.shown == nil then
		GX_DB.minimap.shown = true
	end
	if GX_DB.minimap.degrees == nil then
		GX_DB.minimap.degrees = 220
	end
	if GX_DB.autosaveNoInstances == nil then
		GX_DB.autosaveNoInstances = true
	end
end

InitDB()

-- Same coin icon is used for the /gx show portrait and the minimap button
-- (both rendered through the TempPortraitAlphaMask circle mask, so they
-- look like identical circular coin icons).
local COIN_ICON = "Interface\\Icons\\INV_Misc_Coin_05"

local COPPER_PER_SILVER = 100
local SILVER_PER_GOLD = 100
local COPPER_PER_GOLD = COPPER_PER_SILVER * SILVER_PER_GOLD

-- Default autosave interval (seconds). 60 minutes is safe, 60 s is the
-- documented minimum. ReloadUI briefly freezes the UI.
local AUTOSAVE_DEFAULT_S = 60 * 60

-- ===========================================================================
-- Globals for the frames (referenced from the UISpecialFrames table and the
-- watcher documentation). Pre-declared so nil-checking is trivial.
-- ===========================================================================

GXMainFrame = nil
GXExportFrame = nil

-- ===========================================================================
-- Money helpers
-- ===========================================================================

-- Character lookup key: "Name-Realm".
local function GetCharacterKey()
	local name = UnitName("player")
	local realm = GetRealmName()
	if not name or name == UNKNOWNOBJECT then
		return nil
	end
	return name .. "-" .. realm
end

-- Store (overwrite) the current character's gold with a timestamp.
local function SaveCurrentCharacterGold()
	InitDB()
	local key = GetCharacterKey()
	if not key then
		return
	end
	local gold = GetMoney() or 0
	local entry = GX_DB.characters[key]
	if entry then
		entry.gold = gold
		entry.lastSeen = time()
	else
		GX_DB.characters[key] = { gold = gold, lastSeen = time() }
	end
end

-- Query and cache Warband (Account) Bank gold (The War Within / Midnight 11.0+)
local function UpdateWarbandBankGold()
	InitDB()
	if C_Bank and C_Bank.FetchDepositedMoney and Enum and Enum.BankType and Enum.BankType.Account then
		local ok, money = pcall(C_Bank.FetchDepositedMoney, Enum.BankType.Account)
		if ok and type(money) == "number" then
			local canView = C_Bank.CanViewBank and C_Bank.CanViewBank(Enum.BankType.Account)
			if money > 0 or canView then
				GX_DB.warband = GX_DB.warband or {}
				GX_DB.warband.gold = money
				GX_DB.warband.lastSeen = time()
			end
		end
	end
end

-- Sum all stored character gold values and Warband bank across the account.
local function GetTotalGold()
	InitDB()
	local total = 0
	for _, data in pairs(GX_DB.characters) do
		if type(data) == "table" then
			local gold = tonumber(data.gold)
			if gold then
				total = total + gold
			end
		end
	end
	if GX_DB.warband and type(GX_DB.warband.gold) == "number" then
		total = total + GX_DB.warband.gold
	end
	return total
end

-- Plain-text money ("12,345g 67s 89c") - used in the copyable export box.
-- Thousands separator is implemented locally so we don't depend on the
-- optional/undocumented BreakUpLargeNumbers runtime helper.
local function FormatThousands(value)
	local s = tostring(math.floor(value))
	return (s:reverse():gsub("(%d%d%d)", "%1,"):reverse():gsub("^,", ""))
end

local function FormatMoneyText(money)
	local gold = math.floor(money / COPPER_PER_GOLD)
	local silver = math.floor((money - gold * COPPER_PER_GOLD) / COPPER_PER_SILVER)
	local copper = money % COPPER_PER_SILVER

	local text = ""
	local separator = ""
	if gold > 0 then
		text = text .. FormatThousands(gold) .. "g"
		separator = " "
	end
	if silver > 0 then
		text = text .. separator .. silver .. "s"
		separator = " "
	end
	if copper > 0 or text == "" then
		text = text .. separator .. copper .. "c"
	end
	return text
end

-- Native-styled money string (coin texture markup / colorblind abbreviations),
-- produced by Blizzard's GetMoneyString. Used in the /gx show frame.
local function GetStyledMoneyString(money)
	return GetMoneyString(
		money,
		MoneyStringConstants.SeparateThousands,
		MoneyStringConstants.CheckGoldThreshold,
		MoneyStringConstants.ShowZeroAsGold
	)
end

-- ===========================================================================
-- Main frame (/gx show) - created lazily from "GXMainFrameTemplate"
-- ===========================================================================

local GXMainFrameMixin = {}
_G.GXMainFrameMixin = GXMainFrameMixin

function GXMainFrameMixin:OnLoad()
	self:SetTitle("GX - Total Account Gold")
	self:SetPortraitToAsset(COIN_ICON)

	-- Compact: single readable line with the total + a small subtitle.
	local amount = self:CreateFontString(nil, "OVERLAY", "GameFontNormalHuge")
	amount:SetPoint("CENTER", 0, -2)
	amount:SetJustifyH("CENTER")
	amount:SetFont(STANDARD_TEXT_FONT, 20)
	amount:SetShadowOffset(1, -1)
	self.Amount = amount

	local subtitle = self:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	subtitle:SetPoint("CENTER", 0, -30)
	subtitle:SetJustifyH("CENTER")
	subtitle:SetFont(STANDARD_TEXT_FONT, 11)
	subtitle:SetText("Total account gold")
	self.Subtitle = subtitle

	-- Draggable.
	self:SetMovable(true)
	self:EnableMouse(true)
	self:RegisterForDrag("LeftButton")
end

function GXMainFrameMixin:OnDragStart()
	self:StartMoving()
end

function GXMainFrameMixin:OnDragStop()
	self:StopMovingOrSizing()
end

-- ===========================================================================
-- Export frame (/gx export) - created lazily from "GXExportFrameTemplate"
-- ===========================================================================

local GXExportFrameMixin = {}
_G.GXExportFrameMixin = GXExportFrameMixin

function GXExportFrameMixin:OnLoad()
	self.Header:Setup("Gold Export - copy me")

	local hint = self:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	hint:SetPoint("BOTTOM", self, "BOTTOM", 0, 10)
	hint:SetText("Select the text and press Ctrl+C to copy.")
	self.Hint = hint

	-- Draggable.
	self:SetMovable(true)
	self:EnableMouse(true)
	self:RegisterForDrag("LeftButton")
end

function GXExportFrameMixin:OnDragStart()
	self:StartMoving()
end

function GXExportFrameMixin:OnDragStop()
	self:StopMovingOrSizing()
end

-- ===========================================================================
-- Frame creation (lazy, guarded against double-create)
-- ===========================================================================

local function ShowTotalGoldFrame()
	if not GXMainFrame then
		GXMainFrame = CreateFrame("Frame", "GXMainFrame", UIParent, "GXMainFrameTemplate")
		-- ESC closes the frame.
		tinsert(UISpecialFrames, "GXMainFrame")
	end
	local total = GetTotalGold()
	GXMainFrame.Amount:SetText(GetStyledMoneyString(total))
	GXMainFrame:Show()
end

local function ToggleTotalGoldFrame()
	if GXMainFrame and GXMainFrame:IsShown() then
		GXMainFrame:Hide()
		return
	end
	ShowTotalGoldFrame()
end

local function ShowExportFrame()
	if not GXExportFrame then
		GXExportFrame = CreateFrame("Frame", "GXExportFrame", UIParent, "GXExportFrameTemplate")
		-- ESC closes the frame.
		tinsert(UISpecialFrames, "GXExportFrame")
	end
	local total = GetTotalGold()
	GXExportFrame.EditBox:SetText(FormatMoneyText(total) .. " (" .. tostring(total) .. "c)")
	GXExportFrame:Show()
end

local function ToggleExportFrame()
	if GXExportFrame and GXExportFrame:IsShown() then
		GXExportFrame:Hide()
		return
	end
	ShowExportFrame()
end

-- If any exported frame is open, refresh its content (e.g. after PLAYER_MONEY).
local function RefreshOpenFrames()
	if GXMainFrame and GXMainFrame:IsShown() then
		GXMainFrame.Amount:SetText(GetStyledMoneyString(GetTotalGold()))
	end
	if GXExportFrame and GXExportFrame:IsShown() then
		local total = GetTotalGold()
		GXExportFrame.EditBox:SetText(FormatMoneyText(total) .. " (" .. tostring(total) .. "c)")
	end
end

-- ===========================================================================
-- Autosave: periodic ReloadUI so SavedVariables are flushed to disk.
-- ===========================================================================

local autosaveTimer = nil
local autosaveSkipNoticeShown = false

local function CancelAutosaveTimer()
	if autosaveTimer then
		autosaveTimer:Cancel()
		autosaveTimer = nil
	end
	autosaveSkipNoticeShown = false
end

-- Defer the autosave reload while inside an instance (dungeon/raid/arena/BG)
-- if the "skip reload in instances" option is on. Reloading mid-content drops
-- the UI (brief freeze) and can be disruptive.
local function IsReloadAllowed()
	return not (GX_DB.autosaveNoInstances ~= false and IsInInstance())
end

local function ArmAutosaveTimer(seconds)
	autosaveTimer = C_Timer.NewTimer(seconds, function()
		autosaveTimer = nil
		if IsReloadAllowed() then
			ReloadUI()
		else
			if not autosaveSkipNoticeShown then
				autosaveSkipNoticeShown = true
				print(addonLabel() .. "Autosave reload skipped - player is in an instance.")
			end
			ArmAutosaveTimer(seconds)
		end
	end)
end

local function SetAutosave(seconds, silent)
	CancelAutosaveTimer()
	seconds = tonumber(seconds)
	if not seconds or seconds <= 0 then
		GX_DB.autosaveMinutes = nil
		if not silent then
			print(addonLabel() .. "Autosave disabled.")
		end
		return
	end

	GX_DB.autosaveMinutes = seconds / 60
	ArmAutosaveTimer(seconds)
	if not silent then
		print(addonLabel() .. string.format("Autosave enabled - UI reload every %d s.", seconds))
		print(addonLabel() .. "Reload flushes SavedVariables; expect a brief (< 1 s) UI freeze.")
	end
end

-- Re-arm on login so the loop survives reloads.
local function ArmAutosaveFromSaved()
	if GX_DB.autosaveMinutes and GX_DB.autosaveMinutes > 0 then
		local minutes = GX_DB.autosaveMinutes
		ArmAutosaveTimer(minutes * 60)
		print(addonLabel() .. string.format("Autosave active - UI reload every %.0f min.", minutes))
	end
end

-- ===========================================================================
-- Minimap button (Blizzard circular tracking style, orbits Minimap perimeter)
-- Derived from wow-ui-source Minimap / Tracking / AddonCompartment patterns.
-- ===========================================================================

local GXMinimapButton = nil

-- Forward declarations for the options panel
local GXSettingsCategory = nil
local GXSettingsPanel = nil
local OpenSettingsPanel
local RegisterGXSettings

-- Standard shape definition for round, square and corner minimaps
local MINIMAP_SHAPES = {
	["ROUND"]                 = { true, true, true, true },
	["SQUARE"]                = { false, false, false, false },
	["CORNER-TOPLEFT"]        = { false, false, false, true },
	["CORNER-TOPRIGHT"]       = { false, false, true, false },
	["CORNER-BOTTOMLEFT"]     = { false, true, false, false },
	["CORNER-BOTTOMRIGHT"]    = { true, false, false, false },
	["SIDE-LEFT"]             = { false, true, false, true },
	["SIDE-RIGHT"]            = { true, false, true, false },
	["SIDE-TOP"]              = { false, false, true, true },
	["SIDE-BOTTOM"]           = { true, true, false, false },
	["TRICORNER-TOPLEFT"]     = { false, true, true, true },
	["TRICORNER-TOPRIGHT"]    = { true, false, true, true },
	["TRICORNER-BOTTOMLEFT"]  = { true, true, false, true },
	["TRICORNER-BOTTOMRIGHT"] = { true, true, true, false },
}

local function GetMinimapButtonPosition(degrees, radius)
	degrees = degrees or 220
	radius = radius or 8
	local angle = math.rad(degrees)
	local cos = math.cos(angle)
	local sin = math.sin(angle)

	local q = 1
	if cos < 0 then
		q = q + 1
	end
	if sin > 0 then
		q = q + 2
	end

	local width = ((Minimap and Minimap:GetWidth() or 200) / 2) + radius
	local height = ((Minimap and Minimap:GetHeight() or 200) / 2) + radius

	local minimapShape = GetMinimapShape and GetMinimapShape() or "ROUND"
	local shapes = MINIMAP_SHAPES[minimapShape] or MINIMAP_SHAPES["ROUND"]
	local x, y

	if shapes[q] then
		x = cos * width
		y = sin * height
	else
		x = math.max(-width, math.min(cos * (math.sqrt(2 * width ^ 2) - 10), width))
		y = math.max(-height, math.min(sin * (math.sqrt(2 * height ^ 2) - 10), height))
	end

	return "CENTER", x, y
end

local function ApplyMinimapButtonPosition(button)
	InitDB()
	local degrees = GX_DB.minimap.degrees or 220
	local point, x, y = GetMinimapButtonPosition(degrees, 8)
	button:ClearAllPoints()
	button:SetPoint(point, Minimap, point, x, y)
end

local function OnMinimapButtonDragUpdate(self)
	local scale = Minimap:GetEffectiveScale()
	local minimapX, minimapY = Minimap:GetCenter()
	local cursorX, cursorY = GetCursorPosition()

	cursorX = cursorX / scale
	cursorY = cursorY / scale

	local degrees = math.deg(math.atan2(cursorY - minimapY, cursorX - minimapX)) % 360
	GX_DB.minimap.degrees = degrees
	ApplyMinimapButtonPosition(self)
end

local function CreateMinimapButton()
	local button = CreateFrame("Button", "GXMinimapButton", Minimap, "GXMinimapButtonTemplate")
	button:RegisterForClicks("LeftButtonUp", "RightButtonUp")
	button:RegisterForDrag("LeftButton")

	-- Circle mask on the icon matching Blizzard character/portrait style
	if button.Icon then
		local mask = button:CreateMaskTexture()
		mask:SetTexture("Interface\\CharacterFrame\\TempPortraitAlphaMask", "CLAMPTOBLACKADDITIVE", "CLAMPTOBLACKADDITIVE")
		mask:SetAllPoints(button.Icon)
		button.Icon:AddMaskTexture(mask)
	end

	button:SetScript("OnEnter", function(self)
		GameTooltip:SetOwner(self, "ANCHOR_LEFT")
		GameTooltip:AddLine("GX - Total Account Gold")
		GameTooltip:AddLine(GetStyledMoneyString(GetTotalGold()), 1, 1, 1)
		if GX_DB.warband and GX_DB.warband.gold and GX_DB.warband.gold > 0 then
			GameTooltip:AddLine("Warband Bank: " .. GetStyledMoneyString(GX_DB.warband.gold), 0.8, 0.8, 0.8)
		end
		GameTooltip:AddLine(" ")
		GameTooltip:AddLine("Left-click: toggle gold window | Right-click: options", 0.6, 0.8, 1)
		GameTooltip:AddLine("Drag to move around minimap", 0.4, 0.4, 0.4)
		GameTooltip:Show()
	end)

	button:SetScript("OnLeave", function(self)
		GameTooltip:Hide()
	end)

	button:SetScript("OnMouseDown", function(self, mouseButton)
		if self.Icon and self.Icon.AdjustPointsOffset then
			self.Icon:AdjustPointsOffset(1, -1)
		end
	end)

	button:SetScript("OnMouseUp", function(self, mouseButton)
		if self.Icon and self.Icon.AdjustPointsOffset then
			self.Icon:AdjustPointsOffset(-1, 1)
		end
	end)

	button:SetScript("OnDragStart", function(self)
		self.wasDragged = true
		self:LockHighlight()
		self:SetScript("OnUpdate", OnMinimapButtonDragUpdate)
	end)

	button:SetScript("OnDragStop", function(self)
		self:UnlockHighlight()
		self:SetScript("OnUpdate", nil)
		C_Timer.After(0.05, function()
			self.wasDragged = false
		end)
	end)

	button:SetScript("OnClick", function(self, mouseButton)
		if self.wasDragged then
			return
		end
		if mouseButton == "RightButton" then
			OpenSettingsPanel()
		else
			SaveCurrentCharacterGold()
			ToggleTotalGoldFrame()
		end
	end)

	ApplyMinimapButtonPosition(button)
	button:SetShown(not GX_DB or not GX_DB.minimap or GX_DB.minimap.shown ~= false)
	return button
end

-- ===========================================================================
-- Addon Compartment (Blizzard native minimap menu in Dragonflight / TWW)
-- ===========================================================================

function GX_OnAddonCompartmentClick(addonName, mouseButton)
	if mouseButton == "RightButton" then
		OpenSettingsPanel()
	else
		SaveCurrentCharacterGold()
		ToggleTotalGoldFrame()
	end
end

function GX_OnAddonCompartmentEnter(addonName, button)
	GameTooltip:SetOwner(button, "ANCHOR_LEFT")
	GameTooltip:AddLine("GX - Total Account Gold")
	GameTooltip:AddLine(GetStyledMoneyString(GetTotalGold()), 1, 1, 1)
	if GX_DB.warband and GX_DB.warband.gold and GX_DB.warband.gold > 0 then
		GameTooltip:AddLine("Warband Bank: " .. GetStyledMoneyString(GX_DB.warband.gold), 0.8, 0.8, 0.8)
	end
	GameTooltip:AddLine(" ")
	GameTooltip:AddLine("Left-click: toggle gold window | Right-click: options", 0.6, 0.8, 1)
	GameTooltip:Show()
end

function GX_OnAddonCompartmentLeave(addonName, button)
	GameTooltip:Hide()
end

local function RegisterAddonCompartment()
	if AddonCompartmentFrame and AddonCompartmentFrame.RegisterAddon then
		AddonCompartmentFrame:RegisterAddon({
			text = "GX - Gold Export",
			icon = COIN_ICON,
			notCheckable = true,
			registerForAnyClick = true,
			func = function(_, menuInputData)
				local mouseButton = type(menuInputData) == "table" and menuInputData.buttonName or menuInputData
				GX_OnAddonCompartmentClick("GX", mouseButton)
			end,
			funcOnEnter = function(button)
				GX_OnAddonCompartmentEnter("GX", button)
			end,
			funcOnLeave = function(button)
				GX_OnAddonCompartmentLeave("GX", button)
			end,
		})
	end
end

-- ===========================================================================
-- Options panel (Esc -> Options -> AddOns -> GX - Gold Export)
-- Built through the modern Settings canvas API. All values are applied
-- immediately and live in GX_DB.
-- ===========================================================================

local function GetSettingsCategoryID()
	if not GXSettingsCategory and RegisterGXSettings then
		RegisterGXSettings()
	end
	if not GXSettingsCategory then
		return nil
	end
	if type(GXSettingsCategory) == "table" then
		if GXSettingsCategory.GetID then
			return GXSettingsCategory:GetID()
		elseif GXSettingsCategory.ID then
			return GXSettingsCategory.ID
		end
	elseif type(GXSettingsCategory) == "number" then
		return GXSettingsCategory
	end
	return nil
end

OpenSettingsPanel = function()
	local categoryID = GetSettingsCategoryID()
	if categoryID and Settings and Settings.OpenToCategory then
		local ok = pcall(Settings.OpenToCategory, categoryID)
		if not ok then
			pcall(Settings.OpenToCategory, GXSettingsCategory)
		end
	elseif SettingsPanel and SettingsPanel.Show then
		SettingsPanel:Show()
	else
		print(addonLabel() .. "Options panel not available (Settings addon missing).")
	end
end

local function BuildSettingsPanel(panel)
	panel:SetSize(460, 340)

	local title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
	title:SetPoint("TOPLEFT", 20, -20)
	title:SetText("GX - Gold Export")

	local totalLabel = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	totalLabel:SetPoint("TOPLEFT", 20, -52)
	totalLabel:SetJustifyH("LEFT")
	panel.TotalLabel = totalLabel

	-- Minimap button ---------------------------------------------------------
	local mmHeader = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
	mmHeader:SetPoint("TOPLEFT", 20, -88)
	mmHeader:SetText("Minimap button")

	local minimapCheck = CreateFrame("CheckButton", nil, panel, "UICheckButtonTemplate")
	minimapCheck:SetSize(26, 26)
	minimapCheck:SetPoint("TOPLEFT", 20, -108)
	minimapCheck:SetScript("OnClick", function(self)
		InitDB()
		GX_DB.minimap.shown = self:GetChecked() == true
		if GXMinimapButton then
			GXMinimapButton:SetShown(GX_DB.minimap.shown)
		end
	end)
	panel.MinimapCheck = minimapCheck

	local mmCheckLabel = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	mmCheckLabel:SetPoint("LEFT", minimapCheck, "RIGHT", 8, 0)
	mmCheckLabel:SetText("Show circular icon next to the minimap")

	-- Autosave ---------------------------------------------------------------
	local autoHeader = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
	autoHeader:SetPoint("TOPLEFT", 20, -152)
	autoHeader:SetText("Autosave")

	local autoSlider = CreateFrame("Slider", nil, panel, "OptionsSliderTemplate")
	autoSlider:SetSize(300, 17)
	autoSlider:SetPoint("TOPLEFT", 20, -175)
	autoSlider:SetMinMaxValues(0, 240)
	autoSlider:SetValueStep(5)
	autoSlider:SetObeyStepOnDrag(true)
	panel.AutoSlider = autoSlider

	local autoValue = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	autoValue:SetPoint("LEFT", autoSlider, "RIGHT", 12, 0)
	autoValue:SetText("")
	panel.AutoValue = autoValue

	local function SliderMinutes(slider)
		return math.floor(slider:GetValue() + 0.5)
	end

	autoSlider:SetScript("OnValueChanged", function(self, value)
		local minutes = math.floor(value + 0.5)
		autoValue:SetText(minutes <= 0 and "off" or (minutes .. " min"))
	end)
	autoSlider:SetScript("OnMouseUp", function(self)
		SetAutosave(SliderMinutes(self) * 60, true)
	end)

	local autoInfo = panel:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
	autoInfo:SetPoint("TOPLEFT", 20, -210)
	autoInfo:SetJustifyH("LEFT")
	autoInfo:SetText("0 = disabled. Every N minutes the UI reloads so the client writes\nSavedVariables to disk and watcher.py updates totalgold.txt for OBS.")

	local noInstancesCheck = CreateFrame("CheckButton", nil, panel, "UICheckButtonTemplate")
	noInstancesCheck:SetSize(26, 26)
	noInstancesCheck:SetPoint("TOPLEFT", 20, -244)
	noInstancesCheck:SetScript("OnClick", function(self)
		InitDB()
		GX_DB.autosaveNoInstances = self:GetChecked() == true
	end)
	panel.NoInstancesCheck = noInstancesCheck

	local noInstancesLabel = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	noInstancesLabel:SetPoint("LEFT", noInstancesCheck, "RIGHT", 8, 0)
	noInstancesLabel:SetText("Skip autosave reload while in an instance")

	-- Buttons ----------------------------------------------------------------
	local showButton = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
	showButton:SetSize(190, 26)
	showButton:SetText("Show gold window")
	showButton:SetPoint("BOTTOMLEFT", 20, 26)
	showButton:SetScript("OnClick", function()
		SaveCurrentCharacterGold()
		ShowTotalGoldFrame()
	end)

	local reloadButton = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
	reloadButton:SetSize(190, 26)
	reloadButton:SetText("Save & reload now")
	reloadButton:SetPoint("LEFT", showButton, "RIGHT", 10, 0)
	reloadButton:SetScript("OnClick", function()
		SaveCurrentCharacterGold()
		ReloadUI()
	end)

	local resetButton = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
	resetButton:SetSize(190, 26)
	resetButton:SetText("Reset saved gold")
	resetButton:SetPoint("LEFT", reloadButton, "RIGHT", 10, 0)
	resetButton:SetScript("OnClick", function()
		GX_DB.characters = {}
		GX_DB.warband = { gold = 0, lastSeen = time() }
		print(addonLabel() .. "All saved gold data cleared.")
	end)

	-- Sync the controls with GX_DB whenever the settings panel is shown.
	panel.OnRefresh = function(self)
		InitDB()
		self.TotalLabel:SetText("Current total: " .. GetStyledMoneyString(GetTotalGold()))
		self.MinimapCheck:SetChecked(not GX_DB.minimap or GX_DB.minimap.shown ~= false)
		self.NoInstancesCheck:SetChecked(GX_DB.autosaveNoInstances ~= false)
		local minutes = math.max(math.floor((GX_DB.autosaveMinutes or 0) + 0.5), 0)
		self.AutoSlider:SetValue(minutes)
		self.AutoValue:SetText(minutes <= 0 and "off" or (minutes .. " min"))
	end
end

-- Register the category into Blizzard's in-game AddOns settings list.
do
	RegisterGXSettings = function()
		if GXSettingsCategory then
			return GXSettingsCategory
		end
		if not (Settings and Settings.RegisterCanvasLayoutCategory) then
			print(addonLabel() .. "Settings API not available - options panel skipped.")
			return nil
		end
		InitDB()
		local panel = CreateFrame("Frame", "GXSettingsFrame", UIParent)
		BuildSettingsPanel(panel)
		GXSettingsPanel = panel
		local category = Settings.RegisterCanvasLayoutCategory(panel, "GX - Gold Export")
		GXSettingsCategory = category
		Settings.RegisterAddOnCategory(category)
		return category
	end

	-- Fires immediately (GX is already loaded at this point) or on ADDON_LOADED.
	if EventUtil and EventUtil.ContinueOnAddOnLoaded then
		EventUtil.ContinueOnAddOnLoaded("GX", function()
			RegisterGXSettings()
		end)
	else
		RegisterGXSettings()
	end
end

-- ===========================================================================
-- Slash command: /gx
-- ===========================================================================

SLASH_GX1 = "/gx"

SlashCmdList.GX = function(msg)
	local cmd, rest = string.match(msg or "", "^(%S*)%s*(.-)$")
	cmd = string.lower(cmd or "")

	if cmd == "" or cmd == "help" then
		print(addonLabel() .. "Commands:")
		print(addonLabel() .. "  |cffffffff/gx show|r        - open the compact gold window")
		print(addonLabel() .. "  |cffffffff/gx export|r      - open a copyable export box")
		print(addonLabel() .. "  |cffffffff/gx autosave|r    - toggle periodic reload (default 60 min)")
		print(addonLabel() .. "  |cffffffff/gx autosave <min>|r  - set interval (>= 1)")
		print(addonLabel() .. "  |cffffffff/gx autosave off|r    - stop autosave")
		print(addonLabel() .. "  |cffffffff/gx settings|r    - open the options panel (Esc -> Options -> AddOns)")
		print(addonLabel() .. "  |cffffffff/gx minimap|r     - show / hide the circular minimap icon")
		print(addonLabel() .. "  |cffffffff/gx reset|r       - clear all saved gold data")
	elseif cmd == "show" then
		SaveCurrentCharacterGold()
		ToggleTotalGoldFrame()
	elseif cmd == "export" then
		SaveCurrentCharacterGold()
		ToggleExportFrame()
	elseif cmd == "autosave" then
		if rest == "" then
			if GX_DB.autosaveMinutes and GX_DB.autosaveMinutes > 0 then
				SetAutosave(0)
			else
				SetAutosave(AUTOSAVE_DEFAULT_S)
			end
		elseif rest == "off" or rest == "0" or rest == "stop" then
			SetAutosave(0)
		else
			local minutes = tonumber(rest)
			if minutes and minutes >= 1 then
				SetAutosave(minutes * 60)
			else
				print(addonLabel() .. "Invalid interval. Usage: /gx autosave <minutes> (>= 1).")
			end
		end
	elseif cmd == "settings" or cmd == "options" then
		OpenSettingsPanel()
	elseif cmd == "minimap" then
		InitDB()
		GX_DB.minimap.shown = not (GX_DB.minimap.shown ~= false)
		if GXMinimapButton then
			GXMinimapButton:SetShown(GX_DB.minimap.shown)
		end
		print(addonLabel() .. (GX_DB.minimap.shown and "Minimap icon shown." or "Minimap icon hidden."))
	elseif cmd == "reset" then
		GX_DB.characters = {}
		GX_DB.warband = { gold = 0, lastSeen = time() }
		print(addonLabel() .. "All saved gold data cleared.")
	else
		print(addonLabel() .. "Unknown command '" .. cmd .. "'. Type /gx for help.")
	end
end

-- ===========================================================================
-- Event handling
-- ===========================================================================

local eventFrame = CreateFrame("Frame")
eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:RegisterEvent("PLAYER_LOGIN")
eventFrame:RegisterEvent("PLAYER_MONEY")
eventFrame:RegisterEvent("ACCOUNT_MONEY")
eventFrame:RegisterEvent("BANKFRAME_OPENED")
eventFrame:SetScript("OnEvent", function(self, event, ...)
	if event == "ADDON_LOADED" then
		local addon = ...
		if addon == "GX" then
			InitDB()
		end
	elseif event == "PLAYER_LOGIN" then
		InitDB()
		-- Fresh login: save the live value right away and re-arm autosave.
		SaveCurrentCharacterGold()
		UpdateWarbandBankGold()
		ArmAutosaveFromSaved()
		RegisterAddonCompartment()
		-- Create the draggable minimap icon once the UI is fully up.
		if not GXMinimapButton then
			GXMinimapButton = CreateMinimapButton()
		end
	elseif event == "PLAYER_MONEY" then
		-- Money changed: update stored value + any open frame.
		SaveCurrentCharacterGold()
		UpdateWarbandBankGold()
		RefreshOpenFrames()
	elseif event == "ACCOUNT_MONEY" or event == "BANKFRAME_OPENED" then
		UpdateWarbandBankGold()
		RefreshOpenFrames()
	end
end)

-- ===========================================================================
-- Self-test at load: make sure the essential FrameXML helpers we rely on
-- exist. If something is missing we report it instead of erroring later.
-- ===========================================================================

do
	local missing = {}
	if not GetMoneyString then
		tinsert(missing, "GetMoneyString")
	end
	if not UISpecialFrames then
		tinsert(missing, "UISpecialFrames")
	end
	if not EventUtil then
		tinsert(missing, "EventUtil")
	end
	if next(missing) then
		print(addonLabel() .. "WARNING: missing FrameXML helpers - " .. table.concat(missing, ", "))
	end
end