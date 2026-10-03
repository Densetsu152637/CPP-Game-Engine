#include "interaction/action_input.h"
#include "project/gameplay_ui.h"
#include "test/test_assertions.h"
#include <cmath>
#include <limits>

void runInputUiTests() {
    using test::require;
    using namespace interaction;
    ActionMapper mapper({{"move",{"Key:W","Key:Up"}},{"confirm",{"Key:Enter"}},{"wheel",{"Mouse:WheelUp"}},{"axis",{"Gamepad:LeftXPositive"}}});
    RawInput raw; raw.down={"Key:W"}; auto f=mapper.sample(raw);
    require(f.pressed.contains("move")&&f.held.contains("move"),"initial press emits held and pressed");
    f=mapper.sample(raw);require(f.pressed.empty(),"OS repeat must not create duplicate edges");
    raw.down.insert("Key:Up");mapper.sample(raw);raw.down.erase("Key:W");f=mapper.sample(raw);require(f.held.contains("move")&&f.released.empty(),"second binding retains held");
    raw.down.clear();f=mapper.sample(raw);require(f.released.contains("move"),"last binding emits one release");require(mapper.sample(raw).released.empty(),"release emits once");
    raw.wheelY=2;require(mapper.sample(raw).pressed.contains("wheel"),"wheel pulse maps to action");raw.wheelY=0;require(mapper.sample(raw).released.contains("wheel"),"wheel pulse expires");
    raw.down={"Key:W"};mapper.sample(raw);raw.focused=false;require(mapper.sample(raw).released.contains("move"),"focus loss releases held action");raw.focused=true;require(!mapper.sample(raw).held.contains("move"),"focus regain quarantines held physical control");raw.down.clear();mapper.sample(raw);raw.down={"Key:W"};require(mapper.sample(raw).pressed.contains("move"),"release rearms focus control");
    raw.down.clear();raw.gamepadConnected=true;raw.axes["LeftX"]=0;mapper.sample(raw);raw.axes["LeftX"]=0.1f;require(!mapper.sample(raw).held.contains("axis"),"deadzone rejects drift");raw.axes["LeftX"]=0.6f;f=mapper.sample(raw);require(f.pressed.contains("axis")&&std::abs(f.values.at("axis")-.5f)<.001f,"deadzone remaps analog range");raw.gamepadConnected=false;require(mapper.sample(raw).released.contains("axis"),"disconnect emits release");raw.gamepadConnected=true;require(!mapper.sample(raw).held.contains("axis"),"reconnect held axis quarantined");raw.axes["LeftX"]=0;mapper.sample(raw);raw.axes["LeftX"]=1;require(mapper.sample(raw).pressed.contains("axis"),"neutral rearms gamepad");
    require(!mapper.setDeadzone(1)&&!mapper.setDeadzone(NAN),"invalid deadzones rejected");
    require(!mapper.rebind({{"move",{"Key:DoesNotExist"}}}).valid()&&mapper.bindings().contains("confirm"),"invalid rebind atomic");
    auto conflicts=validateBindings({{"interact",{"Key:Enter"}},{"ui_confirm",{"Key:Enter"}}});require(conflicts.valid()&&conflicts.conflicts.size()==1,"shared UI controls permitted with advisory");require(!validateBindings({{"a",{"Key:Enter"}},{"b",{"Key:Enter"}}},true).valid(),"strict conflict policy optional");
    ActionMapper mixed({{"mixed",{"Key:W","Gamepad:A"}}}); RawInput mixedRaw; mixed.sample(mixedRaw); mixedRaw.gamepadConnected=true;mixedRaw.down={"Key:W","Gamepad:A"};require(mixed.sample(mixedRaw).pressed.contains("mixed"),"reconnected pad quarantine preserves independent keyboard binding");
    for(const auto& b:bindingRegistry()) require(findBinding(b.token)!=nullptr,"registry lookup stable");

    using namespace project::ui;
    require(validUtf8("ASCII \xE2\x82\xAC \xF0\x9F\x95\x8A")&&!validUtf8("\xC0\xAF")&&!validUtf8("\xED\xA0\x80")&&!validUtf8("\xF4\x90\x80\x80"),"UTF8 validates scalar values and overlong encodings");
    UiModel ui;PanelOptions panel;panel.id="book";panel.text="abcdefghij\n\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC";panel.rect={0,0,20,20};panel.lineHeight=10;panel.glyphAdvance=10;
    require(ui.open(panel),"valid panel opens");require(ui.panels().front().lines.size()==7,"long words and UTF8 wrap by complete scalar");for(auto& line:ui.panels().front().lines) require(validUtf8(line),"wrapped lines valid UTF8");
    require(ui.scroll("book",10000)&&ui.panels().front().scrollOffset==50,"scroll clamps at content end");require(ui.setText("book","x")&&ui.panels().front().scrollOffset==0,"replacement clamps stale scroll");require(!ui.setText("book","\xFF"),"invalid replacement rejected");require(ui.resize("book",{0,0,100,40}),"resize relayout");require(!ui.setText("book",std::string(65537,'x')),"oversized text rejected");require(!ui.setText("book",std::string(5000,'\n'))&&ui.panels().front().text=="x","excessive line layout rejects atomically");
    ActionFrame input;input.held={"interact"};input.pressed={"interact"};ui.tick(input);require(ui.gameplayFrame().held.empty(),"modal consumes gameplay");
    PanelOptions nested;nested.id="dialogue";nested.choices={"Yes","No"};require(ui.open(nested),"nested panel opens");require(!ui.panels().front().focused&&ui.panels().back().focused,"only top panel focused");ui.tick({});input={};input.pressed={"ui_down"};input.held=input.pressed;ui.tick(input);require(ui.panels().back().selected==1,"keyboard or mapped controller navigation");ui.pollEvent();
    input={};input.pressed={"ui_confirm"};input.held=input.pressed;ui.tick(input);auto event=ui.pollEvent();require(event&&event->type=="confirm"&&event->selection==1,"confirm reports selected choice");
    input={};input.pressed={"ui_back"};input.held=input.pressed;ui.tick(input);require(ui.panels().size()==1&&ui.panels().front().focused,"cancel pops modal and restores focus");ui.pollEvent();ui.tick(input);require(ui.panels().size()==1,"held dismissal cannot close parent");ui.tick({});ui.tick(input);require(ui.panels().empty()&&ui.gameplayFrame().pressed.empty(),"closing final modal cannot leak dismiss");ui.tick(input);require(ui.gameplayFrame().held.empty(),"closed dismiss held stays suppressed");ui.tick({});ui.tick(input);require(ui.gameplayFrame().pressed.contains("ui_back"),"neutral returns gameplay control");
    ui.clear();ui.tick({});panel.id="pointer";panel.text="";panel.rect={10,10,100,100};panel.choices={"one","two"};require(ui.open(panel),"pointer choice panel opens");ui.tick({});input={};input.pressed={"ui_click"};input.pointerX=0;input.pointerY=0;ui.tick(input);require(!ui.pollEvent(),"outside pointer ignored");input.pointerX=20;input.pointerY=35;ui.tick(input);event=ui.pollEvent();require(event&&event->type=="selection"&&event->selection==1,"inside pointer selects row");ui.pollEvent();ui.clear();require(ui.panels().empty()&&!ui.pollEvent()&&ui.gameplayFrame().pressed.empty(),"scene fault clear removes overlays events and input leakage");
    UiModel metricUi;
    PanelOptions tiny; tiny.id="tiny";tiny.text="";tiny.choices={"one"};tiny.lineHeight=1e-20f;
    require(metricUi.open(tiny),"finite positive tiny line height remains supported");metricUi.tick({});
    ActionFrame tinyClick;tinyClick.pressed={"ui_click"};tinyClick.pointerX=1;tinyClick.pointerY=1;
    metricUi.tick(tinyClick);require(!metricUi.pollEvent(),"huge floating pointer row rejected before integer conversion");
    tiny.id="underflow";tiny.lineHeight=std::numeric_limits<float>::denorm_min();tiny.scale=.5f;
    require(!metricUi.open(tiny)&&metricUi.panels().size()==1,"underflowed effective line height rejected atomically");
    tiny.id="advance-underflow";tiny.lineHeight=20;tiny.glyphAdvance=std::numeric_limits<float>::denorm_min();
    require(!metricUi.open(tiny),"underflowed effective glyph advance rejected");
    tiny.id="advance-overflow";tiny.scale=16;tiny.glyphAdvance=std::numeric_limits<float>::max();
    require(!metricUi.open(tiny),"overflowed effective glyph advance rejected");
    UiModel atlasOverflow([](std::string_view,char32_t){return std::numeric_limits<float>::max();});
    PanelOptions atlasPanel;atlasPanel.id="atlas-overflow";atlasPanel.text="x";atlasPanel.scale=16;
    require(!atlasOverflow.open(atlasPanel),"overflowed callback advance rejected");
    UiModel atlasUnderflow([](std::string_view,char32_t){return std::numeric_limits<float>::denorm_min();});
    atlasPanel.id="atlas-underflow";atlasPanel.scale=.5f;
    require(!atlasUnderflow.open(atlasPanel),"underflowed positive callback advance rejected");
}
