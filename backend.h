#ifndef BACKEND_H
#define BACKEND_H

#include <samba-4.0/libsmbclient.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <functional>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <sstream>
#include <mutex>

int file_exists_fopen(const char *filename) {
    FILE *file;
    // Try to open the file in read mode ("r")
    if ((file = fopen(filename, "r")) != NULL) {
        // If successful, the file exists, so close it and return true (1)
        fclose(file);
        return 1;
    } else {
        // If fopen returns NULL, the file does not exist or an error occurred
        return 0;
    }
}


// URL-encode helper for SMB URLs. Leaves '/' unencoded so path separators remain.
static std::string UrlEncode(const std::string& s) {
    std::string out;
    const char *hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        // unreserved characters according to RFC3986 plus '/' left as-is
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~' || c == '/') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

// (moved below so auth globals are declared before use)
struct SMBFileInfo {
    std::string name;
    bool is_dir;
    uint64_t size;
};

// Global auth credentials used by the SMB auth callback
static std::string g_smb_username = "";
static std::string g_smb_password = "";

// Global auth callback (fixes deprecated lambda issue)
void auth_fn(const char *server, const char *share,
             char *workgroup, int wgmaxlen,
             char *username, int unmaxlen,
             char *password, int pwmaxlen) {
    if (!g_smb_username.empty()) {
        strncpy(username, g_smb_username.c_str(), unmaxlen - 1);
        username[unmaxlen - 1] = '\0';
    } else {
        strncpy(username, "guest", unmaxlen - 1);
        username[unmaxlen - 1] = '\0';
    }

    if (!g_smb_password.empty()) {
        strncpy(password, g_smb_password.c_str(), pwmaxlen - 1);
        password[pwmaxlen - 1] = '\0';
    } else {
        password[0] = '\0';
    }

    workgroup[0] = 0;
}

// global mutex to serialize libsmbclient operations (some backends/protocols
// are not safe for concurrent smbc calls from multiple threads)
static std::mutex g_smb_mutex;

// Ensure remote directory exists by creating any missing path components.
// Returns true if the directory exists or was created successfully.
static bool EnsureRemoteDirExists(const std::string& server, const std::string& share,
                                  const std::string& remoteDir,
                                  const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> smb_lock(g_smb_mutex);
    if (remoteDir.empty()) return true;
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string base = std::string("smb://") + server + "/" + share;

    std::istringstream iss(remoteDir);
    std::string token;
    std::string accum;
    while (std::getline(iss, token, '/')) {
        if (token.empty()) continue;
        if (!accum.empty()) accum += "/";
        accum += token;
        std::string cur = base + "/" + UrlEncode(accum);
        int dh = smbc_opendir(cur.c_str());
            if (dh == -1) {
                // Try a raw (non-URL-encoded) path as some servers dislike percent-encoding
                std::string cur_raw = base + "/" + accum;
                int pdh_raw = smbc_opendir(cur_raw.c_str());
                if (pdh_raw != -1) {
                    smbc_closedir(pdh_raw);
                    continue;
                }

                // try to create (first encoded, then raw)
                if (smbc_mkdir(cur.c_str(), 0777) == -1) {
                    if (errno == EEXIST) {
                        // exists, continue
                    } else {
                        // try raw mkdir
                        if (smbc_mkdir(cur_raw.c_str(), 0777) == -1) {
                            if (errno == EEXIST) {
                                // exists
                            } else {
                                printf("EnsureRemoteDirExists: mkdir failed for '%s' and '%s' (errno=%d: %s)\n", cur.c_str(), cur_raw.c_str(), errno, strerror(errno));
                                return false;
                            }
                        } else {
                            printf("EnsureRemoteDirExists: created (raw) '%s'\n", cur_raw.c_str());
                        }
                    }
                } else {
                    printf("EnsureRemoteDirExists: created '%s'\n", cur.c_str());
                }
            } else {
                smbc_closedir(dh);
            }
    }
    return true;
}

std::vector<SMBFileInfo> ListSMBFiles(const std::string& server, const std::string& share, 
                                    const std::string& path, const std::string& username, 
                                    const std::string& password) {
    std::vector<SMBFileInfo> files;
    std::lock_guard<std::mutex> smb_lock(g_smb_mutex);
    // Set credentials for auth callback and initialize
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);  // Uses function pointer, not lambda
    
    std::string smb_url = "smb://" + server;
    if (!share.empty()) smb_url += "/" + share;
    if (!path.empty()) smb_url += "/" + UrlEncode(path);
    printf("DEBUG smb_url (opendir): %s\n", smb_url.c_str());
    

    int dh = smbc_opendir(smb_url.c_str());
    if (dh == -1) return files;
    
    struct smbc_dirent* dirent;
    while ((dirent = smbc_readdir(dh)) != NULL) { 
        if (strcmp(dirent->name, ".") == 0 || strcmp(dirent->name, "..") == 0) 
            continue;
            
        SMBFileInfo info;
        info.name = dirent->name;
        info.is_dir = (dirent->smbc_type == SMBC_DIR); 
        info.size = 0;  
        
        files.push_back(info);
    }
    
    smbc_closedir(dh);  
    return files;
}

bool DownloadFile(const std::string& server, const std::string& share, 
                  const std::string& path, const std::string& localFile,
                  const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> smb_lock(g_smb_mutex);
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);
    
    std::string smb_url = "smb://" + server + "/" + share + "/" + UrlEncode(path);
    printf("DEBUG smb_url (open/download): %s\n", smb_url.c_str());
    
    // Returns int fd (NOT SMBCFILE*)
    int fd = smbc_open(smb_url.c_str(), O_RDONLY, 0);
    if (fd == -1) return false;
    
    std::ofstream out(localFile, std::ios::binary);
    if (!out.is_open()) {
        smbc_close(fd);
        return false;
    }
    
    char buffer[4096];
    ssize_t bytes;
    while ((bytes = smbc_read(fd, buffer, sizeof(buffer))) > 0) {
        out.write(buffer, bytes);
    }
    
    out.close();
    smbc_close(fd);
    return true;
}

// Download with progress callback: calls progress_cb(bytes_read) after each chunk read.
bool DownloadFileWithProgress(const std::string& server, const std::string& share,
                              const std::string& path, const std::string& localFile,
                              const std::string& username, const std::string& password,
                              const std::function<void(ssize_t)>& progress_cb) {
    std::lock_guard<std::mutex> smb_lock(g_smb_mutex);
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string smb_url = "smb://" + server + "/" + share + "/" + UrlEncode(path);
    printf("DEBUG smb_url (open/download with progress): %s\n", smb_url.c_str());

    int fd = smbc_open(smb_url.c_str(), O_RDONLY, 0);
    if (fd == -1) {
        printf("DownloadFileWithProgress: failed to open remote '%s' (errno=%d: %s)\n", smb_url.c_str(), errno, strerror(errno));
        return false;
    }

    std::ofstream out(localFile, std::ios::binary);
    if (!out.is_open()) {
        smbc_close(fd);
        printf("DownloadFileWithProgress: failed to open local '%s' for writing\n", localFile.c_str());
        return false;
    }

    char buffer[4096];
    ssize_t bytes;
    while ((bytes = smbc_read(fd, buffer, sizeof(buffer))) > 0) {
        out.write(buffer, bytes);
        if (progress_cb) progress_cb(bytes);
    }

    out.close();
    smbc_close(fd);
    return bytes >= 0;
}

bool UploadFile(const std::string& server, const std::string& share, 
                const std::string& remotePath, const std::string& localFile,
                const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> smb_lock(g_smb_mutex);
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);
    
    std::string smb_url = "smb://" + server + "/" + share + "/" + UrlEncode(remotePath);
    printf("DEBUG smb_url (open/upload): %s\n", smb_url.c_str());
    
    // Open local file for reading
    std::ifstream in(localFile, std::ios::binary);
    if (!in.is_open()) {
        printf("UploadFile: failed to open local file '%s'\n", localFile.c_str());
        return false;
    }

    // Open remote file for writing (O_WRONLY | O_CREAT | O_TRUNC)
        // Verify parent directory exists before open
        std::string parent = remotePath;
        size_t ppos = parent.find_last_of('/');
        if (ppos != std::string::npos) parent = parent.substr(0, ppos);
        else parent.clear();
        std::string parent_url = std::string("smb://") + server + "/" + share;
        if (!parent.empty()) parent_url += "/" + UrlEncode(parent);
        printf("DEBUG smb_url (parent opendir): %s\n", parent_url.c_str());
        int pdh = smbc_opendir(parent_url.c_str());
        if (pdh == -1) {
            printf("Parent opendir failed for '%s' (errno=%d: %s)\n", parent_url.c_str(), errno, strerror(errno));
            // attempt to create the parent directory chain
            if (!parent.empty()) {
                if (EnsureRemoteDirExists(server, share, parent, username, password)) {
                    int pdh_retry = smbc_opendir(parent_url.c_str());
                    if (pdh_retry == -1) {
                        printf("After mkdir, parent opendir still failed for '%s' (errno=%d: %s)\n", parent_url.c_str(), errno, strerror(errno));
                    } else {
                        smbc_closedir(pdh_retry);
                    }
                }
            }
        } else {
            smbc_closedir(pdh);
        }

        int fd = smbc_open(smb_url.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        printf("UploadFile: failed to open remote '%s' (errno=%d: %s)\n", smb_url.c_str(), errno, strerror(errno));
        in.close();
        return false;
    }

    char buffer[4096];
    while (true) {
        in.read(buffer, sizeof(buffer));
        std::streamsize bytes = in.gcount();
        if (bytes > 0) {
            ssize_t written = smbc_write(fd, buffer, (size_t)bytes);
            if (written != bytes) {
                printf("UploadFile: write error for '%s' (wrote %zd of %zd, errno=%d: %s)\n", smb_url.c_str(), written, bytes, errno, strerror(errno));
                smbc_close(fd);
                in.close();
                return false;
            }
        }
        if (!in) break; // EOF or error after processing gcount
    }

    smbc_close(fd);
    in.close();
    printf("UploadFile: upload finished for local '%s' -> '%s'\n", localFile.c_str(), smb_url.c_str());
    return true;
}

// Upload with progress callback: calls progress_cb(bytes_written) after each chunk written.
bool UploadFileWithProgress(const std::string& server, const std::string& share,
                            const std::string& remotePath, const std::string& localFile,
                            const std::string& username, const std::string& password,
                            const std::function<void(ssize_t)>& progress_cb) {
    std::lock_guard<std::mutex> smb_lock(g_smb_mutex);
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string smb_url = "smb://" + server + "/" + share + "/" + UrlEncode(remotePath);

    std::ifstream in(localFile, std::ios::binary);
    if (!in.is_open()) {
        printf("UploadFileWithProgress: failed to open local file '%s'\n", localFile.c_str());
        return false;
    }

    printf("DEBUG smb_url (open/upload progress): %s\n", smb_url.c_str());
    // Verify parent directory exists before open
    std::string parentp = remotePath;
    size_t ppos2 = parentp.find_last_of('/');
    if (ppos2 != std::string::npos) parentp = parentp.substr(0, ppos2);
    else parentp.clear();
    std::string parent_url2 = std::string("smb://") + server + "/" + share;
    if (!parentp.empty()) parent_url2 += "/" + UrlEncode(parentp);
    printf("DEBUG smb_url (parent opendir for progress): %s\n", parent_url2.c_str());
    int pdh2 = smbc_opendir(parent_url2.c_str());
    if (pdh2 == -1) {
        printf("Parent opendir failed for '%s' (errno=%d: %s)\n", parent_url2.c_str(), errno, strerror(errno));
        if (!parentp.empty()) {
            if (EnsureRemoteDirExists(server, share, parentp, username, password)) {
                int pdh_retry2 = smbc_opendir(parent_url2.c_str());
                if (pdh_retry2 == -1) {
                    printf("After mkdir, parent opendir still failed for '%s' (errno=%d: %s)\n", parent_url2.c_str(), errno, strerror(errno));
                } else {
                    smbc_closedir(pdh_retry2);
                }
            }
        }
    } else {
        smbc_closedir(pdh2);
    }
    int fd = smbc_open(smb_url.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        // try raw (non-URL-encoded) path as a fallback
        std::string smb_url_raw = std::string("smb://") + server + "/" + share;
        if (!remotePath.empty()) smb_url_raw += "/" + remotePath;
        printf("UploadFileWithProgress: initial open failed for '%s' (errno=%d: %s), trying raw '%s'\n", smb_url.c_str(), errno, strerror(errno), smb_url_raw.c_str());
        fd = smbc_open(smb_url_raw.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd == -1) {
            printf("UploadFileWithProgress: raw open also failed for '%s' (errno=%d: %s)\n", smb_url_raw.c_str(), errno, strerror(errno));
            in.close();
            return false;
        }
    }

    char buffer[8192];
    while (true) {
        in.read(buffer, sizeof(buffer));
        std::streamsize bytes = in.gcount();
        if (bytes > 0) {
            ssize_t written = smbc_write(fd, buffer, (size_t)bytes);
            if (written != bytes) {
                printf("UploadFileWithProgress: write error for '%s' (wrote %zd of %zd, errno=%d: %s)\n", smb_url.c_str(), written, bytes, errno, strerror(errno));
                smbc_close(fd);
                in.close();
                return false;
            }
            if (progress_cb) progress_cb((ssize_t)written);
        }
        if (!in) break;
    }

    smbc_close(fd);
    in.close();
    printf("UploadFileWithProgress: upload finished for local '%s' -> '%s'\n", localFile.c_str(), smb_url.c_str());
    return true;
}

// Delete remote file or directory. If `is_dir` is true, attempts rmdir.
bool DeleteFile(const std::string& server, const std::string& share,
                const std::string& remotePath, bool is_dir,
                const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> smb_lock(g_smb_mutex);
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string smb_url = "smb://" + server + "/" + share + "/" + UrlEncode(remotePath);
    printf("DEBUG smb_url (delete): %s\n", smb_url.c_str());
    int res = -1;
    if (is_dir) {
        res = smbc_rmdir(smb_url.c_str());
    } else {
        res = smbc_unlink(smb_url.c_str());
    }
    if (res == -1) {
        printf("DeleteFile: failed to delete '%s' (errno=%d)\n", smb_url.c_str(), errno);
        return false;
    }
    printf("DeleteFile: deleted '%s'\n", smb_url.c_str());
    return true;
}

// Recursively delete a directory tree on the SMB share. If the target is not
// a directory, this will attempt to unlink it as a file.
bool DeleteRecursive(const std::string& server, const std::string& share,
                     const std::string& remotePath,
                     const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> smb_lock(g_smb_mutex);
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string smb_url = "smb://" + server + "/" + share;
    if (!remotePath.empty()) smb_url += "/" + UrlEncode(remotePath);
    printf("DEBUG smb_url (opendir/deleteRecursive): %s\n", smb_url.c_str());

    // Try opening as directory
    int dh = smbc_opendir(smb_url.c_str());
    if (dh == -1) {
        // Not a directory or cannot open - try unlink as file
        if (smbc_unlink(smb_url.c_str()) == 0) {
            printf("DeleteRecursive: unlinked file '%s'\n", smb_url.c_str());
            return true;
        }
        printf("DeleteRecursive: opendir and unlink failed for '%s' (errno=%d)\n", smb_url.c_str(), errno);
        return false;
    }

    struct smbc_dirent *ent;
    while ((ent = smbc_readdir(dh)) != NULL) {
        if (strcmp(ent->name, ".") == 0 || strcmp(ent->name, "..") == 0) continue;

        std::string child_remote = remotePath.empty() ? std::string(ent->name)
                                                      : remotePath + "/" + ent->name;

        // If directory, recurse; otherwise unlink the file
        if (ent->smbc_type == SMBC_DIR) {
            if (!DeleteRecursive(server, share, child_remote, username, password)) {
                smbc_closedir(dh);
                return false;
            }
        } else {
            std::string child_url = "smb://" + server + "/" + share + "/" + UrlEncode(child_remote);
            if (smbc_unlink(child_url.c_str()) == -1) {
                printf("DeleteRecursive: unlink failed '%s' (errno=%d)\n", child_url.c_str(), errno);
                smbc_closedir(dh);
                return false;
            }
            printf("DeleteRecursive: unlinked file '%s'\n", child_url.c_str());
        }
    }

    smbc_closedir(dh);

    // Directory should now be empty; remove it
    if (smbc_rmdir(smb_url.c_str()) == -1) {
        printf("DeleteRecursive: rmdir failed '%s' (errno=%d)\n", smb_url.c_str(), errno);
        return false;
    }

    printf("DeleteRecursive: removed directory '%s'\n", smb_url.c_str());
    return true;
}

// Rename (move) a remote file/directory within the same share.
bool MoveRemote(const std::string& server, const std::string& share,
                const std::string& oldRemotePath, const std::string& newRemotePath,
                const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> smb_lock(g_smb_mutex);
    g_smb_username = username;
    g_smb_password = password;
    smbc_init(auth_fn, 1);

    std::string old_url = "smb://" + server + "/" + share;
    if (!oldRemotePath.empty()) old_url += "/" + UrlEncode(oldRemotePath);
    std::string new_url = "smb://" + server + "/" + share;
    if (!newRemotePath.empty()) new_url += "/" + UrlEncode(newRemotePath);
    printf("DEBUG smb_url (rename): %s -> %s\n", old_url.c_str(), new_url.c_str());

    int res = smbc_rename(old_url.c_str(), new_url.c_str());
    if (res == -1) {
        printf("MoveRemote: rename failed '%s' -> '%s' (errno=%d)\n", old_url.c_str(), new_url.c_str(), errno);
        return false;
    }
    printf("MoveRemote: renamed '%s' -> '%s'\n", old_url.c_str(), new_url.c_str());
    return true;
}

#endif
