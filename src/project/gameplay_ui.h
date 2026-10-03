#pragma once
#include "../interaction/action_input.h"
#include <array>
#include <deque>
#include <functional>
#include <optional>

namespace project::ui {
struct Rect { float x = 0, y = 0, width = 640, height = 360; };
using Color = std::array<float, 4>;
struct PanelOptions {
    std::string id, fontAsset, text;
    Rect rect;
    Color color{1, 1, 1, 1};
    float scale = 1, lineHeight = 20, glyphAdvance = 10;
    int layer = 0;
    bool modal = true, dismissible = true;
    std::vector<std::string> choices; // Single-row UTF-8 labels; CR and LF are rejected.
    std::string confirmAction = "ui_confirm", cancelAction = "ui_back", upAction = "ui_up", downAction = "ui_down", pointerAction = "ui_click";
};
struct PanelSnapshot : PanelOptions {
    float scrollOffset = 0, contentHeight = 0;
    bool focused = false;
    std::size_t selected = 0;
    Rect clip;
    std::vector<std::string> lines;
};
struct UiEvent { std::string panel, type; std::size_t selection = 0; };
// Advance callback is in logical pixels at scale 1; default is monospaced.
using GlyphAdvance = std::function<float(std::string_view, char32_t)>;
class UiModel {
    std::vector<PanelSnapshot> m_panels;
    std::deque<UiEvent> m_events;
    std::set<std::string> m_suppressed;
    std::set<std::string> m_releaseSuppressed; // Consume explicit release for this frame only.
    interaction::ActionFrame m_raw;
    bool m_transition = false;
    GlyphAdvance m_advance;
    bool layout(PanelSnapshot& panel);
    void capture();
public:
    explicit UiModel(GlyphAdvance advance = {});
    // Limits: 64 panels, 64 choices, 64 KiB text, 4096 laid-out lines. Invalid UTF-8 is rejected.
    bool open(PanelOptions panel);
    bool close(std::string_view id);
    bool setText(std::string_view id, std::string text);
    bool resize(std::string_view id, Rect rect);
    bool scroll(std::string_view id, float delta);
    void tick(const interaction::ActionFrame& input);
    interaction::ActionFrame gameplayFrame() const;
    std::optional<UiEvent> pollEvent();
    const std::vector<PanelSnapshot>& panels() const { return m_panels; }
    bool modalActive() const;
    void clear(); // Scene/fault teardown: capture held controls before releasing focus.
};
bool validUtf8(std::string_view text);
}
