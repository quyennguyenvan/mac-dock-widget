#ifndef SYSMON_UTIL_H
#define SYSMON_UTIL_H

#include "libs/common/common.h"

std::wstring ToWide(const std::string& s);
void FmtSpeed(double bps, wchar_t* buf, int len);
void FmtMem(ULONGLONG mb, wchar_t* buf, int len);
void FmtDisk(double gb, wchar_t* buf, int len);

#endif
