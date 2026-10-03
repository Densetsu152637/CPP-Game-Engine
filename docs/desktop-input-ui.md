# Desktop action input and gameplay UI

`interaction::ActionMapper` is a CPU-only desktop input mapper. The host samples
physical controls once per simulation tick into `RawInput`. `down` contains exact
registry tokens, not OS repeat events. `bindingRegistry()` supplies the complete
supported tokens and GLFW-compatible key, mouse, gamepad button and axis indices.
Keyboard names include letters, digits, arrows, modifiers, punctuation, F1–F25,
keypad and navigation keys. Mouse buttons are Left, Right, Middle and 4–8; wheel
tokens are `Mouse:WheelUp/Down/Left/Right`. Gamepad buttons use GLFW standard
mapping names (A/B/X/Y, bumpers, Back/Start/Guide, thumbs and Dpad directions).
Stick bindings use `Gamepad:LeftXPositive/Negative`, LeftY, RightX and RightY;
triggers use `Gamepad:LeftTrigger` and `Gamepad:RightTrigger`.

`RawInput::axes` keys are LeftX/LeftY/RightX/RightY in [-1,1] and LeftTrigger /
RightTrigger in [0,1]. The frontend must convert GLFW triggers from [-1,1].
Axis values above the configurable deadzone are normalized to [0,1]. An action
combines bindings by maximum value. Press occurs on zero-to-positive, release
on positive-to-zero, and held is present on the initial press too. Wheel input
is a one-tick pulse; sustained sampled wheel input remains held. NaN/Infinity
input is discarded. Focus loss emits a single release and quarantines held
actions until a neutral sample after focus returns. Reconnected gamepad controls
must likewise return to neutral, without suppressing a keyboard binding of the
same action. Neither OS repeat nor a device reconnect synthesizes a press.

`BindingMap` is a value type suitable for host JSON settings serialization.
Action names are nonempty opaque string identities: spaces, Unicode bytes and legacy names longer than 64 bytes are preserved exactly. Authored schemas enforce their own name length constraints. Physical binding token names remain the exact registry spellings.
Validation accepts up to 256 actions, 16 bindings each, and reports invalid or
duplicate tokens. Sharing a control between UI and gameplay produces advisory
`BindingConflict` records; callers can request strict rejection. `rebind` is
atomic on validation failure and quarantines previously held actions until
neutral. Host persistence and authored legacy bindings are integration concerns.

`project::ui::UiModel` owns an ordered stack of panels and emits `confirm`,
`cancel`, and `selection` events. Action names, font asset, color, logical pixel
rectangle, scale, metrics and choices are configurable. Default navigation uses
ui_confirm / ui_back / ui_up / ui_down / ui_click. Controller support follows
from mapping these actions; pointer coordinates must be in the same logical
coordinate space as panel rectangles. Pointer wheel scrolls only inside the
focused panel. Choice rows follow the text lines. Each choice is a single row; CR/LF characters are rejected. Up/down navigation scrolls the selected row into view. Rows taller than the viewport align at its top. Opening a panel and manual text scrolling retain the reading position until a choice navigation action. Snapshot order is stack order;
only the top panel receives navigation, and closing it restores parent focus.

Feed the original action frame into `tick`, then query `gameplayFrame()` for
world input. While any modal is open it consumes all gameplay controls. Opening,
closing or clearing a panel also masks the current frame immediately, including
opens during a script callback. Controls held across transitions stay suppressed
through their release frame. An explicit release rearms the control for the next frame; a fully neutral frame also rearms it. This
prevents dismissal from interacting with the object beneath or reopening the
same overlay. `clear()` removes panels and queued events for scene changes or
fault teardown while retaining the transition quarantine.

Text must be valid Unicode scalar UTF-8. Layout wraps at scalar boundaries,
including long unbroken strings, and honors newlines. `GlyphAdvance` can supply
font atlas metrics by font asset and scalar; otherwise monospaced configurable
advances are used. `lines`, `contentHeight`, `clip`, and `scrollOffset` expose
renderer geometry; renderer clips drawing to `clip`. Scroll is clamped after
text replacement or resizing. Invalid text and geometry are rejected atomically. Effective font advances and row heights must remain finite and positive after scaling; scalar glyph callbacks may return zero for combining marks. Pointer choice indices are bounded before integer conversion, including for very small valid row heights.
Bounds are 64 panels, 64 choices per panel, 64 KiB text, fewer than 4096 laid-out
lines, 256 queued events, and 16384 logical pixels per rectangle dimension.
Text exceeding the layout bound is rejected rather than silently truncated.
Choice strings are limited to 4096 bytes. The model stores only supplied visible
text; locked narrative content must remain hidden by the authoring layer.

The renderer must draw text and choice rows from panel snapshots, identify
`focused` and `selected` visibly, and use matching glyph advances/line height.
The model does not select a game-specific visual design, font, language or
resolution, and supplies no phone input or native authoring editor UI.

Focused checks are `runInputUiTests()` in
`src/test/project/input_ui_tests.cpp`; the project test runner calls this during
integration. They cover multi-binding edges, repeat, wheel, deadzone, focus,
disconnect, reconnect, remap validation, conflicts, UTF-8, wrapping and scrolling,
keyboard/controller actions, pointer hit testing, nested modal focus, dismissal
quarantine and scene/fault cleanup.
