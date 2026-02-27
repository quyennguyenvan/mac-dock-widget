#ifndef SYSMON_JSON_H
#define SYSMON_JSON_H

#include "libs/common/common.h"

std::wstring JStr(const std::wstring& j, const std::wstring& key);
double JNum(const std::wstring& j, const std::wstring& key);
int JInt(const std::wstring& j, const std::wstring& key);

#endif
