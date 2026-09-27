#include "editor.h"
#include "../project/runtime.h"

#include <exception>
#include <iostream>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <format>
#include <sstream>
#include <system_error>
#include <vector>
#endif

namespace editor
{
    Document::~Document() { stop(); }

    bool Document::replaceFromManifest(const std::filesystem::path& manifest)
    {
        auto loaded = project::loadProject(manifest);
        if (!loaded)
        {
            m_diagnostics = std::move(loaded.error());
            m_status = "Open failed; current project and unsaved edits were kept";
            return false;
        }

        try
        {
            auto nextProject = std::move(*loaded);
            auto nextSession = std::make_unique<project::SceneSession>(nextProject);
            const bool stoppedCleanly = !m_runtime || stop();
            auto stopDiagnostics = stoppedCleanly ? project::Diagnostics{} : std::move(m_diagnostics);
            m_manifest = manifest;
            m_project = std::move(nextProject);
            m_session = std::move(nextSession);
            m_savedRevision = m_session->revision();
            m_diagnostics = std::move(stopDiagnostics);
            m_status = stoppedCleanly ? "Project opened" : "Project opened; prior play shutdown reported a lifecycle error";
            return true;
        }
        catch (const std::exception& error)
        {
            m_diagnostics = {{"editor.project.open", project::Severity::Error, manifest, "", error.what()}};
            m_status = "Open failed; current project and unsaved edits were kept";
            return false;
        }
    }

    bool Document::open(const std::filesystem::path& manifest, const bool discardUnsaved)
    {
        if (isDirty() && !discardUnsaved)
        {
            m_status = "Open blocked because the current scene has unsaved edits";
            m_diagnostics = {{"editor.document.unsaved", project::Severity::Error, m_manifest, "", "Save or explicitly discard the current scene edits before opening another project"}};
            return false;
        }
        return replaceFromManifest(manifest);
    }

    bool Document::reload(const bool discardUnsaved)
    {
        if (isDirty() && !discardUnsaved)
        {
            m_status = "Reload blocked because the current scene has unsaved edits";
            m_diagnostics = {{"editor.document.unsaved", project::Severity::Error, m_manifest, "", "Save or explicitly discard the current scene edits before reloading"}};
            return false;
        }
        if (m_manifest.empty())
        {
            m_status = "No project is open";
            return false;
        }
        return replaceFromManifest(m_manifest);
    }

    bool Document::save()
    {
        if (!m_session)
        {
            m_status = "No project is open";
            return false;
        }
        auto result = m_session->save(m_session->revision());
        if (!result)
        {
            m_diagnostics = std::move(result.error());
            m_status = "Save failed";
            return false;
        }
        m_diagnostics.clear();
        m_savedRevision = m_session->revision();
        m_status = "Scene saved";
        return true;
    }

    bool Document::apply(const project::EditOperation& operation)
    {
        if (!m_session)
        {
            m_status = "No project is open";
            return false;
        }
        auto result = m_session->apply(operation, m_session->revision());
        if (!result)
        {
            m_diagnostics = std::move(result.error());
            m_status = "Edit rejected";
            return false;
        }
        m_diagnostics.clear();
        m_status = "Scene edit applied";
        return true;
    }

    bool Document::undo()
    {
        if (!m_session) return false;
        auto result = m_session->undo(m_session->revision());
        if (!result)
        {
            m_diagnostics = std::move(result.error());
            m_status = "Undo rejected";
            return false;
        }
        m_diagnostics.clear();
        m_status = "Undo applied";
        return true;
    }

    bool Document::redo()
    {
        if (!m_session) return false;
        auto result = m_session->redo(m_session->revision());
        if (!result)
        {
            m_diagnostics = std::move(result.error());
            m_status = "Redo rejected";
            return false;
        }
        m_diagnostics.clear();
        m_status = "Redo applied";
        return true;
    }

    const project::Project* Document::project() const { return m_project ? &*m_project : nullptr; }
    project::SceneSession* Document::session() const { return m_session.get(); }
    const project::Diagnostics& Document::diagnostics() const { return m_diagnostics; }
    const std::string& Document::status() const { return m_status; }
    bool Document::isPlaying() const
    {
        return m_runtime != nullptr;
    }

    bool Document::isDirty() const
    {
        return m_session && m_session->revision() != m_savedRevision;
    }

    std::optional<std::array<float, 3>> Document::playPosition(const std::string_view authoredEntityId) const
    {
        return m_runtime ? m_runtime->position(authoredEntityId) : std::nullopt;
    }

    bool Document::play()
    {
        if (!m_session) { m_status = "No project is open"; return false; }
        if (m_runtime) { m_status = "Play session is already running"; return false; }
        try
        {
            auto playProject = *m_project;
            playProject.scene = m_session->snapshot();
            auto runtime = std::make_unique<project::Runtime>(std::move(playProject));
            auto started = runtime->start();
            if (!started)
            {
                m_diagnostics = std::move(started.error());
                m_status = "Could not start isolated play session";
                return false;
            }
            m_runtime = std::move(runtime);
            m_diagnostics.clear();
            m_status = "Play running from an isolated authored scene copy";
            return true;
        }
        catch (const std::exception& error)
        {
            m_diagnostics = {{"editor.play.start", project::Severity::Error, m_manifest, "", error.what()}};
            m_status = "Could not start isolated play session";
            return false;
        }
    }

    bool Document::stop()
    {
        if (!m_runtime) { m_status = "No isolated runtime is active"; return false; }
        auto stopped = m_runtime->stop();
        m_runtime.reset();
        if (!stopped)
        {
            m_diagnostics = std::move(stopped.error());
            m_status = "Play stopped with a runtime lifecycle error";
            return false;
        }
        m_status = "Play stopped; runtime changes were discarded";
        return true;
    }

    bool Document::tick()
    {
        if (!m_runtime) return false;
        auto result = m_runtime->tick();
        if (!result)
        {
            auto tickDiagnostics = std::move(result.error());
            (void)stop();
            m_diagnostics.insert(m_diagnostics.end(), tickDiagnostics.begin(), tickDiagnostics.end());
            m_status = "Play stopped after a runtime error";
            return false;
        }
        return true;
    }
}

#ifndef _WIN32
namespace editor
{
    int run(const std::filesystem::path&)
    {
        std::cerr << "editor.unsupported_platform: the native editor is currently supported on Windows only\n";
        return 1;
    }

    bool runNativeControlSmokeTest(const std::filesystem::path&, const std::filesystem::path&)
    {
        return false;
    }
}
#else
namespace editor
{
    namespace
    {
        enum ControlId : int
        {
            ManifestEdit = 100,
            OpenButton,
            SaveButton,
            ReloadButton,
            UndoButton,
            RedoButton,
            PlayButton,
            StopButton,
            EntityList,
            PositionX,
            PositionY,
            PositionZ,
            ScriptPicker,
            MeshPicker,
            TexturePicker,
            DiagnosticsBox,
            StatusText
        };

        std::wstring widen(const std::string& text)
        {
            if (text.empty()) return {};
            const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (size <= 0) return std::wstring(text.begin(), text.end());
            std::wstring value(static_cast<size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), value.data(), size);
            return value;
        }

        std::string narrow(const std::wstring& text)
        {
            if (text.empty()) return {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0) return {};
            std::string value(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), value.data(), size, nullptr, nullptr);
            return value;
        }

        std::wstring windowText(HWND control)
        {
            const int length = GetWindowTextLengthW(control);
            std::wstring text(static_cast<size_t>(std::max(length, 0)) + 1, L'\0');
            const int copied = GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
            text.resize(static_cast<size_t>(std::max(copied, 0)));
            return text;
        }

        struct EditorWindow
        {
            Document document;
            std::filesystem::path initialManifest;
            HWND window = nullptr;
            HWND manifest = nullptr;
            HWND entities = nullptr;
            HWND position[3]{};
            HWND script = nullptr;
            HWND mesh = nullptr;
            HWND texture = nullptr;
            HWND meshLabel = nullptr;
            HWND textureLabel = nullptr;
            HWND diagnostics = nullptr;
            HWND status = nullptr;
            int selectedEntity = -1;
            bool updating = false;

            explicit EditorWindow(std::filesystem::path path) : initialManifest(std::move(path)) {}

            static HWND control(HWND parent, const wchar_t* cls, const wchar_t* label, DWORD style,
                                int x, int y, int width, int height, int id)
            {
                return CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | style,
                    x, y, width, height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                    GetModuleHandleW(nullptr), nullptr);
            }

            void createControls()
            {
                manifest = control(window, L"EDIT", initialManifest.wstring().c_str(), WS_BORDER | ES_AUTOHSCROLL, 12, 12, 600, 26, ManifestEdit);
                control(window, L"BUTTON", L"Open", BS_PUSHBUTTON, 620, 12, 72, 26, OpenButton);
                control(window, L"BUTTON", L"Save", BS_PUSHBUTTON, 698, 12, 64, 26, SaveButton);
                control(window, L"BUTTON", L"Reload", BS_PUSHBUTTON, 768, 12, 72, 26, ReloadButton);
                control(window, L"BUTTON", L"Undo", BS_PUSHBUTTON, 846, 12, 62, 26, UndoButton);
                control(window, L"BUTTON", L"Redo", BS_PUSHBUTTON, 914, 12, 62, 26, RedoButton);
                control(window, L"BUTTON", L"Play", BS_PUSHBUTTON, 846, 46, 62, 26, PlayButton);
                control(window, L"BUTTON", L"Stop", BS_PUSHBUTTON, 914, 46, 62, 26, StopButton);
                control(window, L"STATIC", L"Entities", SS_LEFT, 12, 52, 200, 20, 0);
                entities = control(window, L"LISTBOX", L"", WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 12, 74, 280, 500, EntityList);
                control(window, L"STATIC", L"Inspector", SS_LEFT, 312, 52, 200, 20, 0);
                control(window, L"STATIC", L"Position (float)", SS_LEFT, 312, 82, 160, 20, 0);
                control(window, L"STATIC", L"X", SS_LEFT, 292, 109, 16, 20, 0);
                position[0] = control(window, L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, 312, 106, 170, 26, PositionX);
                control(window, L"STATIC", L"Y", SS_LEFT, 292, 143, 16, 20, 0);
                position[1] = control(window, L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, 312, 140, 170, 26, PositionY);
                control(window, L"STATIC", L"Z", SS_LEFT, 292, 177, 16, 20, 0);
                position[2] = control(window, L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, 312, 174, 170, 26, PositionZ);
                control(window, L"STATIC", L"Script asset", SS_LEFT, 312, 214, 160, 20, 0);
                script = control(window, L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 312, 238, 360, 240, ScriptPicker);
                meshLabel = control(window, L"STATIC", L"MeshRenderer mesh", SS_LEFT, 312, 270, 160, 20, 0);
                mesh = control(window, L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 312, 294, 360, 240, MeshPicker);
                textureLabel = control(window, L"STATIC", L"MeshRenderer texture", SS_LEFT, 312, 326, 180, 20, 0);
                texture = control(window, L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 312, 350, 360, 240, TexturePicker);
                control(window, L"STATIC", L"Validation and log", SS_LEFT, 312, 386, 300, 20, 0);
                diagnostics = control(window, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 312, 410, 664, 164, DiagnosticsBox);
                status = control(window, L"STATIC", L"No project open", SS_LEFT, 12, 590, 964, 32, StatusText);
            }

            std::optional<size_t> selectedIndex() const
            {
                const LRESULT selection = SendMessageW(entities, LB_GETCURSEL, 0, 0);
                if (selection == LB_ERR || selection < 0) return std::nullopt;
                return static_cast<size_t>(selection);
            }

            void fillAssets(HWND picker, const char* kind, const std::optional<std::string>& selectedId)
            {
                SendMessageW(picker, CB_RESETCONTENT, 0, 0);
                const LRESULT none = SendMessageW(picker, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(none)"));
                SendMessageW(picker, CB_SETITEMDATA, static_cast<WPARAM>(none), static_cast<LPARAM>(-1));
                if (!selectedId) SendMessageW(picker, CB_SETCURSEL, static_cast<WPARAM>(none), 0);
                const auto* opened = document.project();
                if (opened)
                {
                    size_t item = 0;
                    for (const auto& [assetId, asset] : opened->assets)
                    {
                        if (asset.kind != kind) continue;
                        const std::wstring label = widen(assetId + " | " + asset.path.generic_string());
                        const LRESULT index = SendMessageW(picker, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                        SendMessageW(picker, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(item));
                        if (selectedId && *selectedId == assetId)
                            SendMessageW(picker, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
                        ++item;
                    }
                }
            }

            std::optional<std::string> selectedAsset(HWND picker, const char* kind) const
            {
                const LRESULT choice = SendMessageW(picker, CB_GETCURSEL, 0, 0);
                if (choice == CB_ERR) return std::nullopt;
                const LRESULT item = SendMessageW(picker, CB_GETITEMDATA, static_cast<WPARAM>(choice), 0);
                if (item == CB_ERR || item < 0) return std::nullopt;
                const auto* opened = document.project();
                if (!opened) return std::nullopt;
                size_t offset = static_cast<size_t>(item);
                for (const auto& [id, asset] : opened->assets)
                {
                    if (asset.kind != kind) continue;
                    if (offset == 0) return id;
                    --offset;
                }
                return std::nullopt;
            }

            static std::wstring propertyLabel(std::string_view componentName, std::string_view propertyName)
            {
                for (const auto& component : project::componentDescriptors())
                    if (component.serializedName == componentName)
                        for (const auto& property : component.properties)
                            if (property.name == propertyName) return widen(std::string(property.name));
                return widen(std::string(propertyName));
            }

            void render()
            {
                updating = true;
                std::wstring title = L"CPP Game Engine Editor";
                if (document.project())
                {
                    title += L" - ";
                    title += widen(document.project()->name);
                }
                if (document.isDirty()) title += L" *";
                SetWindowTextW(window, title.c_str());
                SetWindowTextW(status, widen(document.status()).c_str());
                std::wstring report;
                for (const auto& diagnostic : document.diagnostics())
                {
                    report += widen(diagnostic.severity == project::Severity::Error ? "Error" : "Warning");
                    report += L" [" + widen(diagnostic.code) + L"] ";
                    if (!diagnostic.file.empty()) report += diagnostic.file.wstring() + L" ";
                    if (!diagnostic.path.empty()) report += widen(diagnostic.path) + L": ";
                    report += widen(diagnostic.message) + L"\r\n";
                }
                if (report.empty()) report = L"No diagnostics";
                SetWindowTextW(diagnostics, report.c_str());

                SendMessageW(entities, LB_RESETCONTENT, 0, 0);
                const auto* session = document.session();
                if (session)
                {
                    const auto& scene = session->snapshot();
                    for (const auto& entity : scene.entities)
                    {
                        const std::string label = entity.name.empty() ? entity.id : entity.name + "  [" + entity.id + "]";
                        SendMessageW(entities, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(label).c_str()));
                    }
                    if (selectedEntity >= 0 && static_cast<size_t>(selectedEntity) < scene.entities.size())
                    {
                        SendMessageW(entities, LB_SETCURSEL, static_cast<WPARAM>(selectedEntity), 0);
                        const auto& entity = scene.entities[static_cast<size_t>(selectedEntity)];
                        const std::array<float, 3> values = entity.transform ? entity.transform->position : std::array<float, 3>{};
                        for (size_t i = 0; i < values.size(); ++i) SetWindowTextW(position[i], std::format(L"{:.6g}", values[i]).c_str());
                        fillAssets(script, "script", entity.script ? std::optional<std::string>(entity.script->asset) : std::nullopt);
                        fillAssets(mesh, "mesh", entity.meshRenderer ? std::optional<std::string>(entity.meshRenderer->mesh) : std::nullopt);
                        fillAssets(texture, "texture", entity.meshRenderer ? entity.meshRenderer->texture : std::nullopt);
                        std::wstring meshProperty = propertyLabel("MeshRenderer", "mesh") + L" (schema ";
                        std::wstring textureProperty = propertyLabel("MeshRenderer", "texture") + L" (schema ";
                        for (const auto& descriptor : project::componentDescriptors())
                            if (descriptor.serializedName == "MeshRenderer")
                            {
                                meshProperty += std::to_wstring(descriptor.schemaVersion) + L")";
                                textureProperty += std::to_wstring(descriptor.schemaVersion) + L")";
                            }
                        SetWindowTextW(meshLabel, meshProperty.c_str());
                        SetWindowTextW(textureLabel, textureProperty.c_str());
                    }
                    else
                    {
                        selectedEntity = -1;
                        for (HWND field : position) EnableWindow(field, FALSE);
                        EnableWindow(script, FALSE);
                        fillAssets(script, "script", std::nullopt);
                        fillAssets(mesh, "mesh", std::nullopt);
                        fillAssets(texture, "texture", std::nullopt);
                        SetWindowTextW(meshLabel, L"MeshRenderer mesh");
                        SetWindowTextW(textureLabel, L"MeshRenderer texture");
                    }
                }
                else
                {
                    for (HWND field : position) EnableWindow(field, FALSE);
                    EnableWindow(script, FALSE);
                    fillAssets(script, "script", std::nullopt);
                    fillAssets(mesh, "mesh", std::nullopt);
                    fillAssets(texture, "texture", std::nullopt);
                    SetWindowTextW(meshLabel, L"MeshRenderer mesh");
                    SetWindowTextW(textureLabel, L"MeshRenderer texture");
                }
                if (session && selectedEntity >= 0)
                {
                    for (HWND field : position) EnableWindow(field, TRUE);
                    EnableWindow(script, TRUE);
                    EnableWindow(mesh, TRUE);
                    EnableWindow(texture, session->snapshot().entities[static_cast<size_t>(selectedEntity)].meshRenderer.has_value());
                }
                EnableWindow(GetDlgItem(window, SaveButton), session != nullptr);
                EnableWindow(GetDlgItem(window, ReloadButton), session != nullptr);
                EnableWindow(GetDlgItem(window, UndoButton), session != nullptr);
                EnableWindow(GetDlgItem(window, RedoButton), session != nullptr);
                EnableWindow(GetDlgItem(window, PlayButton), session != nullptr && !document.isPlaying());
                EnableWindow(GetDlgItem(window, StopButton), document.isPlaying());
                if (document.isPlaying()) SetTimer(window, 1, 16, nullptr);
                else KillTimer(window, 1);
                updating = false;
            }

            void selectEntity()
            {
                const auto index = selectedIndex();
                selectedEntity = index ? static_cast<int>(*index) : -1;
                render();
            }

            bool readPosition(std::array<float, 3>& result)
            {
                for (size_t i = 0; i < 3; ++i)
                {
                    const auto text = narrow(windowText(position[i]));
                    float value = 0;
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
                    {
                        return false;
                    }
                    result[i] = value;
                }
                return true;
            }

            void commitPosition()
            {
                const auto index = selectedIndex();
                const auto* session = document.session();
                if (!index || !session || *index >= session->snapshot().entities.size()) return;
                std::array<float, 3> value{};
                if (!readPosition(value))
                {
                    SetWindowTextW(diagnostics, L"Error [editor.position.invalid] Position values must be finite numbers.\r\n");
                    SetWindowTextW(status, L"Position edit rejected; scene was not changed");
                    return;
                }
                document.apply(project::SetPosition{session->snapshot().entities[*index].id, value});
                render();
            }

            void commitScript()
            {
                if (updating) return;
                const auto index = selectedIndex();
                const auto* session = document.session();
                if (!index || !session || *index >= session->snapshot().entities.size()) return;
                const LRESULT choice = SendMessageW(script, CB_GETCURSEL, 0, 0);
                if (choice == CB_ERR) return;
                std::optional<std::string> assetId;
                if (SendMessageW(script, CB_GETITEMDATA, static_cast<WPARAM>(choice), 0) != CB_ERR && choice != 0)
                {
                    const auto* opened = document.project();
                    if (!opened) return;
                    size_t offset = static_cast<size_t>(SendMessageW(script, CB_GETITEMDATA, static_cast<WPARAM>(choice), 0));
                    for (const auto& [id, item] : opened->assets)
                    {
                        if (item.kind != "script") continue;
                        if (offset == 0) { assetId = id; break; }
                        --offset;
                    }
                    if (!assetId) return;
                }
                document.apply(project::SetScript{session->snapshot().entities[*index].id, std::move(assetId)});
                render();
            }

            void commitMesh()
            {
                if (updating) return;
                const auto index = selectedIndex();
                const auto* session = document.session();
                if (!index || !session || *index >= session->snapshot().entities.size()) return;
                const auto& entity = session->snapshot().entities[*index];
                const auto assetId = selectedAsset(mesh, "mesh");
                std::optional<project::MeshRenderer> value;
                if (assetId)
                    value = project::MeshRenderer{*assetId, entity.meshRenderer ? entity.meshRenderer->texture : std::nullopt};
                document.apply(project::SetMeshRenderer{entity.id, std::move(value)});
                render();
            }

            void commitTexture()
            {
                if (updating) return;
                const auto index = selectedIndex();
                const auto* session = document.session();
                if (!index || !session || *index >= session->snapshot().entities.size()) return;
                const auto& entity = session->snapshot().entities[*index];
                if (!entity.meshRenderer) return;
                auto value = *entity.meshRenderer;
                value.texture = selectedAsset(texture, "texture");
                document.apply(project::SetMeshRenderer{entity.id, std::move(value)});
                render();
            }

            void openFromField()
            {
                bool discardUnsaved = false;
                if (!confirmReplace(discardUnsaved)) return;
                const auto path = std::filesystem::path(windowText(manifest));
                if (document.open(path, discardUnsaved)) selectedEntity = -1;
                render();
            }

            bool confirmReplace(bool& discardUnsaved)
            {
                discardUnsaved = false;
                if (!document.isDirty()) return true;
                const int choice = MessageBoxW(window,
                    L"The scene has unsaved edits. Save them before replacing or reloading this project?",
                    L"Unsaved scene edits", MB_YESNOCANCEL | MB_ICONWARNING | MB_TASKMODAL);
                if (choice == IDCANCEL) return false;
                if (choice == IDYES)
                {
                    const bool saved = document.save();
                    render();
                    return saved;
                }
                discardUnsaved = choice == IDNO;
                return discardUnsaved;
            }

            LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam)
            {
                switch (message)
                {
                    case WM_CREATE:
                        createControls();
                        document.open(initialManifest);
                        render();
                        return 0;
                    case WM_COMMAND:
                        if (updating) return 0;
                        switch (LOWORD(wParam))
                        {
                            case OpenButton: openFromField(); return 0;
                            case SaveButton: document.save(); render(); return 0;
                            case ReloadButton:
                            {
                                bool discardUnsaved = false;
                                if (confirmReplace(discardUnsaved) && document.reload(discardUnsaved)) selectedEntity = -1;
                                render();
                                return 0;
                            }
                            case UndoButton: document.undo(); render(); return 0;
                            case RedoButton: document.redo(); render(); return 0;
                            case PlayButton: document.play(); render(); return 0;
                            case StopButton: document.stop(); render(); return 0;
                            case EntityList: if (HIWORD(wParam) == LBN_SELCHANGE) selectEntity(); return 0;
                            case ScriptPicker: if (HIWORD(wParam) == CBN_SELCHANGE) commitScript(); return 0;
                            case MeshPicker: if (HIWORD(wParam) == CBN_SELCHANGE) commitMesh(); return 0;
                            case TexturePicker: if (HIWORD(wParam) == CBN_SELCHANGE) commitTexture(); return 0;
                            case PositionX:
                            case PositionY:
                            case PositionZ:
                                if (HIWORD(wParam) == EN_KILLFOCUS) commitPosition();
                                return 0;
                            default: break;
                        }
                        break;
                    case WM_CLOSE:
                        KillTimer(window, 1);
                        if (document.isPlaying()) document.stop();
                        DestroyWindow(window);
                        return 0;
                    case WM_TIMER:
                        if (wParam != 1) return 0;
                        if (document.tick())
                        {
                            std::wstring playStatus = L"PLAY - isolated runtime; authored scene is unchanged";
                            const auto index = selectedIndex();
                            const auto* session = document.session();
                            if (index && session && *index < session->snapshot().entities.size())
                            {
                                const auto& entity = session->snapshot().entities[*index];
                                if (const auto positionValue = document.playPosition(entity.id))
                                    playStatus = std::format(L"PLAY - {} runtime position: {:.3f}, {:.3f}, {:.3f} - authored scene is unchanged",
                                        widen(entity.name.empty() ? entity.id : entity.name),
                                        (*positionValue)[0], (*positionValue)[1], (*positionValue)[2]);
                            }
                            SetWindowTextW(status, playStatus.c_str());
                        }
                        else
                        {
                            std::wstring report;
                            for (const auto& item : document.diagnostics())
                                report += widen(item.code + ": " + item.message) + L"\r\n";
                            SetWindowTextW(diagnostics, report.empty() ? L"Runtime stopped" : report.c_str());
                            render();
                        }
                        return 0;
                    case WM_DESTROY:
                        PostQuitMessage(0);
                        return 0;
                    default: break;
                }
                return DefWindowProcW(window, message, wParam, lParam);
            }

            static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
            {
                EditorWindow* self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (message == WM_NCCREATE)
                {
                    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                    self = static_cast<EditorWindow*>(create->lpCreateParams);
                    self->window = hwnd;
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                }
                if (self) return self->onMessage(message, wParam, lParam);
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
        };
    }

    bool runNativeControlSmokeTest(const std::filesystem::path& manifest, const std::filesystem::path& screenshot)
    {
        HWND parent = CreateWindowExW(0, L"STATIC", L"Editor control smoke test", WS_POPUP,
            40, 40, 1000, 690, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!parent) return false;
        EditorWindow app(manifest);
        app.window = parent;
        app.createControls();
        bool passed = app.document.open(manifest);
        if (passed)
        {
            app.selectedEntity = 0;
            app.render();
            const HWND required[] = {app.manifest, app.entities, app.position[0], app.position[1], app.position[2],
                app.script, app.meshLabel, app.mesh, app.textureLabel, app.texture, app.diagnostics, app.status};
            for (const HWND child : required)
            {
                RECT bounds{};
                if (!child || !IsWindow(child) || !GetWindowRect(child, &bounds) || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
                    passed = false;
            }
            if (SendMessageW(app.script, CB_GETCOUNT, 0, 0) != 2 || SendMessageW(app.mesh, CB_GETCOUNT, 0, 0) != 2 ||
                SendMessageW(app.texture, CB_GETCOUNT, 0, 0) != 2)
                passed = false;

            // Exercise the same native WM_COMMAND path as the user selecting an asset.
            SendMessageW(app.mesh, CB_SETCURSEL, 1, 0);
            app.onMessage(WM_COMMAND, MAKEWPARAM(MeshPicker, CBN_SELCHANGE), reinterpret_cast<LPARAM>(app.mesh));
            SendMessageW(app.texture, CB_SETCURSEL, 1, 0);
            app.onMessage(WM_COMMAND, MAKEWPARAM(TexturePicker, CBN_SELCHANGE), reinterpret_cast<LPARAM>(app.texture));
            const auto& entities = app.document.session()->snapshot().entities;
            if (entities.empty() || !entities.front().meshRenderer || entities.front().meshRenderer->mesh != "asset:mesh" ||
                entities.front().meshRenderer->texture != std::optional<std::string>("asset:texture"))
                passed = false;
        }

        if (passed && !screenshot.empty())
        {
            std::error_code directoryError;
            if (!screenshot.parent_path().empty()) std::filesystem::create_directories(screenshot.parent_path(), directoryError);
            if (directoryError) passed = false;
            ShowWindow(parent, SW_SHOWNOACTIVATE);
            UpdateWindow(parent);
            HDC windowDc = GetWindowDC(parent);
            HDC memoryDc = windowDc ? CreateCompatibleDC(windowDc) : nullptr;
            RECT bounds{};
            GetWindowRect(parent, &bounds);
            const int width = bounds.right - bounds.left;
            const int height = bounds.bottom - bounds.top;
            HBITMAP bitmap = memoryDc && windowDc ? CreateCompatibleBitmap(windowDc, width, height) : nullptr;
            HGDIOBJ old = bitmap ? SelectObject(memoryDc, bitmap) : nullptr;
            if (!bitmap || !PrintWindow(parent, memoryDc, PW_RENDERFULLCONTENT)) passed = false;
            if (passed)
            {
                BITMAP image{};
                GetObjectW(bitmap, sizeof(image), &image);
                BITMAPINFOHEADER info{};
                info.biSize = sizeof(info);
                info.biWidth = image.bmWidth;
                info.biHeight = image.bmHeight;
                info.biPlanes = 1;
                info.biBitCount = 24;
                info.biCompression = BI_RGB;
                const DWORD stride = (static_cast<DWORD>(width) * 3 + 3) & ~3u;
                std::vector<BYTE> pixels(static_cast<size_t>(stride) * static_cast<size_t>(height));
                if (old) { SelectObject(memoryDc, old); old = nullptr; }
                if (!GetDIBits(memoryDc, bitmap, 0, static_cast<UINT>(height), pixels.data(),
                    reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS)) passed = false;
                if (passed)
                {
                    BITMAPFILEHEADER fileHeader{};
                    fileHeader.bfType = 0x4D42;
                    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(info);
                    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(pixels.size());
                    std::ofstream output(screenshot, std::ios::binary | std::ios::trunc);
                    output.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
                    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
                    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
                    passed = output.good();
                }
            }
            if (old) SelectObject(memoryDc, old);
            if (bitmap) DeleteObject(bitmap);
            if (memoryDc) DeleteDC(memoryDc);
            if (windowDc) ReleaseDC(parent, windowDc);
        }
        if (app.document.isPlaying()) app.document.stop();
        DestroyWindow(parent);
        return passed;
    }

    int run(const std::filesystem::path& manifest)
    {
        const wchar_t* className = L"CPPGameEngineEditorWindow";
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = EditorWindow::procedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = className;
        if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            std::fprintf(stderr, "editor.window.register: could not register the Windows editor window\n");
            return 1;
        }

        EditorWindow app(manifest);
        HWND window = CreateWindowExW(0, className, L"CPP Game Engine Editor", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1000, 690, nullptr, nullptr, windowClass.hInstance, &app);
        if (!window)
        {
            std::fprintf(stderr, "editor.window.create: could not create the Windows editor window\n");
            return 1;
        }
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }
}
#endif
