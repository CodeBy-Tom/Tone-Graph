#include "process_util.h"
#include <tlhelp32.h>

std::wstring nameFromPid(DWORD pid) {
    if (!pid) return L"(unknown)";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"pid " + std::to_wstring(pid);
    wchar_t path[MAX_PATH]{};
    DWORD n = MAX_PATH;
    std::wstring out = L"pid " + std::to_wstring(pid);
    if (QueryFullProcessImageNameW(h, 0, path, &n)) {
        const wchar_t* b = wcsrchr(path, L'\\');
        out = b ? b + 1 : path;
    }
    CloseHandle(h);
    return out;
}

std::vector<DWORD> pidsNamed(const std::wstring& exe) {
    std::vector<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{ sizeof(e) };
    if (Process32FirstW(snap, &e)) {
        do {
            if (_wcsicmp(e.szExeFile, exe.c_str()) == 0) out.push_back(e.th32ProcessID);
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return out;
}

DWORD treeRoot(DWORD pid) {
    const std::wstring name = nameFromPid(pid);
    DWORD cur = pid;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pid;
    for (int n = 0; n < 16; ++n) {
        DWORD parent = 0;
        PROCESSENTRY32W e{ sizeof(e) };
        if (Process32FirstW(snap, &e)) {
            do {
                if (e.th32ProcessID == cur) { parent = e.th32ParentProcessID; break; }
            } while (Process32NextW(snap, &e));
        }
        if (!parent || parent == cur || nameFromPid(parent) != name) break;
        cur = parent;
    }
    CloseHandle(snap);
    return cur;
}
