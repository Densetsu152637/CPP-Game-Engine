local P = require("scripts.progression")
return {on_create=function()
    for _, variant in ipairs({"male", "female"}) do
        local s = P.new(variant)
        assert(not P.page(s):find("Original placeholder", 1, true), "locked text leaked")
        s = P.event(s, "quest:reward")
        assert(s.quests.sample == "available" and not s.reveals["passage:sample"])
        s = P.event(s, "quest:objective") -- objective before accepting must remain observable
        s = P.event(s, "quest:accept")
        assert(s.quests.sample == "completed")
        local accepted = s
        s = P.event(s, "quest:accept")
        assert(accepted.events["quest:accept"] and s.quests.sample == "completed")
        s = P.event(s, "quest:reward")
        s = P.event(s, "quest:reward")
        s = P.event(s, "story:reveal")
        assert(s.quests.sample == "reward_recorded" and s.reveals["passage:sample"])
        assert(s.provenance["passage:sample"]["quest:sample"] and s.provenance["passage:sample"]["story:sample"])
        local count = 0; for _ in pairs(s.reveals) do count=count+1 end
        assert(count == 1 and s.followers == 2)
        s = P.event(s, "encounter:defeat")
        assert(s.scene == "retry" and not s.events["scene:healed"])
        s = P.event(s, "scene:injury"); s = P.event(s, "scene:healed")
        s = P.event(s, "scene:healed")
        assert(s.scene == "idle" and s.followers == 2)
        local migrated = P.copy(s); migrated.version=1; migrated.contentVersion=nil
        assert(P.restore(migrated).version == 2)
        local newer=P.copy(s); newer.version=99; assert(not P.restore(newer))
        local missing=P.copy(s); missing.reveals["passage:missing"]=true; assert(not P.restore(missing))
        assert(P.restore(s).variant == variant)
    end
    print("DESKTOP2D_PURE_PASS")
end}
