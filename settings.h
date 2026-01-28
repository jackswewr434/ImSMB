#include "imgui/imgui.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

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
            ImVec4& collapseHead = style.Colors[ImGuiCol_TitleBg];
            ImVec4& collapseHeadActive = style.Colors[ImGuiCol_TitleBgActive];
            ImVec4& ProgressBar = style.Colors[ImGuiCol_PlotHistogram];
            if(ImGui::CollapsingHeader("Rounding")){
                ImGui::SliderFloat("Frame Rounding", &style.FrameRounding, 0.0f, 10.0f);
                ImGui::SliderFloat("Scrollbar Rounding", &style.ScrollbarRounding, 0.0f, 10.0f);
            }
            if(ImGui::CollapsingHeader("Colors")){
                ImGui::Text("Modify Colors:");
                if (ImGui::ColorEdit4("Button Color", (float*)&buttonCol.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if(ImGui::ColorEdit4("Button Hovered Color", (float*)&buttonHov.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if(ImGui::ColorEdit4("Button Active Color", (float*)&buttonAct.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if (ImGui::ColorEdit4("Window Background Color", (float*)&windowBgCol.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if (ImGui::ColorEdit4("Frame Background Color", (float*)&frameBgCol.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if (ImGui::ColorEdit4("Scrollbar Background Color", (float*)&scrollBgCol.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if (ImGui::ColorEdit4("Scrollbar Grab Color", (float*)&scrollGrabCol.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if(ImGui::ColorEdit4("Scrollbar Grab Hovered Color", (float*)&scrollGrabHovCol.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if(ImGui::ColorEdit4("Scrollbar Grab Active Color", (float*)&scrollGrabActCol.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if(ImGui::ColorEdit4("Collapsing Header Color", (float*)&collapseHead.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if(ImGui::ColorEdit4("Collapsing Header Active Color", (float*)&collapseHeadActive.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                if(ImGui::ColorEdit4("Progress Bar Color", (float*)&ProgressBar.x, ImGuiColorEditFlags_AlphaBar)) {
                }
                
            }
            if(ImGui::Button("Save Theme")){
                SaveStyle("theme.cfg");
            }
    } 
}