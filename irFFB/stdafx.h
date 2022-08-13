// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

//#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <Strsafe.h>
#include <Shlobj.h>
#include <timeapi.h>
#include <shellapi.h>
#include <dbt.h>
#include <xmmintrin.h>
#include <commctrl.h>
#include <hidclass.h>
#include <hidsdi.h>
#include <SetupAPI.h>
#include <winhttp.h>
#include <cfgmgr32.h>
#include <comdef.h>
#include <RegStr.h>
#include <newdev.h>
#include <devguid.h>
#include <sddl.h>
#include <string>
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define DIRECTINPUT_VERSION 0x0800 
#include <dinput.h>

#define _USE_MATH_DEFINES
#include <cmath>
#include <chrono>
#include <thread>


#ifdef _UNICODE

#if defined _M_IX86

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")

#elif defined _M_IA64

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")

#elif defined _M_X64

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")

#else

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#endif

#endif
