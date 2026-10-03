#include "action_input.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace interaction {
const std::vector<BindingDescriptor>& bindingRegistry() {
    static const auto registry = [] {
        std::vector<BindingDescriptor> r;
        auto add = [&](std::string name, Device device, int code, float direction = 1) { r.push_back({std::move(name), device, code, direction}); };
        for (char c = 'A'; c <= 'Z'; ++c) add("Key:" + std::string(1,c), Device::Keyboard, c);
        for (char c = '0'; c <= '9'; ++c) add("Key:" + std::string(1,c), Device::Keyboard, c);
        const std::vector<std::pair<std::string,int>> keys{{"Space",32},{"Apostrophe",39},{"Comma",44},{"Minus",45},{"Period",46},{"Slash",47},{"Semicolon",59},{"Equal",61},{"LeftBracket",91},{"Backslash",92},{"RightBracket",93},{"GraveAccent",96},{"World1",161},{"World2",162},{"Escape",256},{"Enter",257},{"Tab",258},{"Backspace",259},{"Insert",260},{"Delete",261},{"Right",262},{"Left",263},{"Down",264},{"Up",265},{"PageUp",266},{"PageDown",267},{"Home",268},{"End",269},{"CapsLock",280},{"ScrollLock",281},{"NumLock",282},{"PrintScreen",283},{"Pause",284},{"KeypadDecimal",330},{"KeypadDivide",331},{"KeypadMultiply",332},{"KeypadSubtract",333},{"KeypadAdd",334},{"KeypadEnter",335},{"KeypadEqual",336},{"LeftShift",340},{"LeftControl",341},{"LeftAlt",342},{"LeftSuper",343},{"RightShift",344},{"RightControl",345},{"RightAlt",346},{"RightSuper",347},{"Menu",348}};
        for (const auto& [name,code] : keys) add("Key:"+name,Device::Keyboard,code);
        for (int i=1;i<=25;++i) add("Key:F"+std::to_string(i),Device::Keyboard,289+i);
        for (int i=0;i<=9;++i) add("Key:Keypad"+std::to_string(i),Device::Keyboard,320+i);
        const std::vector<std::string> mouse{"Left","Right","Middle","4","5","6","7","8"};
        for (int i=0;i<8;++i) add("Mouse:"+mouse[i],Device::MouseButton,i);
        add("Mouse:WheelUp",Device::Wheel,1); add("Mouse:WheelDown",Device::Wheel,1,-1);
        add("Mouse:WheelRight",Device::Wheel,0); add("Mouse:WheelLeft",Device::Wheel,0,-1);
        const std::vector<std::string> buttons{"A","B","X","Y","LeftBumper","RightBumper","Back","Start","Guide","LeftThumb","RightThumb","DpadUp","DpadRight","DpadDown","DpadLeft"};
        for (int i=0;i<15;++i) add("Gamepad:"+buttons[i],Device::GamepadButton,i);
        const std::vector<std::string> axes{"LeftX","LeftY","RightX","RightY","LeftTrigger","RightTrigger"};
        for (int i=0;i<6;++i) { add("Gamepad:"+axes[i]+(i<4?"Positive":""),Device::GamepadAxis,i); if(i<4) add("Gamepad:"+axes[i]+"Negative",Device::GamepadAxis,i,-1); }
        return r;
    }();
    return registry;
}
const BindingDescriptor* findBinding(std::string_view token) {
    const auto& r=bindingRegistry(); auto i=std::find_if(r.begin(),r.end(),[&](const auto& b){return b.token==token;}); return i==r.end()?nullptr:&*i;
}
BindingValidation validateBindings(const BindingMap& bindings, bool strict) {
    BindingValidation result; std::map<std::string,std::vector<std::string>> owners;
    if(bindings.size()>256) result.errors.push_back("At most 256 actions are supported");
    for(const auto& [action,tokens]:bindings) {
        if(action.empty()) result.errors.push_back("Invalid action name: "+action);
        if(tokens.empty()||tokens.size()>16) result.errors.push_back("Action requires 1 to 16 bindings: "+action);
        std::set<std::string> unique;
        for(const auto& token:tokens) { if(!findBinding(token)) result.errors.push_back("Unknown binding: "+token); if(!unique.insert(token).second) result.errors.push_back("Duplicate binding: "+token); else owners[token].push_back(action); }
    }
    for(auto& [token,actions]:owners) if(actions.size()>1) { result.conflicts.push_back({token,actions}); if(strict) result.errors.push_back("Binding conflict: "+token); }
    return result;
}
ActionMapper::ActionMapper(BindingMap bindings) { auto result=rebind(std::move(bindings)); if(!result.valid()) throw std::invalid_argument(result.errors.front()); }
BindingValidation ActionMapper::rebind(BindingMap bindings,bool strict) { auto result=validateBindings(bindings,strict); if(result.valid()) { m_bindings=std::move(bindings); for(const auto& a:m_frame.held) m_blocked.insert(a); } return result; }
bool ActionMapper::setDeadzone(float value) { if(!std::isfinite(value)||value<0||value>=1) return false; m_deadzone=value; return true; }
void ActionMapper::clear() { m_frame={}; m_blocked.clear(); m_blockedGamepad.clear(); m_wasFocused=true; m_wasConnected=false; m_sampled=false; }
ActionFrame ActionMapper::sample(const RawInput& input) {
    ActionFrame next; next.focused=input.focused;
    next.pointerX=std::isfinite(input.pointerX)?input.pointerX:0; next.pointerY=std::isfinite(input.pointerY)?input.pointerY:0;
    if(input.focused) { next.wheelX=std::isfinite(input.wheelX)?input.wheelX:0; next.wheelY=std::isfinite(input.wheelY)?input.wheelY:0; }
    for(const auto& [action,tokens]:m_bindings) {
        float value=0;
        for(const auto& token:tokens) {
            const auto* b=findBinding(token); if(!b) continue;
            bool pad=b->device==Device::GamepadButton||b->device==Device::GamepadAxis;
            if(pad&&!input.gamepadConnected) continue;
            float v=0;
            if(b->device==Device::Wheel) v=std::max(0.f,(b->code?next.wheelY:next.wheelX)*b->direction);
            else if(b->device==Device::GamepadAxis) {
                const std::string axisNames[]{"LeftX","LeftY","RightX","RightY","LeftTrigger","RightTrigger"}; auto i=input.axes.find(axisNames[b->code]);
                if(i!=input.axes.end()&&std::isfinite(i->second)) { float raw=std::clamp(i->second*b->direction,0.f,1.f); if(raw>m_deadzone) v=(raw-m_deadzone)/(1-m_deadzone); }
            } else v=input.down.contains(token)?1.f:0.f;
            if(pad) { if(m_sampled&&!m_wasConnected&&v>0) m_blockedGamepad.insert(token); if(m_blockedGamepad.contains(token)) { if(v==0) m_blockedGamepad.erase(token); v=0; } }
            value=std::max(value,std::min(v,1.f));
        }
        // Quarantine held controls across focus transitions. Disconnect releases pad actions.
        if(!input.focused || (!m_wasFocused&&value>0)) { if(value>0) m_blocked.insert(action); value=0; }
        else if(m_blocked.contains(action)) { if(value==0) m_blocked.erase(action); value=0; }
        next.values[action]=value;
        if(value>0) { next.held.insert(action); if(!m_frame.held.contains(action)) next.pressed.insert(action); }
    }
    for(const auto& action:m_frame.held) if(!next.held.contains(action)) next.released.insert(action);
    m_sampled=true; m_wasFocused=input.focused; m_wasConnected=input.gamepadConnected; m_frame=next; return next;
}
}
