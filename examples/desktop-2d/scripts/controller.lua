local P = require("scripts.progression")
local bindings = require("scripts.bindings")
local M = {}
local ROOM_ASSETS = {["room:A"]="asset:room-a", ["room:B"]="asset:room-b"}
local function require_ok(result, error_message)
    assert(result, tostring(error_message or "host rejected fixture operation"))
    return result
end
local function contains(items, value)
    for _, item in ipairs(items) do if item == value then return true end end
    return false
end
local function verify_progress(s)
    assert(s.quests.sample == "reward_recorded" and s.reveals["passage:sample"], "reward lost")
    assert(s.provenance["passage:sample"]["quest:sample"] and s.provenance["passage:sample"]["story:sample"], "provenance lost")
    local count=0; for _ in pairs(s.reveals) do count=count+1 end
    assert(count == 1 and s.followers == 2, "duplicate reward/follower")
end
function M.new(options)
    options = options or {}
    local s, initialized, loop_voice, panel, status, elapsed = nil, false, nil, nil, "", 0
    local current_room, last_ui, scene_time, muted = "room:A", nil, 0, false
    local preferences={textScale=1,reducedMotion=false}
    local function publish() require_ok(state.write(s)) end
    local function event(name)
        local changed; s, changed = P.event(s, name); publish()
        if changed and (name == "quest:reward" or name == "story:reveal") then
            local voice, err=audio.play("asset:cue", false, "effects", .5)
            if not voice then engine.log("CUE UNAVAILABLE: " .. tostring(err)) end
        end
        return changed
    end
    local function show(id, text, choices)
        panel = id
        require_ok(ui.open({id=id, font="asset:font", text=text, x=12, y=48, width=296, height=118,
            scale=preferences.textScale or 1, modal=true, choices=choices}))
    end
    local function close()
        if panel then ui.close(panel); panel=nil end
    end
    local function hud()
        local text = "DESKTOP 2D LAB | " .. current_room .. " | " .. s.variant .. "\n" ..
            "WASD move E inspect J page R rest L load\nH scene B defeat F rebind M mute P variant\n" .. status
        ui.set_text("hud", text)
    end
    local function request_room(room, spawn)
        close()
        local ok, err=engine.change_scene(ROOM_ASSETS[room], spawn or "entry", "actor:player")
        if not ok then status="ROOM FAILED: " .. tostring(err); return false end
        s.room=room; s.flock=false; publish()
        return true
    end
    local function save_rest()
        s.flock=true
        local p=self.get_position(); s.position={x=p.x,y=p.y}
        publish()
        local ok, err=save.write("desktop-slot", s)
        if ok then
            status="SAVE CONFIRMED. Progress survives restart."
            local voice, sound_error=audio.play("asset:cue", false, "effects", .4)
            if not voice then engine.log("SAVE CUE UNAVAILABLE: " .. tostring(sound_error)) end
        else status="SAVE FAILED. Previous save retained. " .. tostring(err) end
        return ok, err
    end
    local function restore()
        local snapshot, err=save.read("desktop-slot")
        if not snapshot then status="LOAD FAILED: " .. tostring(err); return false end
        local restored, reason=P.restore(snapshot)
        if not restored then status="LOAD REJECTED: " .. reason; return false end
        s=restored; s.mode="interactive"; s.restorePending=true; publish()
        if s.room ~= current_room then return request_room(s.room) end
        if s.position then self.set_position(s.position.x,s.position.y,0) end
        s.restorePending=false; publish(); status="LOAD CONFIRMED. Stable progress restored."
        return true
    end
    local function start_scene()
        if s.scene == "injury" or s.events["scene:healed"] then return end
        s.scene="injury"; scene_time=0
        require_ok(engine.lock_controls("authored-scene", true))
        local bird=engine.find_entity("actor:scene-dove")
        if bird then engine.sprite_visible(bird,true); engine.set_position(bird,0,3,0) end
        show("scene", "DEVELOPER AUTHORED INJURY\nA staged descent is separate from ordinary defeat.\nBack cancels safely. Wait to complete once.")
        publish()
    end
    local function cancel_scene()
        close(); require_ok(engine.lock_controls("authored-scene",false))
        engine.reset_camera(); s.scene="idle"; scene_time=0
        local bird=engine.find_entity("actor:scene-dove"); if bird then engine.sprite_visible(bird,false) end
        status="SCENE CANCELLED. Controls restored; no healing awarded."; publish()
    end
    local function finish_scene()
        event("scene:injury"); event("scene:healed")
        close(); require_ok(engine.lock_controls("authored-scene",false)); engine.reset_camera()
        local bird=engine.find_entity("actor:scene-dove"); if bird then engine.sprite_visible(bird,false) end
        status="SCENE COMPLETED ONCE. Followers remain two."; publish()
    end
    local function save_settings(rebound)
        local remapped=P.copy(bindings)
        if rebound then remapped.interact={"Key:Q","Gamepad:A"} end
        local settings_data={version=1, bindings=remapped, audio={master=.8,music=.25,effects=.7,dialogue=.9},
            reducedMotion=true, textScale=1, interactRebound=rebound}
        local ok, err=settings.write(settings_data)
        if ok then preferences=settings_data; status=rebound and "SETTINGS SAVED. Interact now Q; restart retains it." or "SETTINGS SAVED."
        else status="SETTINGS FAILED: " .. tostring(err) end
        return ok, err
    end
    local function consume_ui()
        local e=ui.event()
        while e do
            last_ui=e
            if e.type == "cancel" then
                if e.panel == "scene" then cancel_scene() else close() end
            elseif e.type == "confirm" then
                if e.panel == "variant" then
                    s.variant=e.selection == 1 and "male" or "female"; publish(); close()
                elseif e.panel == "quest" then
                    if e.selection == 1 then event("quest:accept") end
                    s.acceptedChoice=e.selection; publish(); close()
                elseif e.panel ~= "scene" then close() end
            end
            e=ui.event()
        end
    end
    local function follow(dt)
        local p=self.get_position()
        for index=1,2 do
            local bird=engine.find_entity("actor:dove" .. index)
            if bird then
                local point=engine.get_position(bird)
                local tx=p.x-index*.65; local ty=p.y+.6
                if s.flock then tx=-4+(index-1)*1.1; ty=1.2+(index-1)*.45 end
                local dx=tx-point.x; local dy=ty-point.y
                if dx*dx+dy*dy > 36 then
                    engine.set_position(bird,tx,ty,0)
                    local safe=engine.safe_position(bird,2,.25)
                    if safe then engine.set_position(bird,safe.x,safe.y,0) end
                else
                    require_ok(engine.move(bird,dx*math.min(1,dt*4),dy*math.min(1,dt*4)))
                end
            end
        end
    end
    local function initialize()
        current_room=engine.find_entity("room:B") and "room:B" or "room:A"
        local shared=state.read()
        if options.test == "restore" then
            local snapshot, err=save.read("desktop-slot"); require_ok(snapshot,err)
            s=require_ok(P.restore(snapshot)); assert(s.variant == options.variant, "wrong saved branch")
            verify_progress(s)
            local cfg, cfg_error=settings.read(); require_ok(cfg,cfg_error)
            assert(cfg.interactRebound and cfg.bindings.interact[1] == "Key:Q" and cfg.audio.music == .25, "settings/rebind lost at restart")
            s.mode="restore-test"; s.phase=0; s.room=current_room
            print("DESKTOP2D_RESTART_RESTORE_PASS:" .. s.variant)
        elseif shared and shared.version then s=require_ok(P.restore(shared))
        else s=P.new(options.variant or "male"); s.mode=options.test == "new" and "new-test" or "interactive" end
        if s.restorePending and s.room == current_room and s.position then
            self.set_position(s.position.x,s.position.y,0); s.restorePending=false
        end
        s.room=current_room; publish()
        local saved_settings=settings.read()
        if saved_settings then preferences=saved_settings end
        require_ok(ui.open({id="hud",font="asset:font",text="DESKTOP 2D LAB",x=8,y=6,width=304,height=38,scale=1,modal=false}))
        loop_voice=audio.play("asset:loop",true,"music",.18)
        if not loop_voice then engine.log("MUSIC UNAVAILABLE; VISUAL WALKTHROUGH STILL WORKS") end
        initialized=true
    end
    local function test_step()
        local phase=s.phase
        if s.mode == "restore-test" then
            if phase == 0 then
                local changed=event("quest:reward"); assert(not changed)
                changed=event("story:reveal"); assert(not changed)
                changed=event("scene:healed"); assert(not changed)
                verify_progress(s); s.phase=1; publish(); require_ok(request_room("room:B")); return
            elseif phase == 1 then
                assert(current_room == "room:B"); verify_progress(s)
                s.mode="complete"; publish(); print("DESKTOP2D_RESTART_PASS:" .. s.variant)
            end
            return
        end
        if s.mode ~= "new-test" then return end
        if phase == 0 then
            assert(not s.reveals["passage:sample"] and not P.page(s):find("Original placeholder",1,true))
            assert(engine.find_entity("actor:player") == self.id())
        elseif phase == 1 then
            local result=require_ok(self.move(100,0)); assert(#result.contacts > 0 and result.x < 8, "wall did not stop sweep")
            self.set_position(-2,0,0)
        elseif phase == 2 then
            assert(contains(require_ok(self.overlaps()),"interact:book"), "stable trigger overlap missing")
        elseif phase == 3 then
            local observed=false
            for _, trigger in ipairs(engine.triggers()) do
                if (trigger.first == "actor:player" and trigger.second == "interact:book") or
                    (trigger.second == "actor:player" and trigger.first == "interact:book") then observed=true end
            end
            assert(observed,"trigger event missing")
        elseif phase == 4 then show("journal",P.page(s))
        elseif phase == 5 then
            assert(not input.held("move_right") and input.value("move_right") == 0, "modal leaked world action")
        elseif phase == 6 then
            assert(last_ui and last_ui.type == "confirm" and last_ui.panel == "journal", "inject ui_confirm:6")
            assert(not input.pressed("interact"),"dismissal leaked underlying interact")
        elseif phase == 7 then show("quest","DEVELOPER QUEST\nChoose a placeholder response.",{"Accept","Later"})
        elseif phase == 8 then assert(last_ui and last_ui.type == "selection" and last_ui.selection == 2,"inject ui_down:8")
        elseif phase == 9 then assert(s.acceptedChoice == 2,"inject ui_confirm:9")
        elseif phase == 10 then
            assert(not event("quest:reward")); event("quest:accept"); assert(not event("quest:accept"))
        elseif phase == 11 then event("quest:objective"); assert(s.quests.sample == "completed")
        elseif phase == 12 then
            require_ok(save_rest()); local middle=require_ok(save.read("desktop-slot"))
            assert(middle.quests.sample == "completed" and not middle.reveals["passage:sample"],"midquest state lost")
        elseif phase == 13 then assert(event("quest:reward")); assert(not event("quest:reward"))
        elseif phase == 14 then event("story:reveal"); verify_progress(s)
        elseif phase == 15 then show("journal",P.page(s)); require_ok(ui.scroll("journal",32))
        elseif phase == 16 then assert(last_ui and last_ui.type == "cancel" and last_ui.panel == "journal","inject ui_back:16")
        elseif phase == 17 then start_scene()
        elseif phase == 18 then
            assert(last_ui and last_ui.type == "cancel" and last_ui.panel == "scene","inject ui_back:18")
            assert(s.scene == "idle" and not s.events["scene:healed"],"cancellation granted healing")
        elseif phase == 19 then event("encounter:defeat"); assert(s.scene == "retry" and not s.events["scene:healed"])
        elseif phase == 20 then start_scene(); assert(s.scene == "injury")
        elseif phase == 21 then finish_scene(); verify_progress(s); assert(s.events["scene:healed"])
        elseif phase == 22 then require_ok(save_settings(true)); require_ok(save_rest())
        elseif phase == 23 then s.phase=24; publish(); require_ok(request_room("room:B")); return
        elseif phase == 24 then
            assert(current_room == "room:B"); verify_progress(s)
            assert(engine.find_entity("actor:dove1") and engine.find_entity("actor:dove2"))
            require_ok(self.safe_position(2,.25))
        elseif phase == 25 then
            local ok=engine.change_scene("asset:missing-room","entry","actor:player")
            assert(not ok,"missing scene accepted")
        elseif phase == 26 then
            assert(current_room == "room:B"); s.phase=27; publish(); require_ok(request_room("room:A")); return
        elseif phase == 27 then
            assert(current_room == "room:A"); verify_progress(s); require_ok(save_rest())
            s.mode="complete"; publish(); print("DESKTOP2D_WALKTHROUGH_PASS:" .. s.variant)
        end
        s.phase=phase+1; publish()
    end
    local function interact()
        local target=nil
        -- Sorted stable overlap IDs select deterministically, including overlapping objects.
        local overlaps=require_ok(self.overlaps()); table.sort(overlaps)
        for _, id in ipairs(overlaps) do if id:sub(1,9) == "interact:" then target=id; break end end
        if target == "interact:book" then show("journal",P.page(s))
        elseif target == "interact:quest" then
            if s.quests.sample == "completed" then event("quest:reward"); show("dialogue","REWARD RECORDED\nOne stable developer passage. Sources are retained.")
            else show("quest","DEVELOPER QUEST\nAccept, visit the blue objective in room B, and return.",{"Accept","Later"}) end
        elseif target == "interact:objective" then event("quest:objective"); show("dialogue","OBJECTIVE OBSERVED\nReturn to the orange quest marker.")
        elseif target == "interact:exit" then request_room(current_room == "room:A" and "room:B" or "room:A")
        elseif target == "interact:rest" then save_rest(); show("dialogue",status)
        else status="Walk onto a marker, then inspect." end
    end
    return {
        on_update=function(dt)
            if not initialized then initialize() end
            elapsed=elapsed+dt; consume_ui()
            if s.mode == "new-test" or s.mode == "restore-test" then test_step()
            elseif s.mode == "interactive" then
                if input.pressed("journal") then show("journal",P.page(s)) end
                if input.pressed("choose_variant") then show("variant","DEVELOPER VARIANT\nBoth placeholders share the same required route.",{"Male","Female"}) end
                if input.pressed("interact") then interact() end
                if input.pressed("rest") then
                    if contains(require_ok(self.overlaps()),"interact:rest") then save_rest(); show("dialogue",status)
                    else status="Reach the green rest marker to save." end
                end
                if input.pressed("load") then restore() end
                if input.pressed("new_game") then
                    s=P.new(s.variant); publish(); request_room("room:A")
                end
                if input.pressed("remap") then save_settings(true) end
                if input.pressed("mute") then muted=not muted; require_ok(audio.volume("master",muted and 0 or .8)); status=muted and "MUTED. Visible feedback remains." or "AUDIO RESTORED." end
                if input.pressed("defeat_demo") then event("encounter:defeat"); status="ORDINARY DEFEAT: retry state, no healing." end
                if input.pressed("scene_demo") then start_scene() end
                if input.pressed("story_demo") then event("story:reveal"); status="DEVELOPER STORY REVEAL. Same passage ID." end
                local dx=input.value("move_right")-input.value("move_left")
                local dy=input.value("move_up")-input.value("move_down")
                local length=math.sqrt(dx*dx+dy*dy)
                if length > 1 then dx=dx/length; dy=dy/length end
                if dx ~= 0 or dy ~= 0 then
                    s.flock=false; require_ok(self.move(dx*3*dt,dy*3*dt)); publish()
                end
            end
            if s.scene == "injury" then
                scene_time=scene_time+dt
                local bird=engine.find_entity("actor:scene-dove")
                if bird then engine.set_position(bird,0,preferences.reducedMotion and 0 or math.max(0,3-scene_time),0) end
                require_ok(engine.set_camera(0,0))
                if s.mode == "interactive" and scene_time >= 3 then finish_scene() end
            end
            follow(dt)
            if preferences.reducedMotion then
                for _,id in ipairs({"actor:player","actor:dove1","actor:dove2","actor:scene-dove","effect:glow"}) do
                    local actor=engine.find_entity(id); if actor then engine.sprite_frame(actor,0) end
                end
            else
                local moving=input.held("move_left") or input.held("move_right") or input.held("move_up") or input.held("move_down")
                engine.sprite_frame(self.id(),(moving or s.mode == "new-test") and math.floor(elapsed*6)%4 or 0)
            end
            if math.floor(elapsed*6) ~= math.floor((elapsed-dt)*6) then hud() end
        end,
        on_destroy=function()
            close(); ui.close("hud"); engine.lock_controls("authored-scene",false)
            if loop_voice then audio.stop(loop_voice); loop_voice=nil end
        end
    }
end
return M
