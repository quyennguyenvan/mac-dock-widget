#include "libs/json/json.h"

std::wstring JStr(const std::wstring& j, const std::wstring& key) {
    std::wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == std::wstring::npos) return {};
    p += k.size();
    while (p < j.size() && (j[p] == L' ' || j[p] == L':' || j[p] == L'\t')) p++;
    if (p >= j.size() || j[p] != L'"') return {};
    size_t s = ++p;
    while (p < j.size() && j[p] != L'"') p++;
    return j.substr(s, p - s);
}

double JNum(const std::wstring& j, const std::wstring& key) {
    std::wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == std::wstring::npos) return 0;
    p += k.size();
    while (p < j.size() && (j[p] == L' ' || j[p] == L':' || j[p] == L'\t')) p++;
    return _wtof(j.c_str() + p);
}

int JInt(const std::wstring& j, const std::wstring& key) {
    return (int)JNum(j, key);
}
