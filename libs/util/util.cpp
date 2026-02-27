#include "libs/util/util.h"

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

void FmtSpeed(double bps, wchar_t* buf, int len) {
    if (bps < 1024.0)
        swprintf_s(buf, len, L"%.0f B/s", bps);
    else if (bps < 1048576.0)
        swprintf_s(buf, len, L"%.1f KB/s", bps / 1024.0);
    else if (bps < 1073741824.0)
        swprintf_s(buf, len, L"%.1f MB/s", bps / 1048576.0);
    else
        swprintf_s(buf, len, L"%.2f GB/s", bps / 1073741824.0);
}

void FmtMem(ULONGLONG mb, wchar_t* buf, int len) {
    if (mb >= 1024)
        swprintf_s(buf, len, L"%.1f GB", (double)mb / 1024.0);
    else
        swprintf_s(buf, len, L"%llu MB", mb);
}

void FmtDisk(double gb, wchar_t* buf, int len) {
    if (gb >= 1024.0) swprintf_s(buf, len, L"%.1f TB", gb / 1024.0);
    else              swprintf_s(buf, len, L"%.1f GB", gb);
}
