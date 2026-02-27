#ifndef SYSMON_TOOLTIP_H
#define SYSMON_TOOLTIP_H

#include "libs/globals/globals.h"

void InitTip(HWND parent);
int HitTestCore(int cx, int cy);
int HitTestVol(int cx, int cy);
void ShowTip(HWND hw, const wchar_t* text);
void HideTip(HWND hw);
void UpdateTip(HWND hw);

#endif
