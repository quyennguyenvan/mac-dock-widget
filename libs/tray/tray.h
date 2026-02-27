#ifndef SYSMON_TRAY_H
#define SYSMON_TRAY_H

#include "libs/globals/globals.h"

void AddTray(HWND hw);
void RemoveTray();
void ShowTrayMenu(HWND hw);
void ToggleAutoStart();

#endif
