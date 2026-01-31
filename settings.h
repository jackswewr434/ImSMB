#pragma once

#include "imgui/imgui.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
extern char server_buf[64];
extern char share_buf[64];
extern char username_buf[64];
extern char password_buf[64];

std::string downloadPath = "";
static char downloadPathBuffer[256] = "";
// Config file handlers (for non-theme settings like download path)
void SaveConfig(const char* path) {
    std::ofstream f(path);
    if (!f) return;

    // sync string from buffer
    downloadPath = downloadPathBuffer;

    // one value per line; avoid spaces/newlines in values if possible
    f << downloadPath   << '\n';
    f << server_buf     << '\n';
    f << share_buf      << '\n';
    f << username_buf   << '\n';
    f << password_buf   << '\n';
    std::cout << server_buf << " " << share_buf << " " << username_buf << " " << password_buf << " " << std::endl;
}

void LoadConfig(const char* path) {
    std::ifstream f(path);
    if (!f) return;

    std::string line;

    // downloadPath
    if (std::getline(f, downloadPath)) {
        std::strncpy(downloadPathBuffer, downloadPath.c_str(),
                     sizeof(downloadPathBuffer) - 1);
        downloadPathBuffer[sizeof(downloadPathBuffer) - 1] = '\0';
    }

    // helper lambda to read a line into a char buffer
    auto read_into_buf = [&](char* buf, size_t buf_size) {
        if (std::getline(f, line)) {
            std::strncpy(buf, line.c_str(), buf_size - 1);
            buf[buf_size - 1] = '\0';
        }
    };

    read_into_buf(server_buf,   sizeof(server_buf));
    read_into_buf(share_buf,    sizeof(share_buf));
    read_into_buf(username_buf, sizeof(username_buf));
    read_into_buf(password_buf, sizeof(password_buf));
}

void SaveStyle(const char* path){
    ImGuiStyle& style = ImGui::GetStyle();
    std::ofstream f(path);
    if(!f) return;
    f << style.FrameRounding << ' ' << style.WindowRounding << ' ' << style.ScrollbarRounding << '\n';
    for (int i = 0; i < ImGuiCol_COUNT; i++){
        ImVec4& col = style.Colors[i];
        f << col.x << ' ' << col.y << ' ' << col.z << ' ' << col.w << '\n';
    }
    f.close();
}

void LoadStyle(const char* path){
std::ifstream f(path);
    if (!f) return;
    ImGuiStyle& style = ImGui::GetStyle();

    // fallback
    ImGui::StyleColorsDark();

    // read rounding header (first line: frame window scrollbar)
    float fr=style.FrameRounding, wr=style.WindowRounding, sr=style.ScrollbarRounding;
    if (!(f >> fr >> wr >> sr)) return;
    style.FrameRounding = fr; style.WindowRounding = wr; style.ScrollbarRounding = sr;

    // read remaining lines into memory
    std::vector<std::string> lines;
    std::string line;
    std::getline(f, line); // consume end-of-line after header
    while (std::getline(f, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        lines.push_back(line);
    }

    // detect whether lines start with an index (e.g. "12 r g b a") or are plain "r g b a"
    bool has_index = false;
    if (!lines.empty()) {
        std::istringstream iss(lines[0]);
        int maybe_idx; float a,b,c,d;
        if ( (iss >> maybe_idx) && (iss >> a >> b >> c >> d) ) has_index = true;
    }

    if (has_index) {
        for (auto &ln : lines) {
            std::istringstream iss(ln);
            int idx; float r,g,b,a;
            if (!(iss >> idx >> r >> g >> b >> a)) continue;
            if (idx >= 0 && idx < ImGuiCol_COUNT) style.Colors[idx] = ImVec4(r,g,b,a);
        }
    } else {
        // sequential mapping: first color line -> ImGuiCol_0, etc.
        for (size_t i = 0; i < lines.size() && i < (size_t)ImGuiCol_COUNT; ++i) {
            std::istringstream iss(lines[i]);
            float r,g,b,a;
            if (!(iss >> r >> g >> b >> a)) continue;
            style.Colors[(int)i] = ImVec4(r,g,b,a);
        }
    }
}

void showSettingsTab(){
    if(ImGui::CollapsingHeader("Theme")){
            ImGuiStyle& style = ImGui::GetStyle();
            ImVec4& buttonCol = style.Colors[ImGuiCol_Button];
            ImVec4& windowBgCol = style.Colors[ImGuiCol_WindowBg];
            ImVec4& frameBgCol = style.Colors[ImGuiCol_FrameBg];
            ImVec4& scrollBgCol = style.Colors[ImGuiCol_ScrollbarBg];
            ImVec4& scrollGrabCol = style.Colors[ImGuiCol_ScrollbarGrab];
            ImVec4& scrollGrabHovCol = style.Colors[ImGuiCol_ScrollbarGrabHovered];
            ImVec4& scrollGrabActCol = style.Colors[ImGuiCol_ScrollbarGrabActive];
            ImVec4& buttonHov = style.Colors[ImGuiCol_ButtonHovered];
            ImVec4& buttonAct = style.Colors[ImGuiCol_ButtonActive];
            ImVec4& collapseHead = style.Colors[ImGuiCol_Header];
            ImVec4& collapseHeadActive = style.Colors[ImGuiCol_HeaderActive];
            ImVec4& collapseHeadHover = style.Colors[ImGuiCol_HeaderHovered];
            ImVec4& ProgressBar = style.Colors[ImGuiCol_PlotHistogram];
            ImVec4& titlebgActive = style.Colors[ImGuiCol_TitleBgActive];
            ImVec4& titlebg = style.Colors[ImGuiCol_TitleBg];
            if(ImGui::CollapsingHeader("Rounding")){
                ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 10.0f);
                ImGui::SliderFloat("Scrollbar Rounding", &style.ScrollbarRounding, 0.0f, 10.0f);
            }
            if(ImGui::CollapsingHeader("Colors")){
                ImGui::Text("Modify Colors:");
                auto show_picker = [](const char* label, ImVec4& col){
                    char btnid[128]; char popid[128];
                    snprintf(btnid, sizeof(btnid), "btn_%s", label);
                    snprintf(popid, sizeof(popid), "pop_%s", label);
                    ImGui::TextUnformatted(label);
                    ImGui::SameLine(260);
                    if (ImGui::ColorButton(btnid, col, ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(48,18))) ImGui::OpenPopup(popid);
                    if (ImGui::BeginPopup(popid)){
                        ImGui::ColorPicker4("##picker", (float*)&col.x, ImGuiColorEditFlags_AlphaBar);
                        ImGui::EndPopup();
                    }
                };

                show_picker("Title Background Color", titlebg);
                show_picker("Title Background Active Color", titlebgActive);
                show_picker("Button Color", buttonCol);
                show_picker("Button Hovered Color", buttonHov);
                show_picker("Button Active Color", buttonAct);
                show_picker("Window Background Color", windowBgCol);
                show_picker("Frame Background Color", frameBgCol);
                show_picker("Scrollbar Background Color", scrollBgCol);
                show_picker("Scrollbar Grab Color", scrollGrabCol);
                show_picker("Scrollbar Grab Hovered Color", scrollGrabHovCol);
                show_picker("Scrollbar Grab Active Color", scrollGrabActCol);
                show_picker("Collapsing Header Color", collapseHead);
                show_picker("Collapsing Header Active Color", collapseHeadActive);
                show_picker("Collapsing Header Hovered Color", collapseHeadHover);
                show_picker("Progress Bar Color", ProgressBar);
                show_picker("Progress Bar Hovered Color", style.Colors[ImGuiCol_PlotHistogramHovered]);
                show_picker("Text Color", style.Colors[ImGuiCol_Text]);
                show_picker("Text Disabled Color", style.Colors[ImGuiCol_TextDisabled]);
                show_picker("Border Color", style.Colors[ImGuiCol_Border]);
            }
            if(ImGui::Button("Save Theme")){
                SaveStyle("theme.cfg");
            }
            
            if(ImGui::Button("Load Theme")){
                LoadStyle("theme.cfg");
            }
    } 
    if(ImGui::CollapsingHeader("File Locations")){
        if(ImGui::InputText("File Path", downloadPathBuffer, sizeof(downloadPathBuffer))){
            SaveConfig("config.cfg");
        }
        ImGui::Text("(saves automatically), if blank goes to program dir");
    }

    ImGui::SetWindowFontScale(.75f);
    ImGui::TextDisabled("SMB Viewer v1.1.1 - by Jackson Andrawis");
    ImGui::SetWindowFontScale(1.0f);

}