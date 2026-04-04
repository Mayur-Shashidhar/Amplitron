// =============================================================================
// Native file dialog implementations (Windows, macOS, Linux)
// =============================================================================

#include "gui/file_dialog.h"
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

#ifdef __APPLE__
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#endif

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace GuitarAmp {

#ifdef _WIN32
std::string show_save_dialog(const std::string& default_name,
                             const std::string& filter_desc,
                             const std::string& filter_ext) {
    char filename[MAX_PATH];
    std::strncpy(filename, default_name.c_str(), MAX_PATH - 1);
    filename[MAX_PATH - 1] = '\0';

    // Build filter string: "WAV Audio (*.wav)\0*.wav\0All Files (*.*)\0*.*\0\0"
    char filter[256];
    std::memset(filter, 0, sizeof(filter));
    int pos = 0;
    pos += snprintf(filter + pos, 256 - pos, "%s (*.%s)", filter_desc.c_str(), filter_ext.c_str());
    pos++; // null separator
    pos += snprintf(filter + pos, 256 - pos, "*.%s", filter_ext.c_str());
    pos++; // null separator
    pos += snprintf(filter + pos, 256 - pos, "All Files (*.*)");
    pos++;
    pos += snprintf(filter + pos, 256 - pos, "*.*");
    // double null terminator is already there from memset

    // Get desktop/documents as initial dir
    char initial_dir[MAX_PATH] = "";
    SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, initial_dir);

    OPENFILENAMEA ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initial_dir;
    ofn.lpstrTitle = "Save Recording As";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = filter_ext.c_str();

    if (GetSaveFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
}

#elif defined(__APPLE__)
std::string show_save_dialog(const std::string& default_name,
                             const std::string& /*filter_desc*/,
                             const std::string& filter_ext) {
    // Use osascript to show a native NSSavePanel
    std::string cmd = "osascript -e 'set theFile to POSIX path of (choose file name "
                      "with prompt \"Save Recording As\" "
                      "default name \"" + default_name + "\")' 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buf[1024];
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    pclose(pipe);

    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    if (result.empty()) return "";

    // Ensure it ends with the correct extension
    if (result.size() < filter_ext.size() + 1 ||
        result.substr(result.size() - filter_ext.size() - 1) != "." + filter_ext) {
        result += "." + filter_ext;
    }

    return result;
}

#else // Linux
std::string show_save_dialog(const std::string& default_name,
                             const std::string& filter_desc,
                             const std::string& filter_ext) {
    // Try zenity first, then kdialog
    std::string cmd = "zenity --file-selection --save --confirm-overwrite "
                      "--title='Save Recording As' "
                      "--filename='" + default_name + "' "
                      "--file-filter='" + filter_desc + " (*." + filter_ext + ")|*." + filter_ext + "' "
                      "--file-filter='All Files (*)|*' 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buf[1024];
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    int status = pclose(pipe);

    // If zenity failed (not installed), try kdialog
    if (status != 0) {
        cmd = "kdialog --getsavefilename ~/ '*." + filter_ext + "|" + filter_desc + "' "
              "--title 'Save Recording As' 2>/dev/null";
        pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        result.clear();
        while (fgets(buf, sizeof(buf), pipe)) {
            result += buf;
        }
        pclose(pipe);
    }

    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    if (result.empty()) return "";

    // Ensure it ends with the correct extension
    if (result.size() < filter_ext.size() + 1 ||
        result.substr(result.size() - filter_ext.size() - 1) != "." + filter_ext) {
        result += "." + filter_ext;
    }

    return result;
}
#endif

#ifdef _WIN32
std::string show_folder_dialog(const std::string& title) {
    BROWSEINFOA bi;
    std::memset(&bi, 0, sizeof(bi));
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return "";

    char path[MAX_PATH];
    bool ok = SHGetPathFromIDListA(pidl, path);
    CoTaskMemFree(pidl);
    return ok ? std::string(path) : "";
}

#elif defined(__APPLE__)
std::string show_folder_dialog(const std::string& title) {
    // Sanitize title for embedding in an AppleScript string literal:
    // escape backslashes first, then double-quotes.
    std::string safe_title;
    for (char c : title) {
        if (c == '\\') { safe_title += "\\\\"; }
        else if (c == '"') { safe_title += "\\\""; }
        else { safe_title += c; }
    }

    std::string script = "POSIX path of (choose folder with prompt \"" + safe_title + "\")";

    // Use fork+exec to invoke /usr/bin/osascript directly, bypassing /bin/sh
    // so the script string is never interpreted by a shell.
    int pipefd[2];
    if (pipe(pipefd) != 0) return "";

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return "";
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execl("/usr/bin/osascript", "osascript", "-e", script.c_str(), nullptr);
        _exit(1);
    }

    // Parent process
    close(pipefd[1]);
    char buf[1024];
    std::string result;
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
        result.append(buf, static_cast<size_t>(n));
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    // osascript returns paths with trailing slash; strip it for consistency
    if (!result.empty() && result.back() == '/') result.pop_back();

    return result;
}

#else // Linux
std::string show_folder_dialog(const std::string& title) {
    // Sanitize title for single-quote shell embedding: replace ' with '\''
    std::string safe_title;
    for (char c : title) {
        if (c == '\'') { safe_title += "'\\''"; }
        else { safe_title += c; }
    }

    std::string cmd = "zenity --file-selection --directory "
                      "--title='" + safe_title + "' 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buf[1024];
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    int wait_status = pclose(pipe);
    int exit_code = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1;

    if (exit_code != 0) {
        // Exit status 1 means the user cancelled in zenity — return empty.
        if (exit_code == 1) return "";

        // Any other non-zero status means zenity is unavailable; try kdialog.
        cmd = "kdialog --getexistingdirectory ~/ --title '" + safe_title + "' 2>/dev/null";
        pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        result.clear();
        while (fgets(buf, sizeof(buf), pipe)) {
            result += buf;
        }
        pclose(pipe);
    }

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    return result;
}
#endif

} // namespace GuitarAmp
