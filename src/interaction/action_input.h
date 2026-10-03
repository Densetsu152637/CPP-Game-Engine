#pragma once
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace interaction {
// Action names are nonempty opaque string identities; authored schemas own any name length limits.
using BindingMap = std::map<std::string, std::vector<std::string>>;
enum class Device { Keyboard, MouseButton, Wheel, GamepadButton, GamepadAxis };
struct BindingDescriptor { std::string token; Device device; int code; float direction = 1; };
// Codes match GLFW's public desktop constants; this module has no GLFW dependency.
const std::vector<BindingDescriptor>& bindingRegistry();
const BindingDescriptor* findBinding(std::string_view token);
struct RawInput {
    std::set<std::string> down;
    std::map<std::string, float> axes; // LeftX/LeftY/RightX/RightY [-1,1], triggers [0,1].
    float pointerX = 0, pointerY = 0, wheelX = 0, wheelY = 0;
    bool focused = true, gamepadConnected = false;
};
struct ActionFrame {
    std::set<std::string> pressed, held, released;
    std::map<std::string, float> values;
    float pointerX = 0, pointerY = 0, wheelX = 0, wheelY = 0;
    bool focused = true;
};
struct BindingConflict { std::string token; std::vector<std::string> actions; };
struct BindingValidation { std::vector<std::string> errors; std::vector<BindingConflict> conflicts; bool valid() const { return errors.empty(); } };
BindingValidation validateBindings(const BindingMap&, bool rejectConflicts = false);
class ActionMapper {
    BindingMap m_bindings;
    ActionFrame m_frame;
    std::set<std::string> m_blocked, m_blockedGamepad;
    float m_deadzone = 0.2f;
    bool m_wasFocused = true, m_wasConnected = false, m_sampled = false;
public:
    explicit ActionMapper(BindingMap bindings = {});
    BindingValidation rebind(BindingMap bindings, bool rejectConflicts = false);
    bool setDeadzone(float deadzone);
    float deadzone() const { return m_deadzone; }
    const BindingMap& bindings() const { return m_bindings; }
    ActionFrame sample(const RawInput& input);
    void clear();
};
}
