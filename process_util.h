#pragma once

#include <windows.h>
#include <string>
#include <vector>

std::wstring nameFromPid(DWORD pid);
std::vector<DWORD> pidsNamed(const std::wstring& exe);
DWORD treeRoot(DWORD pid);
