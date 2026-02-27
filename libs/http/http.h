#ifndef SYSMON_HTTP_H
#define SYSMON_HTTP_H

#include "libs/common/common.h"

std::string HttpGet(const wchar_t* host, const wchar_t* path, bool tls);

#endif
