#pragma once
// Host side of the mapping layer (okayui/include/okay/UI/OkayUIMap.hpp): the Dear ImGui flag and colour names a ported panel uses,
// paired with OkayUI's. Written from the meaning of the names only. Needs imgui.h before it, so only editor / launcher code that
// already has ImGui includes this. Not built by the headless OkayUI tests (they do not have ImGui): checked by the compiler against
// the project's ImGui and exercised by a one-off program, see docs/ui/own_ui_toolkit.md section 6.7.
#include "okay/UI/OkayUIMap.hpp"

namespace okayui_map {

/// ImGuiWindowFlags_* to OkayUI window flags. Flags OkayUI has no equivalent for (NoScrollWithMouse, MenuBar, HorizontalScrollbar,
/// NoSavedSettings, ...) are left out and show up in *unmapped.
inline int WindowFlags(int imguiFlags, int* unmapped = nullptr) {
    static const OkayUI::FlagMap t[] = {
        { ImGuiWindowFlags_NoResize,            OkayUI::Window_NoResize },
        { ImGuiWindowFlags_NoMove,              OkayUI::Window_NoMove },
        { ImGuiWindowFlags_NoTitleBar,          OkayUI::Window_NoTitleBar },
        { ImGuiWindowFlags_NoScrollbar,         OkayUI::Window_NoScrollbar },
        { ImGuiWindowFlags_NoBackground,        OkayUI::Window_NoBackground },
        { ImGuiWindowFlags_NoCollapse,          OkayUI::Window_NoCollapse },
        { ImGuiWindowFlags_AlwaysAutoResize,    OkayUI::Window_AlwaysAutoResize },
        { ImGuiWindowFlags_NoFocusOnAppearing,  OkayUI::Window_NoFocusOnAppearing },
        { ImGuiWindowFlags_NoBringToFrontOnFocus, OkayUI::Window_NoBringToFront },
    };
    return OkayUI::MapFlags(imguiFlags, t, (int)(sizeof t / sizeof t[0]), unmapped);
}

/// ImGuiCol_* to OkayUI::Col; -1 for a colour slot OkayUI does not have.
inline int Col(int c) {
    switch (c) {
        case ImGuiCol_Text: return OkayUI::Col_Text;
        case ImGuiCol_TextDisabled: return OkayUI::Col_TextDisabled;
        case ImGuiCol_WindowBg: return OkayUI::Col_WindowBg;
        case ImGuiCol_ChildBg: return OkayUI::Col_ChildBg;
        case ImGuiCol_PopupBg: return OkayUI::Col_PopupBg;
        case ImGuiCol_Border: return OkayUI::Col_Border;
        case ImGuiCol_BorderShadow: return OkayUI::Col_BorderShadow;
        case ImGuiCol_FrameBg: return OkayUI::Col_FrameBg;
        case ImGuiCol_FrameBgHovered: return OkayUI::Col_FrameBgHovered;
        case ImGuiCol_FrameBgActive: return OkayUI::Col_FrameBgActive;
        case ImGuiCol_TitleBg: return OkayUI::Col_TitleBg;
        case ImGuiCol_TitleBgActive: return OkayUI::Col_TitleBgActive;
        case ImGuiCol_TitleBgCollapsed: return OkayUI::Col_TitleBgCollapsed;
        case ImGuiCol_MenuBarBg: return OkayUI::Col_MenuBarBg;
        case ImGuiCol_ScrollbarBg: return OkayUI::Col_ScrollbarBg;
        case ImGuiCol_ScrollbarGrab: return OkayUI::Col_ScrollbarGrab;
        case ImGuiCol_ScrollbarGrabHovered: return OkayUI::Col_ScrollbarGrabHovered;
        case ImGuiCol_ScrollbarGrabActive: return OkayUI::Col_ScrollbarGrabActive;
        case ImGuiCol_CheckMark: return OkayUI::Col_CheckMark;
        case ImGuiCol_SliderGrab: return OkayUI::Col_SliderGrab;
        case ImGuiCol_SliderGrabActive: return OkayUI::Col_SliderGrabActive;
        case ImGuiCol_Button: return OkayUI::Col_Button;
        case ImGuiCol_ButtonHovered: return OkayUI::Col_ButtonHovered;
        case ImGuiCol_ButtonActive: return OkayUI::Col_ButtonActive;
        case ImGuiCol_Header: return OkayUI::Col_Header;
        case ImGuiCol_HeaderHovered: return OkayUI::Col_HeaderHovered;
        case ImGuiCol_HeaderActive: return OkayUI::Col_HeaderActive;
        case ImGuiCol_Separator: return OkayUI::Col_Separator;
        case ImGuiCol_SeparatorHovered: return OkayUI::Col_SeparatorHovered;
        case ImGuiCol_SeparatorActive: return OkayUI::Col_SeparatorActive;
        case ImGuiCol_ResizeGrip: return OkayUI::Col_ResizeGrip;
        case ImGuiCol_ResizeGripHovered: return OkayUI::Col_ResizeGripHovered;
        case ImGuiCol_ResizeGripActive: return OkayUI::Col_ResizeGripActive;
        case ImGuiCol_TabHovered: return OkayUI::Col_TabHovered;
        case ImGuiCol_PlotLines: return OkayUI::Col_PlotLines;
        case ImGuiCol_PlotLinesHovered: return OkayUI::Col_PlotLinesHovered;
        case ImGuiCol_PlotHistogram: return OkayUI::Col_PlotHistogram;
        case ImGuiCol_PlotHistogramHovered: return OkayUI::Col_PlotHistogramHovered;
        case ImGuiCol_TableHeaderBg: return OkayUI::Col_TableHeaderBg;
        case ImGuiCol_TableBorderStrong: return OkayUI::Col_TableBorderStrong;
        case ImGuiCol_TableBorderLight: return OkayUI::Col_TableBorderLight;
        case ImGuiCol_TableRowBg: return OkayUI::Col_TableRowBg;
        case ImGuiCol_TableRowBgAlt: return OkayUI::Col_TableRowBgAlt;
        case ImGuiCol_TextSelectedBg: return OkayUI::Col_TextSelectedBg;
        case ImGuiCol_DragDropTarget: return OkayUI::Col_DragDropTarget;
        case ImGuiCol_NavWindowingHighlight: return OkayUI::Col_NavWindowingHighlight;
        case ImGuiCol_NavWindowingDimBg: return OkayUI::Col_NavWindowingDimBg;
        case ImGuiCol_ModalWindowDimBg: return OkayUI::Col_ModalWindowDimBg;
        case ImGuiCol_DockingPreview: return OkayUI::Col_DockingPreview;
        case ImGuiCol_DockingEmptyBg: return OkayUI::Col_DockingEmptyBg;
        default: return -1;
    }
}

/// ImGuiStyleVar_* to OkayUI::StyleVar; -1 for one OkayUI has no slot for.
inline int StyleVar(int v) {
    switch (v) {
        case ImGuiStyleVar_Alpha: return OkayUI::StyleVar_Alpha;
        case ImGuiStyleVar_WindowPadding: return OkayUI::StyleVar_WindowPadding;
        case ImGuiStyleVar_WindowRounding: return OkayUI::StyleVar_WindowRounding;
        case ImGuiStyleVar_FramePadding: return OkayUI::StyleVar_FramePadding;
        case ImGuiStyleVar_FrameRounding: return OkayUI::StyleVar_FrameRounding;
        case ImGuiStyleVar_ItemSpacing: return OkayUI::StyleVar_ItemSpacing;
        case ImGuiStyleVar_ItemInnerSpacing: return OkayUI::StyleVar_ItemInnerSpacing;
        case ImGuiStyleVar_ChildRounding: return OkayUI::StyleVar_ChildRounding;
        case ImGuiStyleVar_ChildBorderSize: return OkayUI::StyleVar_BorderSize;
        case ImGuiStyleVar_FrameBorderSize: return OkayUI::StyleVar_BorderSize;
        case ImGuiStyleVar_WindowBorderSize: return OkayUI::StyleVar_BorderSize;
        case ImGuiStyleVar_ButtonTextAlign: return OkayUI::StyleVar_ButtonTextAlign;
        case ImGuiStyleVar_SelectableTextAlign: return OkayUI::StyleVar_SelectableTextAlign;
        case ImGuiStyleVar_ScrollbarSize: return OkayUI::StyleVar_ScrollbarSize;
        default: return -1;
    }
}

} // namespace okayui_map
