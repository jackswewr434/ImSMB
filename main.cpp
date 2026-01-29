/*
    IMPORTANT!!! TODO:
        Fix upload. idk wtf went wrong [ ]
        DONT DELETE THIS DIRECTORY [ ]

        Made by Jackson Andrawis (jacksonandrawis@gmail.com) 2026
*/
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "backend.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <map>
#include <chrono>
#include <filesystem>
#include "styles.h"
#include "settings.h"
#include "images.h"
#include "image_utils.h"
char server_buf[64] = "192.168.8.93";
char share_buf[64] = "tmp";
char username_buf[64] = "jandrawis";
char password_buf[64] = "1997";
static std::vector<SMBFileInfo> file_list;
static char current_path[512] = "";
// bool tabs
bool smbBrowsing = true;
bool settingsTab = false;
static std::vector<std::string> upload_queue;
static char local_file_buf[512] = "";
static bool upload_ready = false;
// Upload worker structures
struct UploadTask
{
    int id;
    std::string local;
    std::string remote;
    size_t total_bytes;
};
struct UploadStatus
{
    int id;
    std::string local;
    std::string remote;
    size_t transferred = 0;
    size_t total;
    bool done;
    bool success;
    std::chrono::steady_clock::time_point finished_time;
};
static std::deque<UploadTask> upload_tasks;
static std::map<int, UploadStatus> upload_status_map;
static std::mutex upload_mutex;
static std::condition_variable upload_cv;
static std::atomic<int> next_upload_id{1};
static std::thread upload_worker_thread;
static std::atomic<bool> workers_stop{false};
static bool upload_show_active = false; // keep upload UI visible until next upload attempt
static std::atomic<int> uploads_in_progress{0};
static std::atomic<bool> refresh_listing_after_upload{false};

// Download worker structures
struct DownloadTask
{
    int id;
    std::string remote;
    std::string local;
    size_t total_bytes;
};
struct DownloadStatus
{
    int id;
    std::string remote;
    std::string local;
    size_t transferred = 0;
    size_t total;
    bool done;
    bool success;
};
static std::deque<DownloadTask> download_tasks;
static std::map<int, DownloadStatus> download_status_map;
static std::mutex download_mutex;
static std::condition_variable download_cv;
static std::atomic<int> next_download_id{1};
static std::thread download_worker_thread;

// Delete worker structures
struct DeleteTask
{
    std::string remote;
    bool is_dir;
};
static std::deque<DeleteTask> delete_tasks;
static std::mutex delete_mutex;
static std::condition_variable delete_cv;
static std::thread delete_worker_thread;

struct StagedDelete
{
    std::string original;
    std::string temp;
    bool is_dir;
};
static std::vector<StagedDelete> staged_deletes;
static std::string pending_delete_remote;
static bool pending_delete_is_dir = false;
static bool pending_delete_open_popup = false;
// Rename state
static std::string rename_target_remote;
static char rename_buf[512] = {0};
static bool rename_open = false;

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(1280, 800, "SMB Browser by Jackson Andrawis", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glewInit();

    printf("All inits done\n");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    if (file_exists_fopen("theme.cfg"))
    {
        LoadStyle("theme.cfg");
    }
    else
    {
        ImGui::StyleColorsDark();
    }
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    // initialize small icons (requires GL context)
    InitImageTextures();
    // Receive OS file drops (first path) and mark upload ready
    glfwSetDropCallback(window, [](GLFWwindow * /*w*/, int count, const char **paths)
                        {
            if (count > 0 && paths) {
                for (int i = 0; i < count; ++i) {
                    if (!paths[i]) continue;
                    upload_queue.push_back(paths[i]);
                    printf("[DROP DEBUG] GLFW drop queued: %s\n", paths[i]);
                }
                upload_ready = !upload_queue.empty();
            } });

    // Start upload worker
    upload_worker_thread = std::thread([]()
                                       {
                while (!workers_stop) {
                    UploadTask task;
                    {
                        std::unique_lock<std::mutex> lk(upload_mutex);
                        upload_cv.wait(lk, [] {return workers_stop || !upload_tasks.empty(); });
                        if (workers_stop) break;
                        task = upload_tasks.front();
                        upload_tasks.pop_front();
                    }

                    // mark status (guarded)
                    {
                        std::lock_guard<std::mutex> lk(upload_mutex);
                        auto& st = upload_status_map[task.id];
                        st.id = task.id; st.local = task.local; st.remote = task.remote; st.transferred = 0; st.total = task.total_bytes; st.done = false; st.success = false;
                    }

                    // perform upload with progress callback
                    bool ok = UploadFileWithProgress(server_buf, share_buf, task.remote, task.local, username_buf, password_buf,
                        [&](ssize_t written) {
                            std::lock_guard<std::mutex> lk(upload_mutex);
                            auto it = upload_status_map.find(task.id);
                            if (it != upload_status_map.end()) {
                                // if total unknown, try to determine it from the local file
                                if (it->second.total == 0) {
                                    try {
                                        it->second.total = (size_t)std::filesystem::file_size(task.local);
                                    } catch (...) {
                                        // leave total as 0 if we can't determine it
                                    }
                                }
                                it->second.transferred += (size_t)written;
                            }
                        });

                    {
                        std::lock_guard<std::mutex> lk(upload_mutex);
                        auto it = upload_status_map.find(task.id);
                        if (it != upload_status_map.end()) {
                            it->second.done = true;
                            it->second.success = ok;
                            it->second.finished_time = std::chrono::steady_clock::now();
                        }
                    }
                    // track active uploads
                    uploads_in_progress.fetch_sub(1);
                } });

    // Start delete worker
    delete_worker_thread = std::thread([]()
                                       {
                while (!workers_stop) {
                    DeleteTask dt;
                    {
                        std::unique_lock<std::mutex> lk(delete_mutex);
                        delete_cv.wait(lk, [] {return workers_stop || !delete_tasks.empty(); });
                        if (workers_stop) break;
                        dt = delete_tasks.front(); delete_tasks.pop_front();
                    }
                    // perform delete (recursive)
                    DeleteRecursive(server_buf, share_buf, dt.remote, username_buf, password_buf);
                } });

    // Start download worker
    download_worker_thread = std::thread([]()
                                         {
                    while (!workers_stop) {
                        DownloadTask task;
                        {
                            std::unique_lock<std::mutex> lk(download_mutex);
                            download_cv.wait(lk, [] {return workers_stop || !download_tasks.empty(); });
                            if (workers_stop) break;
                            task = download_tasks.front(); download_tasks.pop_front();
                        }

                        {
                            std::lock_guard<std::mutex> lk(download_mutex);
                            auto& st = download_status_map[task.id];
                            st.id = task.id; st.local = task.local; st.remote = task.remote; st.transferred = 0; st.total = task.total_bytes; st.done = false; st.success = false;
                        }

                        bool ok = DownloadFileWithProgress(server_buf, share_buf, task.remote, task.local, username_buf, password_buf,
                            [&](ssize_t read) {
                                std::lock_guard<std::mutex> lk(download_mutex);
                                auto it = download_status_map.find(task.id);
                                if (it != download_status_map.end()) it->second.transferred += (size_t)read;
                            });

                        {
                            std::lock_guard<std::mutex> lk(download_mutex);
                            auto it = download_status_map.find(task.id);
                            if (it != download_status_map.end()) { it->second.done = true; it->second.success = ok; }
                        }
                    } });

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // Host window that fills the entire GLFW client area and is not movable.
        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("SMB Browser", NULL, host_flags);
        if (ImGui::Button("SMB Browser"))
        {
            if (settingsTab)
            {
                settingsTab = false;
            }
            if (!smbBrowsing)
            {
                smbBrowsing = true;
            }
            else
            {
                smbBrowsing = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Settings"))
        {
            if (smbBrowsing)
            {
                smbBrowsing = false;
            }
            if (!settingsTab)
            {
                settingsTab = true;
            }
            else
            {
                settingsTab = false;
            }
        }
        if (settingsTab)
        {
            smbBrowsing = false;
            showSettingsTab();
        }
        if (smbBrowsing)
        {
            ImGui::InputText("Server", server_buf, 64);
            ImGui::InputText("Folder/Share", share_buf, 64);
            ImGui::InputText("Username", username_buf, 64);
            ImGui::InputText("Password", password_buf, 64, ImGuiInputTextFlags_Password);

            if (ImGui::Button("Connect"))
            {
                strncpy(current_path, "", sizeof(current_path) - 1);
                file_list = ListSMBFiles(server_buf, share_buf, "", username_buf, password_buf);
                printf("Connected, found %zu files\n", file_list.size());
            }

            if (ImGui::Button("Back") && strlen(current_path) > 0)
            {
                // todo fix
                char *last_slash = strrchr(current_path, '/');
                if (last_slash && last_slash > current_path)
                {
                    *last_slash = 0;
                }
                else
                {
                    current_path[0] = '\0';
                }
                file_list = ListSMBFiles(server_buf, share_buf, current_path, username_buf, password_buf);
            }

            ImGui::SameLine();
            ImGui::Text("Current: %s", current_path);

            // DRAG & DROP ZONE WITH DEBUG
            ImGui::Separator();

            if (ImGui::BeginChild("DropZone", ImVec2(0, 80), true))
            {
                ImGui::Text("drag files for upload here!");

                if (ImGui::BeginDragDropTarget())
                {
                    printf("[DROP DEBUG] BeginDragDropTarget() = TRUE\n");
                    const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("Files");
                    if (payload && payload->Data && payload->DataSize > 0)
                    {
                        const char *path = (const char *)payload->Data;
                        printf("[DROP DEBUG] PAYLOAD PATH DETECTED: '%s'\n", path);
                        upload_queue.push_back(std::string(path));
                        upload_ready = !upload_queue.empty();
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::EndChild();

            // Always show staged deletes so user can undo even when not uploading
            if (!staged_deletes.empty())
            {
                ImGui::Separator();
                ImGui::Text("Staged deletes:");
                for (size_t i = 0; i < staged_deletes.size(); ++i)
                {
                    ImGui::Text("%zu: %s -> %s", i + 1, staged_deletes[i].original.c_str(), staged_deletes[i].temp.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("Undo Last Delete"))
                {
                    auto sd = staged_deletes.back();

                    bool moved_back = MoveRemote(server_buf, share_buf, sd.temp, sd.original, username_buf, password_buf);
                    if (moved_back)
                    {
                        staged_deletes.pop_back();
                        file_list = ListSMBFiles(server_buf, share_buf, current_path, username_buf, password_buf);
                        printf("UNDO: moved back '%s' -> '%s'\n", sd.temp.c_str(), sd.original.c_str());
                    }
                    else
                    {
                        printf("UNDO FAILED for '%s'\n", sd.temp.c_str());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Empty Trash"))
                {
                    // commit staged deletes to background worker
                    std::lock_guard<std::mutex> lk(delete_mutex);
                    for (const auto &sd : staged_deletes)
                        delete_tasks.push_back({sd.temp, sd.is_dir});
                    if (!staged_deletes.empty())
                        delete_cv.notify_one();
                    staged_deletes.clear();
                    file_list = ListSMBFiles(server_buf, share_buf, current_path, username_buf, password_buf);
                    printf("EMPTY TRASH: committed staged deletes to worker\n");
                }
            }

            if ((upload_ready && (!upload_queue.empty() || strlen(local_file_buf) > 0)) || upload_show_active)
            {
                ImGui::Separator();

                if (!upload_queue.empty())
                {
                    ImGui::Text("Queued for upload:");
                    for (size_t i = 0; i < upload_queue.size(); ++i)
                    {
                        ImGui::Text("%zu: %s", i + 1, upload_queue[i].c_str());
                        ImGui::SameLine();
                        char lbl[64];
                        sprintf(lbl, "Remove##%zu", i);
                        if (ImGui::SmallButton(lbl))
                        {
                            upload_queue.erase(upload_queue.begin() + i);
                            break;
                        }
                    }
                }

                // If a lone local_file_buf exists (back-compat), let user add it to queue
                if (strlen(local_file_buf) > 0 && upload_queue.empty())
                {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "READY: %s", local_file_buf);
                    ImGui::SameLine();
                    if (ImGui::Button("Add to Queue"))
                    {
                        upload_queue.push_back(std::string(local_file_buf));
                        local_file_buf[0] = 0;
                    }
                }

                // Upload all queued files
                if (!upload_queue.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Upload All"))
                    {
                        printf("UPLOAD ALL CLICKED (%zu files)\n", upload_queue.size());
                        // Starting a new upload run: clear any previous status and show upload UI
                        {
                            std::lock_guard<std::mutex> lk(upload_mutex);
                            upload_status_map.clear();
                        }
                        upload_show_active = true;
                        for (const auto &localPath : upload_queue)
                        {
                            std::string full_remote = std::string(current_path);
                            if (strlen(current_path) > 0 && current_path[0])
                                full_remote += "/";

                            // Use only the local filename when uploading (don't create local parent dirs remotely)
                            try
                            {
                                std::filesystem::path p(localPath);
                                std::string filename = p.filename().string();
                                if (!full_remote.empty() && full_remote.back() != '/')
                                    full_remote += "/";
                                full_remote += filename;
                            }
                            catch (...)
                            {
                                const char *lp = localPath.c_str();
                                const char *filename_slash = strrchr(lp, '/');
                                const char *filename_back = strrchr(lp, '\\');
                                const char *filename = filename_slash;
                                if (!filename || (filename_back && filename_back > filename))
                                    filename = filename_back;
                                if (!filename)
                                    filename = lp;
                                else
                                    filename++;
                                if (!full_remote.empty() && full_remote.back() != '/')
                                    full_remote += "/";
                                full_remote += filename;
                            }

                            printf("ENQUEUE UPLOAD -> smb://%s/%s/%s\n", server_buf, share_buf, full_remote.c_str());
                            // compute file size
                            size_t fsize = 0;
                            try
                            {
                                fsize = (size_t)std::filesystem::file_size(localPath);
                            }
                            catch (...)
                            {
                                fsize = 0;
                            }
                            int id = next_upload_id.fetch_add(1);
                            {
                                std::lock_guard<std::mutex> lk(upload_mutex);
                                upload_tasks.push_back(UploadTask{id, localPath, full_remote, fsize});
                                // initialize status entry explicitly so finished_time is defaulted
                                upload_status_map[id] = UploadStatus();
                                auto &st = upload_status_map[id];
                                st.id = id;
                                st.local = localPath;
                                st.remote = full_remote;
                                st.transferred = 0;
                                st.total = fsize;
                                st.done = false;
                                st.success = false;
                                st.finished_time = std::chrono::steady_clock::time_point();
                            }
                            uploads_in_progress.fetch_add(1);
                            upload_cv.notify_one();
                        }
                        // request a refresh after uploads complete (avoid doing SMB calls here)
                        refresh_listing_after_upload.store(true);
                        upload_queue.clear();
                        upload_ready = false;
                    }
                }

                // Active uploads
                if (!upload_status_map.empty())
                {
                    ImGui::Separator();
                    ImGui::Text("Active uploads:");
                    std::lock_guard<std::mutex> lk(upload_mutex);
                    // hide completed uploads after a short delay
                    auto now = std::chrono::steady_clock::now();
                    const auto hide_after = std::chrono::seconds(5);
                    std::vector<int> to_erase;
                    for (auto &p : upload_status_map)
                    {
                        auto &st = p.second;
                        if (st.done)
                        {
                            if (st.finished_time != std::chrono::steady_clock::time_point() && (now - st.finished_time) > hide_after)
                            {
                                to_erase.push_back(p.first);
                                continue;
                            }
                        }

                        float frac = 0.0f;
                        if (st.total > 0)
                            frac = (float)st.transferred / (float)st.total;
                        // show filename and numeric progress for debugging
                        if (st.total > 0)
                        {
                            ImGui::Text("%s (%zu / %zu bytes)", st.local.c_str(), st.transferred, st.total);
                        }
                        else
                        {
                            ImGui::Text("%s (%zu bytes)", st.local.c_str(), st.transferred);
                        }
                        ImGui::ProgressBar(frac, ImVec2(-1, 0));
                        if (st.done)
                            ImGui::Text(st.success ? "Done" : "Failed");
                    }
                    for (int id : to_erase)
                        upload_status_map.erase(id);
                }

                // Active downloads
                if (!download_status_map.empty())
                {
                    ImGui::Separator();
                    ImGui::Text("Active downloads:");
                    std::lock_guard<std::mutex> dlk(download_mutex);
                    for (auto &p : download_status_map)
                    {
                        auto &st = p.second;
                        float frac = 0.0f;
                        if (st.total > 0)
                            frac = (float)st.transferred / (float)st.total;
                        ImGui::Text("%s", st.local.c_str());
                        ImGui::ProgressBar(frac, ImVec2(-1, 0));
                        if (st.done)
                            ImGui::Text(st.success ? "Done" : "Failed");
                    }
                }
            }

            if (!file_list.empty())
            {
                ImGui::BeginChild("FileList", ImVec2(0, 200), true);
                // single column: name (actions inline)
                ImGui::Columns(1, "files_cols", false);
                for (size_t idx = 0; idx < file_list.size(); ++idx)
                {
                    const auto &file = file_list[idx];
                    ImGui::PushID((int)idx);
                    // name column: icon + selectable
                    ImGui::BeginGroup();
                    GLuint icon = file.is_dir ? GetFolderTexture() : GetFileTexture();
                    // compute full remote path for this entry
                    std::string full_remote = std::string(current_path);
                    if (strlen(current_path) > 0 && current_path[0])
                        full_remote += "/";
                    full_remote += file.name;
                    if (icon)
                    {
                        ImGui::Image((void *)(intptr_t)icon, ImVec2(16, 16));
                        ImGui::SameLine();
                    }
                    if (ImGui::Selectable(file.name.c_str(), false, 0, ImVec2(0, 0)))
                    {
                        if (!file.is_dir)
                        {
                            size_t fsize = (size_t)file.size;
                            int id = next_download_id.fetch_add(1);
                            {
                                std::lock_guard<std::mutex> lk(download_mutex);
                                download_tasks.push_back(DownloadTask{id, full_remote, std::string(file.name), fsize});
                                download_status_map[id] = DownloadStatus{id, full_remote, std::string(file.name), 0, fsize, false, false};
                            }
                            download_cv.notify_one();
                        }
                        else
                        {
                            std::string new_path = std::string(current_path);
                            if (strlen(current_path) > 0 && current_path[0])
                                new_path += "/";
                            new_path += file.name;
                            strncpy(current_path, new_path.c_str(), sizeof(current_path) - 1);
                            file_list = ListSMBFiles(server_buf, share_buf, current_path, username_buf, password_buf);
                        }
                    }

                    // context menu for this item (right-click) — attach to the Selectable above
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        printf("[CTX DEBUG] Right-click detected on '%s'\n", file.name.c_str());
                        rename_target_remote = full_remote;
                        strncpy(rename_buf, file.name.c_str(), sizeof(rename_buf) - 1);
                        rename_buf[sizeof(rename_buf) - 1] = '\0';
                        ImGui::OpenPopup("file_ctx");
                    }

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        printf("[CTX DEBUG] Right-click detected on '%s'\n", file.name.c_str());
                        rename_target_remote = full_remote;
                        strncpy(rename_buf, file.name.c_str(), sizeof(rename_buf) - 1);
                        rename_buf[sizeof(rename_buf) - 1] = '\0';
                        ImGui::OpenPopup("file_ctx");
                    }

                    if (ImGui::BeginPopup("file_ctx"))
                    {
                        if (ImGui::MenuItem("Rename"))
                        {
                            // prepare rename state and close the small popup; open modal next frame
                            rename_target_remote = full_remote;
                            strncpy(rename_buf, file.name.c_str(), sizeof(rename_buf) - 1);
                            rename_buf[sizeof(rename_buf) - 1] = '\0';
                            rename_open = true;
                            ImGui::CloseCurrentPopup();
                        }
                        if(ImGui::MenuItem("Delete"))
                        {
                            pending_delete_remote = full_remote;
                            pending_delete_is_dir = file.is_dir;
                            pending_delete_open_popup = true;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::NextColumn();

                    // actions column: delete button


                    ImGui::EndGroup();
                    ImGui::PopID();
                    ImGui::NextColumn();
                }

                if (pending_delete_open_popup)
                {
                    ImGui::OpenPopup("Confirm Delete");
                    pending_delete_open_popup = false;
                }
                if (ImGui::BeginPopupModal("Confirm Delete", NULL, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    if (!pending_delete_is_dir)
                        ImGui::Text("Delete file '%s'?", pending_delete_remote.c_str());
                    else
                        ImGui::Text("Delete directory '%s' and ALL its contents?", pending_delete_remote.c_str());
                    ImGui::Separator();
                    if (ImGui::Button("OK", ImVec2(120, 0)))
                    {
                        // If there are staged deletes already, commit them first for file-delete path
                        if (!pending_delete_is_dir)
                        {
                            if (!staged_deletes.empty())
                            {
                                std::lock_guard<std::mutex> lk(delete_mutex);
                                for (const auto &sd : staged_deletes)
                                    delete_tasks.push_back({sd.temp, sd.is_dir});
                                delete_cv.notify_one();
                                staged_deletes.clear();
                            }

                            // Stage single file delete via rename
                            time_t t = time(NULL);
                            // extract base name
                            std::string name_only = pending_delete_remote;
                            size_t pos = name_only.find_last_of('/');
                            if (pos != std::string::npos)
                                name_only = name_only.substr(pos + 1);
                            // sanitize
                            while (!name_only.empty() && name_only[0] == '.')
                                name_only.erase(0, 1);
                            const std::string del_prefix = "deleted_";
                            while (name_only.rfind(del_prefix, 0) == 0)
                            {
                                size_t after = del_prefix.size();
                                size_t next_us = name_only.find('_', after);
                                if (next_us == std::string::npos)
                                    break;
                                name_only.erase(0, next_us + 1);
                                while (!name_only.empty() && name_only[0] == '.')
                                    name_only.erase(0, 1);
                            }
                            char tmpname[512];
                            snprintf(tmpname, sizeof(tmpname), ".deleted_%ld_%s", (long)t, name_only.c_str());
                            std::string temp_remote = std::string(".") + std::string(tmpname);

                            printf("STAGE DELETE: rename '%s' -> '%s'\n", pending_delete_remote.c_str(), temp_remote.c_str());
                            bool moved = MoveRemote(server_buf, share_buf, pending_delete_remote, temp_remote, username_buf, password_buf);
                            if (moved)
                            {
                                staged_deletes.push_back({pending_delete_remote, temp_remote, false});
                                file_list = ListSMBFiles(server_buf, share_buf, current_path, username_buf, password_buf);
                            }
                            else
                            {
                                printf("STAGE DELETE FAILED for '%s'\n", pending_delete_remote.c_str());
                            }
                        }
                        else
                        {
                            // Directory delete path (existing behavior)
                            // If there's a staged delete pending, commit it first (synchronously)
                            if (!staged_deletes.empty())
                            {
                                for (const auto &sd : staged_deletes)
                                {
                                    DeleteRecursive(server_buf, share_buf, sd.temp, username_buf, password_buf);
                                }
                                staged_deletes.clear();
                            }

                            // Stage the directory delete via rename
                            time_t t = time(NULL);
                            std::string name_only = pending_delete_remote;
                            size_t pos = name_only.find_last_of('/');
                            if (pos != std::string::npos)
                                name_only = name_only.substr(pos + 1);
                            while (!name_only.empty() && name_only[0] == '.')
                                name_only.erase(0, 1);
                            const std::string del_prefix = "deleted_";
                            while (name_only.rfind(del_prefix, 0) == 0)
                            {
                                size_t after = del_prefix.size();
                                size_t next_us = name_only.find('_', after);
                                if (next_us == std::string::npos)
                                    break;
                                name_only.erase(0, next_us + 1);
                                while (!name_only.empty() && name_only[0] == '.')
                                    name_only.erase(0, 1);
                            }
                            char tmpname[512];
                            snprintf(tmpname, sizeof(tmpname), ".deleted_%ld_%s", (long)t, name_only.c_str());
                            std::string temp_remote = std::string(".") + std::string(tmpname);

                            printf("STAGE RECURSIVE DELETE: rename '%s' -> '%s'\n", pending_delete_remote.c_str(), temp_remote.c_str());
                            bool moved = MoveRemote(server_buf, share_buf, pending_delete_remote, temp_remote, username_buf, password_buf);
                            if (moved)
                            {
                                // commit any previously staged deletes in background
                                if (!staged_deletes.empty())
                                {
                                    std::lock_guard<std::mutex> lk(delete_mutex);
                                    for (const auto &sd : staged_deletes)
                                        delete_tasks.push_back({sd.temp, sd.is_dir});
                                    delete_cv.notify_one();
                                    staged_deletes.clear();
                                }
                                staged_deletes.push_back({pending_delete_remote, temp_remote, true});
                                file_list = ListSMBFiles(server_buf, share_buf, current_path, username_buf, password_buf);
                            }
                            else
                            {
                                printf("STAGE RECURSIVE DELETE FAILED for '%s'\n", pending_delete_remote.c_str());
                            }
                        }

                        pending_delete_remote.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    {
                        pending_delete_remote.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                if (rename_open)
                {
                    ImGui::OpenPopup("Alert!");
                    rename_open = false;
                }

                // Rename modal
                if (ImGui::BeginPopupModal("Alert!", NULL, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("Rename to:");
                    ImGui::InputText("New name", rename_buf, sizeof(rename_buf));
                    ImGui::Separator();
                    if (ImGui::Button("OK", ImVec2(120, 0)))
                    {
                        // construct new remote path
                        std::string new_remote;
                        size_t pos = rename_target_remote.find_last_of('/');
                        if (pos != std::string::npos)
                            new_remote = rename_target_remote.substr(0, pos);
                        else
                            new_remote.clear();
                        if (!new_remote.empty())
                            new_remote += "/";
                        new_remote += std::string(rename_buf);
                        printf("RENAME: '%s' -> '%s'\n", rename_target_remote.c_str(), new_remote.c_str());
                        bool moved = MoveRemote(server_buf, share_buf, rename_target_remote, new_remote, username_buf, password_buf);
                        if (moved)
                        {
                            file_list = ListSMBFiles(server_buf, share_buf, current_path, username_buf, password_buf);
                        }
                        else
                        {
                            printf("RENAME FAILED: '%s' -> '%s'\n", rename_target_remote.c_str(), new_remote.c_str());
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::Columns(1);
                ImGui::EndChild();
            }
        }

        ImGui::End();
        ImGui::Render();
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Signal workers to stop and join threads cleanly
    workers_stop = true;
    upload_cv.notify_all();
    delete_cv.notify_all();
    download_cv.notify_all();
    if (upload_worker_thread.joinable())
        upload_worker_thread.join();
    if (delete_worker_thread.joinable())
        delete_worker_thread.join();
    if (download_worker_thread.joinable())
        download_worker_thread.join();

    FreeImageTextures();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
