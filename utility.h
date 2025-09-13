#ifndef UTILITY_H
#define UTILITY_H

#pragma warning(disable : 4100 4201 4214 4305 4309 4244 4115 4514)
#include <windows.h>
#pragma warning(default : 4100 4201 4214 4305 4309 4244)

#include <io.h>
#include <time.h>
#include <wchar.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <direct.h>
#include <process.h>
#include <tlhelp32.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <Rpc.h>
#include <ctype.h>
#include <wctype.h>
#include <stddef.h>
#include <locale.h>
#include <string.h>
#include <stdint.h>




#include <ProToolkit.h>
#include <ProArray.h>
#include <ProTKRunTime.h>
#include <ProDrawing.h>
#include <ProModelitem.h>
#include <ProFeatType.h>
#include <ProIntfimport.h>
#include <ProMenu.h>
#include <ProUIDashboard.h>
#include <ProMenuBar.h>
#include <ProMessage.h>
#include <ProParameter.h>
#include <ProSimprep.h>
#include <ProSolid.h>
#include <ProEdge.h>
#include <ProEdgedata.h>
#include <ProSurface.h>
#include <ProAsmComp.h>
#include <ProArray.h>
#include <ProCsys.h>
#include <ProCsysdata.h>
#include <ProUICheckbutton.h>
#include <ProUIMessage.h>
#include <ProSelbuffer.h>
#include <ProSelection.h>
#include <ProUIList.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProGeomitem.h>
#include <ProUtil.h>
#include <ProUIDialog.h>
#include <ProUI.h>
#include <ProUILabel.h>
#include <ProUIMessage.h>
#include <ProUdf.h>
#include <ProWindows.h>
#include <ProWString.h>
#include <ProWTUtils.h>
#include <ProUITree.h>
#include <ProUILayout.h>
#include <ProUIRadiogroup.h>
#include <ProUILabel.h>
#include <ProUIDrawingarea.h>
#include <ProUIInputpanel.h>
#include <ProCollect.h>
#include "symboltable.h"

#define MAX_MSG_BUFFER_SIZE 1024


#define SQ(a) ((a) * (a))
static wchar_t wMsgFile[] = L"EmjacParametricAutomation.txt";

extern wchar_t selectedTabFilePath[MAX_PATH];


typedef struct SelMapEntry {
    char* key;
    char* label;
} SelMapEntry;

ProError ProGenericMsg(wchar_t* wMsg);
void ProPrintf(const wchar_t* format, ...);
void ProPrintfChar(const char* format, ...);
void LogOnlyPrintf(const wchar_t* format, ...);
void LogOnlyPrintfChar(const char* format, ...);
wchar_t* char_to_wchar(const char* str);
char* wchar_to_char(const wchar_t* wstr);
// Helper function to convert string to lowercase (add this if not available)
void to_lowercase(char* str);
bool get_gif_dimensions(const char* filepath, int* width, int* height);
int starts_with(const char* str, const char* prefix);
void selmap_set_path(const char* path);
int  selmap_reload(void);
int  selmap_lookup_w(const char* param, wchar_t** out_wlabel);



#endif // !UTILITY_H
