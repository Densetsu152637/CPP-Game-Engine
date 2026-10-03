-- Generic developer progression; all IDs and text are artificial test content.
local M = {}
local function copy(value)
    if type(value) ~= "table" then return value end
    local result = {}
    for key, item in pairs(value) do result[key] = copy(item) end
    return result
end
M.copy = copy
function M.new(variant)
    assert(variant == "male" or variant == "female", "unknown fixture variant")
    return {version=2, contentVersion=1, variant=variant, room="room:A",
        events={}, quests={sample="available"}, reveals={}, provenance={},
        scene="idle", flock=false, followers=2, phase=0, mode="interactive"}
end
function M.restore(value)
    if type(value) ~= "table" then return nil, "snapshot must be an object" end
    local restored = copy(value)
    if restored.version == 1 then
        restored.provenance = restored.provenance or {}
        restored.contentVersion = 1
        restored.version = 2
    end
    if restored.version ~= 2 or restored.contentVersion ~= 1 then
        return nil, "unsupported snapshot/content version"
    end
    if restored.variant ~= "male" and restored.variant ~= "female" then return nil, "unknown variant ID" end
    if restored.room ~= "room:A" and restored.room ~= "room:B" then return nil, "unknown room ID" end
    if type(restored.events) ~= "table" or type(restored.quests) ~= "table" or
        type(restored.reveals) ~= "table" or type(restored.provenance) ~= "table" then
        return nil, "missing progress collections"
    end
    local valid = {available=true, active=true, completed=true, reward_recorded=true}
    if not valid[restored.quests.sample] then return nil, "unknown quest state" end
    for id in pairs(restored.reveals) do
        if id ~= "passage:sample" then return nil, "unknown passage ID: " .. tostring(id) end
    end
    return restored
end
function M.reveal(current, passage, source)
    assert(passage == "passage:sample", "unknown passage ID")
    local next = copy(current)
    local changed = not next.reveals[passage]
    next.reveals[passage] = true
    next.provenance[passage] = next.provenance[passage] or {}
    next.provenance[passage][source] = true
    return next, changed
end
function M.event(current, event)
    local next = copy(current)
    if next.events[event] then return next, false end
    next.events[event] = true
    if event == "quest:accept" then
        if next.quests.sample == "available" then next.quests.sample = "active" end
        if next.events["quest:objective"] and next.quests.sample == "active" then next.quests.sample = "completed" end
    elseif event == "quest:objective" then
        if next.quests.sample == "active" then next.quests.sample = "completed" end
    elseif event == "quest:reward" then
        if next.quests.sample ~= "completed" then
            next.events[event] = nil -- premature reward must remain retryable
            return next, false
        end
        next.quests.sample = "reward_recorded"
        return M.reveal(next, "passage:sample", "quest:sample")
    elseif event == "story:reveal" then
        return M.reveal(next, "passage:sample", "story:sample")
    elseif event == "encounter:defeat" then
        next.scene = "retry" -- ordinary failure cannot trigger authored healing
    elseif event == "scene:injury" then
        next.scene = "injury"
    elseif event == "scene:healed" then
        assert(next.events["scene:injury"], "healing requires authored injury")
        next.scene = "idle"
    end
    return next, true
end
function M.page(current)
    if current.reveals["passage:sample"] then
        return "DEVELOPER PASSAGE A\nOriginal placeholder text. No scripture.\nA shared reward retains its stable ID and both sources.\nUnicode coverage: Ω 十\n" ..
            string.rep("Long page: scroll to inspect clipped text and return safely.\n", 12)
    end
    return "UNREVEALED\n[locked placeholder]\nNo readable passage is exposed. Confirm or Back to exit."
end
return M
