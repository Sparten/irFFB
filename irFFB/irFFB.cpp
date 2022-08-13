/*
Copyright (c) 2016 NLP

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "irFFB.h"
#include "Settings.h"
#include "public.h"
#include "yaml_parser.h"
#include "vjoyinterface.h"
#include "shlwapi.h"
#include <Hidclass.h>
#include <uxtheme.h>
#include <intrin.h>
#define MAX_LOADSTRING 100

#define STATUS_CONNECTED_PART 0
#define STATUS_ONTRACK_PART 1
#define STATUS_CAR_PART 2

extern HANDLE hDataValidEvent;

// Globals
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
NOTIFYICONDATA niData;

HANDLE globalMutex;

HANDLE debugHnd = INVALID_HANDLE_VALUE;
wchar_t debugLastMsg[512];
LONG debugRepeat = 0;

LPDIRECTINPUT8 pDI = nullptr;
LPDIRECTINPUTDEVICE8 ffdevice = nullptr;
LPDIRECTINPUTEFFECT effect = nullptr;

CRITICAL_SECTION effectCrit;

DIJOYSTATE joyState;
DWORD axes[1] = { DIJOFS_X };
LONG  dir[1] = { 0 };
DIPERIODIC pforce;
DIEFFECT   dieff;

LogiLedData logiLedData;
DIEFFESCAPE logiEscape;

Settings settings;


float firc6[] = {
	0.1295867f, 0.2311436f, 0.2582509f, 0.1923936f, 0.1156718f, 0.0729534f
};
float firc12[] = {
	0.0322661f, 0.0696877f, 0.0967984f, 0.1243019f, 0.1317534f, 0.1388793f,
	0.1129315f, 0.0844297f, 0.0699100f, 0.0567884f, 0.0430215f, 0.0392321f
};

char car[MAX_CAR_NAME];
understeerCoefs usteerCoefs[] = {
	{ "astonmartin dbr9",   46.0f, 78.0f  },
	{ "audirs3lms",         40.0f, 70.0f  },
	{ "audir8gt3",          52.0f, 78.0f  },
	{ "bmwm8gte",           46.0f, 78.0f  },
	{ "bmwm4gt4",           40.0f, 70.0f  },
	{ "bmwz4gt3",           54.0f, 80.0f  },
	{ "bmwm4gt3",           37.5f, 82.0f  },
	{ "c6r",                40.5f, 82.0f  },
	{ "c8rvettegte",        48.0f, 78.0f  },
	{ "dallaraf3",          38.0f, 102.0f },
	{ "dallarair18",        44.0f, 110.0f },
	{ "dallarap217",        44.0f, 110.0f },
	{ "ferrari488gt3",      52.0f, 78.0f  },
	{ "ferrarievogt3",      54.0f, 80.0f  },
	{ "ferrari488gte",      44.0f, 82.0f  },
	{ "fordgt gt3",         52.0f, 78.0f  },
	{ "formulamazda",       34.5f, 96.0f  },
	{ "formularenault20",   34.5f, 96.0f  },
	{ "formularenault35",   44.0f, 110.0f },
	{ "formulavee",         23.0f, 68.0f  },
	{ "fr500s",             40.0f, 70.0f  },
	{ "hondacivictyper",    40.0f, 70.0f  },
	{ "hpdarx01c",          44.0f, 110.0f },
	{ "hyundaielantracn7",  40.0f, 70.0f  },
	{ "indypropm18",        34.5f, 100.0f },
	{ "lamborghinievogt3",  52.0f, 78.0f  },
	{ "lotus49",            23.8f, 70.0f  },
	{ "lotus79",            27.8f, 104.0f },
	{ "mclaren570sgt4",     40.0f, 70.0f  },
	{ "mclarenmp4",         52.0f, 78.0f  },
	{ "mclarenmp430",       38.0f, 110.0f },
	{ "mercedesamggt3",     37.5f, 82.0f  },
	{ "mercedesw12",        48.0f, 120.0f },
	{ "mx5 mx52016",        36.0f, 96.0f  },
	{ "nissangtpzxt",       44.0f, 110.0f },
	{ "porsche718gt4",      40.0f, 70.0f  },
	{ "porsche911cup",      46.0f, 88.0f  },
	{ "porsche992cup",      48.0f, 90.0f  },
	{ "porsche911rgt3",     52.0f, 80.0f  },
	{ "porsche991rsr",      42.0f, 72.0f  },
	{ "radical sr8",        40.0f, 100.0f },
	{ "rt2000",             25.0f, 86.0f  },
	{ "rufrt12r track",     46.0f, 88.0f  },
	{ "specracer",          25.0f, 86.0f  },
	{ "usf2000usf17",       34.5f, 96.0f  },
	{ "v8supercars fordmustanggt",  52.0f, 78.0f  },
	{ "v8supercars holden2019",     52.0f, 78.0f  },
	{ "williamsfw31",       38.0f, 110.0f },
	{ "dummy",              0.0f, 0.0f }
};

int force = 0;
volatile float suspForce = 0.0f;
volatile float yawForce[DIRECT_INTERP_SAMPLES];
__declspec(align(16)) volatile float suspForceST[DIRECT_INTERP_SAMPLES];
bool onTrack = false, stopped = true, deviceChangePending = false, logiWheel = false, sleepSpin = false;

volatile int ffbMag = 0;
volatile bool nearStops = false;

int numButtons = 0, numPov = 0, vjButtons = 0, vjPov = 0;
UINT samples, clippedSamples;

HANDLE wheelEvent = CreateEvent(nullptr, false, false, L"WheelEvent");
HANDLE ffbEvent = CreateEvent(nullptr, false, false, L"FFBEvent");

#define ID_SMOOTHPROGRESSCTRL	402

int progressClippingMax = 500;
HWND mainWnd, textWnd, statusWnd, overlayWnd, currentForceWnd, clippingForceWnd, currentForceOverlayWnd, clippingForceOverlayWnd, overlayMoveWnd;

LARGE_INTEGER freq;

int vjDev = 1;
FFB_DATA ffbPacket;
RTL_OSVERSIONINFOW winVer;

DWORD windowInitialExtendedState = NULL;


float* floatvarptr(const char* data, const char* name) {
	int idx = irsdk_varNameToIndex(name);
	if (idx >= 0)
		return (float*)(data + irsdk_getVarHeaderEntry(idx)->offset);
	else
		return nullptr;
}

int* intvarptr(const char* data, const char* name) {
	int idx = irsdk_varNameToIndex(name);
	if (idx >= 0)
		return (int*)(data + irsdk_getVarHeaderEntry(idx)->offset);
	else
		return nullptr;
}

bool* boolvarptr(const char* data, const char* name) {
	int idx = irsdk_varNameToIndex(name);
	if (idx >= 0)
		return (bool*)(data + irsdk_getVarHeaderEntry(idx)->offset);
	else
		return nullptr;
}

bool IsSavedDisplayActive()
{
	// Check if we have a monitor
	bool has = false;

	// Iterate over all displays and check if we have a valid one.
	//  If the device ID contains the string default_monitor no monitor is attached.
	DISPLAY_DEVICE dd;
	dd.cb = sizeof(dd);
	int deviceIndex = 0;
	while (EnumDisplayDevices(0, deviceIndex, &dd, 0))
	{
		std::wstring deviceName = dd.DeviceName;
		int monitorIndex = 0;
		while (EnumDisplayDevices(deviceName.c_str(), monitorIndex, &dd, 0))
		{
			size_t len = _tcslen(dd.DeviceID);
			for (size_t i = 0; i < len; ++i)
				dd.DeviceID[i] = _totlower(dd.DeviceID[i]);

			has = has || ((len > 10 && _tcsstr(dd.DeviceID, L"default_monitor") == nullptr) && dd.StateFlags & DISPLAY_DEVICE_ACTIVE);

			++monitorIndex;
		}
		++deviceIndex;
	}

	return has;
}


RTL_OSVERSIONINFOW GetRealOSVersion() {
	HMODULE hMod = ::GetModuleHandleW(L"ntdll.dll");
	if (hMod) {
		RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)::GetProcAddress(hMod, "RtlGetVersion");
		if (fxPtr != nullptr) {
			RTL_OSVERSIONINFOW rovi = { 0 };
			rovi.dwOSVersionInfoSize = sizeof(rovi);
			if (STATUS_SUCCESS == fxPtr(&rovi)) {
				return rovi;
			}
		}
	}
	RTL_OSVERSIONINFOW rovi = { 0 };
	return rovi;
}

// Thread that reads the wheel, writes to vJoy and updates the DI effect
DWORD WINAPI readWheelThread(LPVOID lParam) {

	UNREFERENCED_PARAMETER(lParam);

	HRESULT res;
	JOYSTICK_POSITION vjData;
	DWORD* hats[] = { &vjData.bHats, &vjData.bHatsEx1, &vjData.bHatsEx2, &vjData.bHatsEx3 };
	ResetVJD(vjDev);
	LONG lastX;
	LARGE_INTEGER lastTime, time, elapsed;
	float vel[DIRECT_INTERP_SAMPLES] = { 0.0f }, fd[4] = { 0.0f };
	int velIdx = 0, vi = 0, fdIdx = 0;
	float d = 0.0f;

	lastTime.QuadPart = 0;

	while (true) {

		DWORD signaled = WaitForSingleObject(wheelEvent, 1);

		if (!ffdevice)
			continue;

		if (signaled == WAIT_OBJECT_0) {

			res = ffdevice->GetDeviceState(sizeof(joyState), &joyState);
			if (res != DI_OK) {
				debug(L"GetDeviceState returned: 0x%x, requesting reacquire", res);
				reacquireDIDevice();
				continue;
			}

			vjData.wAxisX = joyState.lX;
			vjData.wAxisY = joyState.lY;
			vjData.wAxisZ = joyState.lZ;
			vjData.wAxisXRot = joyState.lRx;
			vjData.wAxisYRot = joyState.lRy;
			vjData.wAxisZRot = joyState.lRz;

			if (vjButtons > 0)
				for (int i = 0; i < numButtons; i++) {
					if (joyState.rgbButtons[i])
						vjData.lButtons |= 1 << i;
					else
						vjData.lButtons &= ~(1 << i);
				}

			// This could be wrong, untested..
			if (vjPov > 0)
				for (int i = 0; i < numPov && i < 4; i++)
					*hats[i] = joyState.rgdwPOV[i];

			UpdateVJD(vjDev, (PVOID)&vjData);

			if (effect == nullptr)
				continue;

			if (settings.getDampingFactor() != 0.0f || nearStops) {

				QueryPerformanceCounter(&time);

				if (lastTime.QuadPart != 0) {
					elapsed.QuadPart = (time.QuadPart - lastTime.QuadPart) * 1000000;
					elapsed.QuadPart /= freq.QuadPart;
					vel[velIdx] = (float)(joyState.lX - lastX) / elapsed.QuadPart;
				}

				lastTime.QuadPart = time.QuadPart;
				lastX = joyState.lX;

				vi = velIdx;

				if (++velIdx > DIRECT_INTERP_SAMPLES - 1)
					velIdx = 0;

				fd[fdIdx] = vel[vi++] * firc6[0];
				for (int i = 1; i < DIRECT_INTERP_SAMPLES; i++) {
					if (vi > DIRECT_INTERP_SAMPLES - 1)
						vi = 0;
					fd[fdIdx] += vel[vi++] * firc6[i];
				}

				if (++fdIdx > 3)
					fdIdx = 0;

				d = (fd[0] + fd[1] + fd[2] + fd[3]) / 4.0f;

				if (nearStops)
					d *= DAMPING_MULTIPLIER_STOPS;
				else
					d *= DAMPING_MULTIPLIER * settings.getDampingFactor();

			}
			else
				d = 0.0f;

		}

		pforce.lOffset = ffbMag;
		pforce.lOffset += (int)d;

		if (pforce.lOffset > DI_MAX)
			pforce.lOffset = DI_MAX;
		else if (pforce.lOffset < -DI_MAX)
			pforce.lOffset = -DI_MAX;

		EnterCriticalSection(&effectCrit);

		if (effect == nullptr) {
			LeaveCriticalSection(&effectCrit);
			continue;
		}

		HRESULT hr = effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_NORESTART);
		if (hr != DI_OK) {
			debug(L"SetParameters returned 0x%x, requesting reacquire", hr);
			reacquireDIDevice();
		}

		LeaveCriticalSection(&effectCrit);

	}

}

// Calculate FFB samples for the direct modes
DWORD WINAPI directFFBThread(LPVOID lParam) {

	UNREFERENCED_PARAMETER(lParam);
	int16_t mag;

	float s;
	int r;
	__declspec(align(16)) float prod[12];
	float lastSuspForce = 0, lastYawForce = 0;
	LARGE_INTEGER start;


	while (true) {

		bool use360 = settings.getUse360ForDirect();

		// Signalled when force has been updated
		WaitForSingleObject(ffbEvent, INFINITE);

		if (
			settings.getFfbType() != FFBTYPE_DIRECT_FILTER &&
			settings.getFfbType() != FFBTYPE_DIRECT_FILTER_720
			)
			continue;

		if (((ffbPacket.data[0] & 0xF0) >> 4) != vjDev)
			continue;

		mag = (ffbPacket.data[3] << 8) + ffbPacket.data[2];

		QueryPerformanceCounter(&start);

		// sign extend
		force = mag;

		s = (float)force;

		if (!use360)
			s += scaleTorque(suspForce);

		if (settings.getFfbType() == FFBTYPE_DIRECT_FILTER_720) {

			prod[0] = s * firc12[0];
			{
				__m128 mp0 = _mm_load_ps(prod);
				__m128 mp1 = _mm_load_ps(prod + 4);
				__m128 mp2 = _mm_load_ps(prod + 8);
				mp0 = _mm_add_ps(_mm_add_ps(mp0, mp1), mp2);
				mp0 = _mm_hadd_ps(mp0, mp0);
				mp0 = _mm_hadd_ps(mp0, mp0);
				r = _mm_cvttss_si32(mp0);
			}
			/*_asm {
				movaps xmm0, xmmword ptr prod
				movaps xmm1, xmmword ptr prod+16
				movaps xmm2, xmmword ptr prod+32
				addps xmm0, xmm1
				addps xmm0, xmm2
				haddps xmm0, xmm0
				haddps xmm0, xmm0
				cvttss2si eax, xmm0
				mov dword ptr r, eax
			}*/

			if (use360)
				r += scaleTorque(lastSuspForce + (suspForceST[0] - lastSuspForce) / 2.0f);

			r += scaleTorque(lastYawForce + (yawForce[0] - lastYawForce) / 2.0f);

			setFFB(r);

			for (int i = 1; i < DIRECT_INTERP_SAMPLES * 2 - 1; i++) {

				prod[i] = s * firc12[i];
				{
					__m128 mp0 = _mm_load_ps(prod);
					__m128 mp1 = _mm_load_ps(prod + 4);
					__m128 mp2 = _mm_load_ps(prod + 8);
					mp0 = _mm_add_ps(_mm_add_ps(mp0, mp1), mp2);
					mp0 = _mm_hadd_ps(mp0, mp0);
					mp0 = _mm_hadd_ps(mp0, mp0);
					r = _mm_cvttss_si32(mp0);
				}
				/*
				_asm {
					movaps xmm0, xmmword ptr prod
					movaps xmm1, xmmword ptr prod + 16
					movaps xmm2, xmmword ptr prod + 32
					addps xmm0, xmm1
					addps xmm0, xmm2
					haddps xmm0, xmm0
					haddps xmm0, xmm0
					cvttss2si eax, xmm0
					mov dword ptr r, eax
				}
				*/
				int idx = (i - 1) >> 1;
				bool odd = i & 1;

				if (use360)
					r +=
					scaleTorque(
						odd ?
						suspForceST[idx] :
						suspForceST[idx] + (suspForceST[idx + 1] - suspForceST[idx]) / 2.0f
					);

				r +=
					scaleTorque(
						odd ?
						yawForce[idx] :
						yawForce[idx] + (yawForce[idx + 1] - yawForce[idx]) / 2.0f
					);

				//nanosleep(1380 * i);

				sleepSpinUntil(&start, 1000, 1380 * i);
				setFFB(r);

			}

			prod[DIRECT_INTERP_SAMPLES * 2 - 1] = s * firc12[DIRECT_INTERP_SAMPLES * 2 - 1];
			__m128 mp0 = _mm_load_ps(prod);
			__m128 mp1 = _mm_load_ps(prod + 4);
			__m128 mp2 = _mm_load_ps(prod + 8);
			mp0 = _mm_add_ps(_mm_add_ps(mp0, mp1), mp2);
			mp0 = _mm_hadd_ps(mp0, mp0);
			mp0 = _mm_hadd_ps(mp0, mp0);
			r = _mm_cvttss_si32(mp0);
			/*_asm {
				movaps xmm0, xmmword ptr prod
				movaps xmm1, xmmword ptr prod + 16
				movaps xmm2, xmmword ptr prod + 32
				addps xmm0, xmm1
				addps xmm0, xmm2
				haddps xmm0, xmm0
				haddps xmm0, xmm0
				cvttss2si eax, xmm0
				mov dword ptr r, eax
			}*/

			if (use360)
				r += scaleTorque(suspForceST[DIRECT_INTERP_SAMPLES - 1]);

			r += scaleTorque(yawForce[DIRECT_INTERP_SAMPLES - 1]);

			//nanosleep(1380 * (DIRECT_INTERP_SAMPLES * 2 - 1));
			sleepSpinUntil(&start, 1000, 1380 * (DIRECT_INTERP_SAMPLES * 2 - 1));
			setFFB(r);

			lastSuspForce = suspForceST[DIRECT_INTERP_SAMPLES - 1];
			lastYawForce = yawForce[DIRECT_INTERP_SAMPLES - 1];

			continue;

		}

		prod[0] = s * firc6[0];
		r = (int)(prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5]) +
			scaleTorque(yawForce[0]);

		if (use360)
			r += scaleTorque(suspForceST[0]);

		setFFB(r);

		for (int i = 1; i < DIRECT_INTERP_SAMPLES; i++) {

			prod[i] = s * firc6[i];
			r = (int)(prod[0] + prod[1] + prod[2] + prod[3] + prod[4] + prod[5]) +
				scaleTorque(yawForce[i]);

			if (use360)
				r += scaleTorque(suspForceST[i]);

			sleepSpinUntil(&start, 2000, 2760 * i);
			//nanosleep(2760 * i);
			setFFB(r);
		}
	}

	return 0;

}

void resetForces() {
	debug(L"Resetting forces");
	suspForce = 0;
	for (int i = 0; i < DIRECT_INTERP_SAMPLES; i++) {
		suspForceST[i] = 0;
		yawForce[i] = 0;
	}
	force = 0;
	setFFB(0);
}

boolean getCarName() {

	char buf[64];
	const char* ptr;
	int len = -1, carIdx = -1;

	car[0] = 0;

	// Get car idx
	if (!parseYaml(irsdk_getSessionInfoStr(), "DriverInfo:DriverCarIdx:", &ptr, &len))
		return false;

	if (len < 0 || len > sizeof(buf) - 1)
		return false;

	memcpy(buf, ptr, len);
	buf[len] = 0;
	carIdx = atoi(buf);

	// Get car path
	sprintf_s(buf, "DriverInfo:Drivers:CarIdx:{%d}CarPath:", carIdx);
	if (!parseYaml(irsdk_getSessionInfoStr(), buf, &ptr, &len))
		return false;
	if (len < 0 || len > sizeof(car) - 1)
		return false;

	memcpy(car, ptr, len);
	car[len] = 0;

	return true;

}

float getCarRedline() {

	char buf[64];
	const char* ptr;
	int len = -1;

	if (parseYaml(irsdk_getSessionInfoStr(), "DriverInfo:DriverCarRedLine:", &ptr, &len)) {

		if (len < 0 || len > sizeof(buf) - 1)
			return 8000.0f;

		memcpy(buf, ptr, len);
		buf[len] = 0;
		return strtof(buf, NULL);
	}

	return 8000.0f;

}

bool getBuildInCarUsteerCoeffs(char* car) {

	if (settings.getUndersteerlatAccelDiv() > 0 && settings.getUndersteerYawRateMult() > 0)
	{
		return true;
	}
	for (int i = 0; i < sizeof(usteerCoefs) / sizeof(usteerCoefs[0]); i++)
		if (!strcmp(car, usteerCoefs[i].car))
		{
			debug(L"We have understeer coeffs for car %s", car);
			settings.setUndersteerYawRateMult(usteerCoefs[i].yawRateMult, (HWND)-1);
			settings.setUndersteerlatAccelDiv(usteerCoefs[i].latAccelDiv, (HWND)-1);
			return true;
		}

	debug(L"No understeer coeffs for car %s", car);
	return false;

}

void clippingReport() {

	float clippedPerCent = samples > 0 ? (float)clippedSamples * 100.0f / samples : 0.0f;
	text(L"%.02f%% of samples were clipped", clippedPerCent);
	if (clippedPerCent > 2.5f)
		text(L"Consider increasing max force to reduce clipping");
	samples = clippedSamples = 0;

}

void logiRpmLed(float* rpm, float redline) {

	logiLedData.rpmData.rpm = *rpm / (redline * 0.90f);
	logiLedData.rpmData.rpmFirstLed = 0.65f;
	logiLedData.rpmData.rpmRedLine = 1.0f;

	ffdevice->Escape(&logiEscape);

}

void deviceChange() {

	debug(L"Device change notification");
	if (!onTrack) {
		debug(L"Not on track, processing device change");
		deviceChangePending = false;
		enumDirectInput();
		if (!settings.isFfbDevicePresent())
			releaseDirectInput();
	}
	else {
		debug(L"Deferring device change processing whilst on track");
		deviceChangePending = true;
	}

}

DWORD getDeviceVidPid(LPDIRECTINPUTDEVICE8 dev) {

	DIPROPDWORD dipdw;
	dipdw.diph.dwSize = sizeof(DIPROPDWORD);
	dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	dipdw.diph.dwObj = 0;
	dipdw.diph.dwHow = DIPH_DEVICE;

	if (dev == nullptr)
		return 0;

	if (!SUCCEEDED(dev->GetProperty(DIPROP_VIDPID, &dipdw.diph)))
		return 0;

	return dipdw.dwData;

}

void minimise() {
	debug(L"Minimising window");
	Shell_NotifyIcon(NIM_ADD, &niData);
	ShowWindow(mainWnd, SW_HIDE);
}

void restore() {
	debug(L"Restoring window");
	Shell_NotifyIcon(NIM_DELETE, &niData);
	ShowWindow(mainWnd, SW_SHOW);
	BringWindowToTop(mainWnd);
	SetForegroundWindow(mainWnd);
}

int APIENTRY wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow
) {

	UNREFERENCED_PARAMETER(hPrevInstance);

	globalMutex = CreateMutex(NULL, false, L"Global\\irFFB_Mutex");

	if (GetLastError() == ERROR_ALREADY_EXISTS)
		exit(0);

	INITCOMMONCONTROLSEX ccEx;

	HANDLE handles[1];
	char* data = nullptr;
	bool irConnected = false;
	MSG msg;

	float* swTorque = nullptr, * swTorqueST = nullptr, * steer = nullptr, * steerMax = nullptr;
	float* speed = nullptr, * throttle = nullptr, * rpm = nullptr;
	float* LFshockDeflST = nullptr, * RFshockDeflST = nullptr, * CFshockDeflST = nullptr;
	float* LRshockDeflST = nullptr, * RRshockDeflST = nullptr;
	float* vX = nullptr, * vY = nullptr;
	float* latAccel = nullptr, * yawRate = nullptr;
	float LFshockDeflLast = -10000, RFshockDeflLast = -10000, CFshockDeflLast = -10000;
	float LRshockDeflLast = -10000, RRshockDeflLast = -10000;
	bool* isOnTrack = nullptr;
	int* trackSurface = nullptr, * gear = nullptr;

	bool inGarage = false;
	int numHandles = 0, dataLen = 0, lastGear = 0;
	int STnumSamples = 0, STmaxIdx = 0, lastTrackSurface = -1;
	float halfSteerMax = 0, lastTorque = 0, lastSuspForce = 0, redline;
	float yaw = 0.0f, yawFilter[DIRECT_INTERP_SAMPLES];

	ccEx.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
	ccEx.dwSize = sizeof(ccEx);
	InitCommonControlsEx(&ccEx);

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_IRFFB));
#ifdef _M_X64
	LoadStringW(hInstance, IDS_APP_TITLE_64, szTitle, MAX_LOADSTRING);
#else// _M_X64
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
#endif


	LoadStringW(hInstance, IDC_IRFFB, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Setup DI FFB effect
	pforce.dwMagnitude = 0;
	pforce.dwPeriod = INFINITE;
	pforce.dwPhase = 0;
	pforce.lOffset = 0;

	ZeroMemory(&dieff, sizeof(dieff));
	dieff.dwSize = sizeof(DIEFFECT);
	dieff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
	dieff.dwDuration = INFINITE;
	dieff.dwSamplePeriod = 0;
	dieff.dwGain = DI_FFNOMINALMAX;
	dieff.dwTriggerButton = DIEB_NOTRIGGER;
	dieff.dwTriggerRepeatInterval = 0;
	dieff.cAxes = 1;
	dieff.rgdwAxes = axes;
	dieff.rglDirection = dir;
	dieff.lpEnvelope = NULL;
	dieff.cbTypeSpecificParams = sizeof(DIPERIODIC);
	dieff.lpvTypeSpecificParams = &pforce;
	dieff.dwStartDelay = 0;

	ZeroMemory(&logiLedData, sizeof(logiLedData));
	logiLedData.size = sizeof(logiLedData);
	logiLedData.version = 1;
	ZeroMemory(&logiEscape, sizeof(logiEscape));
	logiEscape.dwSize = sizeof(DIEFFESCAPE);
	logiEscape.dwCommand = 0;
	logiEscape.lpvInBuffer = &logiLedData;
	logiEscape.cbInBuffer = sizeof(logiLedData);

	InitializeCriticalSection(&effectCrit);

	if (!InitInstance(hInstance, nCmdShow))
		return FALSE;

	memset(car, 0, sizeof(car));
	setCarStatus(car);
	setConnectedStatus(false);
	setOnTrackStatus(false);

	settings.readGenericSettings();
	settings.readRegSettings(car);
	// Hack! try set better sleep resulution with  undocumented NtSetTimerResolution, no error checking or nothing for now
	ULONG CurrentResolution = 0;
	ULONG MininumResolution = 0;
	ULONG MaximumResolution = 0;
	if (STATUS_SUCCESS == NtQueryTimerResolution(&MininumResolution, &MaximumResolution, &CurrentResolution))
	{
		NtSetTimerResolution(MaximumResolution, TRUE, &CurrentResolution);
	}
	else
	{
		text(L"Unable to set high resolution timer, reverting to old sleep routine");
	}

	sleepSpin = CurrentResolution == 0 || CurrentResolution > 10000;

	winVer = GetRealOSVersion();

	if (winVer.dwMajorVersion < 10 || (winVer.dwMajorVersion >= 10 && winVer.dwBuildNumber < 17134) || sleepSpin)
	{
		settings.setUseAltTimer(true);
		EnableWindow(settings.getAltTimerWnd(), FALSE);
	}
	if (settings.getShowForceOverlay())
	{
		SetLayeredWindowAttributes(overlayWnd, 0, settings.getOverlayTransparency(), LWA_ALPHA);
		SetWindowPos(overlayWnd, NULL, settings.getWindowPosX(), settings.getWindowPosY(), 200, OVERLAY_WINDOW_HEIGHT - 20, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);
		EnableWindow(overlayMoveWnd, TRUE);
	}
	if (RegisterHotKey(mainWnd, 1, MOD_ALT | MOD_CONTROL | MOD_NOREPEAT, VK_UP))
	{
		text(L"Max force+ Hotkey 'ALT + CONTROL + UP' registered");
	}
	if (RegisterHotKey(mainWnd, 2, MOD_ALT | MOD_CONTROL | MOD_NOREPEAT, VK_DOWN))
	{
		text(L"Max force- Hotkey 'ALT + CONTROL + DOWN' registered");
	}

	//int posX = settings.getWindowPosX();
	//int posY = settings.getWindowPosY();
	//RECT workArea;
	//SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
	//posX += workArea.left;
	//posY += workArea.top;

	// make sure the window is not completely out of sight
	/*int max_x = GetSystemMetrics(SM_CXSCREEN) -
		GetSystemMetrics(SM_CXICON);
	int max_y = GetSystemMetrics(SM_CYSCREEN) -
		GetSystemMetrics(SM_CYICON);

	posX = min(posX, max_x);
	posY = min(posY, max_y);

	SetWindowPos(mainWnd, nullptr, posX, posY, 864, 760, 0);*/

	if (settings.getStartMinimised())
		minimise();
	else
	{
		restore();
	}

	enumDirectInput();

	LARGE_INTEGER start;
	QueryPerformanceFrequency(&freq);

	initVJD();
	SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);

	SetThreadPriority(
		CreateThread(NULL, 0, readWheelThread, NULL, 0, NULL), THREAD_PRIORITY_HIGHEST
	);
	SetThreadPriority(
		CreateThread(NULL, 0, directFFBThread, NULL, 0, NULL), THREAD_PRIORITY_HIGHEST
	);

	debug(L"Init complete, entering mainloop");

	while (TRUE) {

		DWORD res;
		const irsdk_header* hdr = NULL;

		if (
			irsdk_startup() && (hdr = irsdk_getHeader()) &&
			hdr->status & irsdk_stConnected && hdr->bufLen != dataLen && hdr->bufLen != 0
			) {

			debug(L"New iRacing session");

			handles[0] = hDataValidEvent;
			numHandles = 1;

			if (data != NULL)
				free(data);

			dataLen = irsdk_getHeader()->bufLen;
			data = (char*)malloc(dataLen);
			setConnectedStatus(true);

			if (getCarName() && settings.getUseCarSpecific()) {
				setCarStatus(car);
				settings.readSettingsForCar(car);
			}
			else
				setCarStatus(nullptr);

			redline = getCarRedline();
			debug(L"Redline is %d rpm", (int)redline);

			getBuildInCarUsteerCoeffs(car);

			// Inform iRacing of the maxForce setting
			irsdk_broadcastMsg(irsdk_BroadcastFFBCommand, irsdk_FFBCommand_MaxForce, (float)settings.getMaxForce());

			swTorque = floatvarptr(data, "SteeringWheelTorque");
			swTorqueST = floatvarptr(data, "SteeringWheelTorque_ST");
			steer = floatvarptr(data, "SteeringWheelAngle");
			steerMax = floatvarptr(data, "SteeringWheelAngleMax");
			speed = floatvarptr(data, "Speed");
			throttle = floatvarptr(data, "Throttle");
			rpm = floatvarptr(data, "RPM");
			gear = intvarptr(data, "Gear");
			isOnTrack = boolvarptr(data, "IsOnTrack");

			trackSurface = intvarptr(data, "PlayerTrackSurface");
			vX = floatvarptr(data, "VelocityX");
			vY = floatvarptr(data, "VelocityY");

			latAccel = floatvarptr(data, "LatAccel");
			yawRate = floatvarptr(data, "YawRate");

			RFshockDeflST = floatvarptr(data, "RFshockDefl_ST");
			LFshockDeflST = floatvarptr(data, "LFshockDefl_ST");
			LRshockDeflST = floatvarptr(data, "LRshockDefl_ST");
			RRshockDeflST = floatvarptr(data, "RRshockDefl_ST");
			CFshockDeflST = floatvarptr(data, "CFshockDefl_ST");

			int swTorqueSTidx = irsdk_varNameToIndex("SteeringWheelTorque_ST");
			STnumSamples = irsdk_getVarHeaderEntry(swTorqueSTidx)->count;
			STmaxIdx = STnumSamples - 1;

			lastTorque = 0.0f;
			onTrack = false;
			resetForces();
			irConnected = true;
			timeBeginPeriod(1);

		}

		res = MsgWaitForMultipleObjects(numHandles, handles, FALSE, 1000, QS_ALLINPUT);

		QueryPerformanceCounter(&start);

		if (numHandles > 0 && res == numHandles - 1 && irsdk_getNewData(data)) {

			if (onTrack && !*isOnTrack) {
				debug(L"No longer on track");
				onTrack = false;
				setOnTrackStatus(onTrack);
				lastTorque = lastSuspForce = 0.0f;
				resetForces();
				clippingReport();
			}

			else if (!onTrack && *isOnTrack) {
				debug(L"Now on track");
				onTrack = true;
				setOnTrackStatus(onTrack);
				RFshockDeflLast = LFshockDeflLast =
					LRshockDeflLast = RRshockDeflLast =
					CFshockDeflLast = -10000.0f;
				clippedSamples = samples = lastGear = 0;
				memset(yawFilter, 0, DIRECT_INTERP_SAMPLES * sizeof(float));
				irsdk_broadcastMsg(irsdk_BroadcastFFBCommand, irsdk_FFBCommand_MaxForce, (float)settings.getMaxForce());
			}

			if (*trackSurface != lastTrackSurface) {
				debug(L"Track surface is now: %d", *trackSurface);
				lastTrackSurface = *trackSurface;
			}

			if (ffdevice && logiWheel)
				logiRpmLed(rpm, redline);

			yaw = 0.0f;

			if (*speed > 2.0f) {

				float bumpsFactor = settings.getBumpsFactor();
				float sopFactor = settings.getSopFactor();
				float sopOffset = settings.getSopOffset();

				bool use360 = settings.getUse360ForDirect();
				int ffbType = settings.getFfbType();

				if (*speed > 5.0f) {

					float halfMaxForce = (float)(settings.getMaxForce() >> 1);
					float r = *vY / *vX;
					float sa, asa, ar = abs(r);
					float reqSteer, uSteer;

					if (*vX < 0.0f)
						r = -r;

					if (ar > 1.0f) {
						sa = csignf(0.785f, r);
						asa = 0.785f;
						yaw = minf(maxf(sa * sopFactor, -halfMaxForce), halfMaxForce);
					}
					else {
						sa = 0.78539816339745f * r + 0.273f * r * (1.0f - ar);
						asa = abs(sa);
						if (asa > sopOffset) {
							sa -= csignf(sopOffset, sa);
							yaw =
								minf(
									maxf(
										sa * (2.0f - asa) * sopFactor,
										-halfMaxForce
									),
									halfMaxForce
								);
						}
					}

					if (settings.getUndersteerFactor() > 0.0f && settings.getUndersteerYawRateMult() > 0 && settings.getUndersteerlatAccelDiv() > 0) {

						reqSteer = abs((*yawRate * settings.getUndersteerYawRateMult()) / *speed + *latAccel / settings.getUndersteerlatAccelDiv());
						uSteer = minf(abs(*steer) - reqSteer - USTEER_MIN_OFFSET - settings.getUndersteerOffset(), 1.0f);

						if (uSteer > 0.0f)
							yaw -= uSteer * settings.getUndersteerFactor() * USTEER_MULTIPLIER * *swTorque;

					}

				}

				if (
					LFshockDeflST != nullptr && RFshockDeflST != nullptr && bumpsFactor != 0.0f
					) {

					if (LFshockDeflLast != -10000.0f) {

						if (ffbType != FFBTYPE_DIRECT_FILTER || use360) {

							__m128 xmm0 = _mm_loadu_ps(LFshockDeflST);
							__m128 xmm1 = _mm_loadu_ps(RFshockDeflST);
							__m128 xmm2 = _mm_load_ps(LFshockDeflST);
							__m128 xmm3 = _mm_load_ps(RFshockDeflST);
							xmm0 = (__m128)_mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(xmm0), 4));
							__m128 xmm4 = _mm_load_ss(&LFshockDeflLast);
							xmm1 = (__m128)_mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(xmm1), 4));
							__m128 xmm5 = _mm_load_ss(&RFshockDeflLast);
							xmm0 = _mm_move_ss(xmm0, xmm4);
							xmm2 = _mm_sub_ps(xmm2, xmm0);
							xmm1 = _mm_move_ss(xmm1, xmm5);
							xmm3 = _mm_sub_ps(xmm3, xmm1);
							xmm4 = _mm_loadu_ps((float*)LFshockDeflST + 12);
							xmm1 = _mm_loadl_pi(xmm1, (const __m64*)LFshockDeflST + 16);
							xmm1 = _mm_sub_ps(xmm1, xmm4);
							xmm5 = _mm_loadu_ps((float*)RFshockDeflST + 12);
							xmm0 = _mm_loadl_pi(xmm0, (const __m64*)RFshockDeflST + 16);
							xmm0 = _mm_sub_ps(xmm0, xmm4);
							xmm3 = _mm_load_ss(&bumpsFactor);
							xmm1 = _mm_sub_ps(xmm1, xmm0);
							xmm3 = _mm_unpacklo_ps(xmm3, xmm3);
							xmm3 = _mm_unpacklo_ps(xmm3, xmm3);
							xmm2 = _mm_mul_ps(xmm2, xmm3);
							xmm1 = _mm_mul_ps(xmm1, xmm3);
							_mm_store_ps((float*)suspForceST, xmm2);
							_mm_storel_pi((__m64*)suspForceST + 16, xmm1);
						}
						else  if (bumpsFactor != 0.0f) {
							suspForce =
								(
									(LFshockDeflST[STmaxIdx] - LFshockDeflLast) -
									(RFshockDeflST[STmaxIdx] - RFshockDeflLast)
									) * bumpsFactor * 0.25f;
						}
						else {
							suspForce =
							(
								(LFshockDeflST[STmaxIdx] - LFshockDeflLast) -
								(RFshockDeflST[STmaxIdx] - RFshockDeflLast)
								);
						}

					}

					RFshockDeflLast = RFshockDeflST[STmaxIdx];
					LFshockDeflLast = LFshockDeflST[STmaxIdx];

				}
				else if (CFshockDeflST != nullptr && bumpsFactor != 0.0f) {

					if (CFshockDeflLast != -10000.0f) {

						if (ffbType != FFBTYPE_DIRECT_FILTER || use360){

							__m128 xmm0 = _mm_loadu_ps(CFshockDeflST);
							__m128 xmm2 = _mm_load_ps(CFshockDeflST);
							__m128 xmm3 = _mm_load_ss(&bumpsFactor);
							xmm0 = (__m128)_mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(xmm0), 4));
							__m128 xmm4 = _mm_load_ss(&CFshockDeflLast);
							xmm3 = _mm_unpacklo_ps(xmm3, xmm3);
							xmm0 = _mm_move_ss(xmm0, xmm4);
							xmm2 = _mm_sub_ps(xmm2, xmm0);
							xmm4 = _mm_loadu_ps((float*)CFshockDeflST + 12);
							__m128 xmm1 = _mm_loadl_pi(xmm1, (const __m64*)CFshockDeflST + 16);
							xmm3 = _mm_unpacklo_ps(xmm3, xmm3);
							xmm1 = _mm_sub_ps(xmm1, xmm4);
							xmm2 = _mm_mul_ps(xmm2, xmm3);
							xmm1 = _mm_mul_ps(xmm1, xmm3);
							_mm_store_ps((float*)suspForceST, xmm2);
							_mm_storel_pi((__m64*)suspForceST + 16, xmm1);
						}
						else if(bumpsFactor != 0.0f)
							suspForce = (CFshockDeflST[STmaxIdx] - CFshockDeflLast) * bumpsFactor * 0.25f;
						else
							CFshockDeflLast = CFshockDeflST[STmaxIdx];

					}

					CFshockDeflLast = CFshockDeflST[STmaxIdx];

				}

				stopped = false;

			}
			else
				stopped = true;

			for (int i = 0; i < DIRECT_INTERP_SAMPLES; i++) {

				yawFilter[i] = yaw * firc6[i];

				yawForce[i] =
					yawFilter[0] + yawFilter[1] + yawFilter[2] +
					yawFilter[3] + yawFilter[4] + yawFilter[5];

			}

			halfSteerMax = *steerMax / 2.0f;
			if (abs(halfSteerMax) < 8.0f && abs(*steer) > halfSteerMax - STOPS_MAXFORCE_RAD * 2.0f)
				nearStops = true;
			else
				nearStops = false;

			if (
				!*isOnTrack ||
				settings.getFfbType() == FFBTYPE_DIRECT_FILTER ||
				settings.getFfbType() == FFBTYPE_DIRECT_FILTER_720
				)
				continue;

			// Bump stops
			if (abs(halfSteerMax) < 8.0f && abs(*steer) > halfSteerMax) {

				float factor, invFactor;

				if (*steer > 0) {
					factor = (-(*steer - halfSteerMax)) / STOPS_MAXFORCE_RAD;
					factor = maxf(factor, -1.0f);
					invFactor = 1.0f + factor;
				}
				else {
					factor = (-(*steer + halfSteerMax)) / STOPS_MAXFORCE_RAD;
					factor = minf(factor, 1.0f);
					invFactor = 1.0f - factor;
				}

				setFFB((int)(factor * DI_MAX + scaleTorque(*swTorque) * invFactor));
				continue;

			}

			// Telemetry FFB
			switch (settings.getFfbType()) {

			case FFBTYPE_360HZ: {

				for (int i = 0; i < STmaxIdx; i++) {
					setFFB(scaleTorque(swTorqueST[i] + suspForceST[i] + yawForce[i]));
					sleepSpinUntil(&start, 2000, 2760 * (i + 1));
					//nanosleep(2760 * (i + 1));
				}
				setFFB(
					scaleTorque(
						swTorqueST[STmaxIdx] + suspForceST[STmaxIdx] + yawForce[STmaxIdx]
					)
				);

			}
							  break;

			case FFBTYPE_360HZ_INTERP: {

				float diff = (swTorqueST[0] - lastTorque) / 2.0f;
				float sdiff = (suspForceST[0] - lastSuspForce) / 2.0f;
				int force, iMax = STmaxIdx << 1;

				setFFB(
					scaleTorque(
						lastTorque + diff + lastSuspForce + sdiff + yawForce[0]
					)
				);

				for (int i = 0; i < iMax; i++) {

					int idx = i >> 1;

					if (i & 1) {
						diff = (swTorqueST[idx + 1] - swTorqueST[idx]) / 2.0f;
						sdiff = (suspForceST[idx + 1] - suspForceST[idx]) / 2.0f;
						force =
							scaleTorque(
								swTorqueST[idx] + diff + suspForceST[idx] +
								sdiff + yawForce[idx]
							);
					}
					else
						force =
						scaleTorque(
							swTorqueST[idx] + suspForceST[idx] + yawForce[idx]
						);

					//nanosleep(1380 * (i + 1));
					sleepSpinUntil(&start, 1000, 1380 * (i + 1));
					setFFB(force);

				}

				//nanosleep(1380 * (iMax + 1));
				sleepSpinUntil(&start, 1000, 1380 * (iMax + 1));
				setFFB(
					scaleTorque(
						swTorqueST[STmaxIdx] + suspForceST[STmaxIdx] + yawForce[STmaxIdx]
					)
				);
				lastTorque = swTorqueST[STmaxIdx];
				lastSuspForce = suspForceST[STmaxIdx];

			}
									 break;

			}


		}

		// Did we lose iRacing?
		if (numHandles > 0 && !(hdr->status & irsdk_stConnected)) {
			debug(L"Disconnected from iRacing");
			numHandles = 0;
			dataLen = 0;
			if (data != NULL) {
				free(data);
				data = NULL;
			}
			resetForces();
			onTrack = false;
			setOnTrackStatus(onTrack);
			setConnectedStatus(false);
			timeEndPeriod(1);
			if (settings.getUseCarSpecific() && car[0] != 0)
				settings.writeSettingsForCar(car);
		}

		// Window messages
		if (res == numHandles) {

			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT)
					DestroyWindow(mainWnd);
				if (!IsDialogMessage(mainWnd, &msg)) {
					if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
						TranslateMessage(&msg);
						DispatchMessage(&msg);
					}
				}
			}

		}

	}

	return (int)msg.wParam;

}

ATOM MyRegisterClass(HINSTANCE hInstance) {

	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_IRFFB));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_IRFFB);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);

}

LRESULT CALLBACK EditWndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subId, DWORD_PTR rData) {

	if (msg == WM_CHAR) {

		wchar_t buf[8];

		if (subId == EDIT_FLOAT) {

			if (GetWindowTextW(wnd, buf, 8) && StrChrIW(buf, L'.') && wParam == '.')
				return 0;

			if (
				!(
					(wParam >= L'0' && wParam <= L'9') ||
					wParam == L'.' ||
					wParam == VK_RETURN ||
					wParam == VK_DELETE ||
					wParam == VK_BACK
					)
				)
				return 0;

			LRESULT ret = DefSubclassProc(wnd, msg, wParam, lParam);

			wchar_t* end;
			float val = 0.0f;

			GetWindowText(wnd, buf, 8);
			val = wcstof(buf, &end);
			if (end - buf == wcslen(buf))
				SendMessage(GetParent(wnd), WM_EDIT_VALUE, reinterpret_cast<WPARAM&>(val), (LPARAM)wnd);

			return ret;

		}
		else {

			if (
				!(
					(wParam >= L'0' && wParam <= L'9') ||
					wParam == VK_RETURN ||
					wParam == VK_DELETE ||
					wParam == VK_BACK
					)
				)
				return 0;

			LRESULT ret = DefSubclassProc(wnd, msg, wParam, lParam);
			GetWindowText(wnd, buf, 8);
			int val = _wtoi(buf);
			SendMessage(GetParent(wnd), WM_EDIT_VALUE, (WPARAM)val, (LPARAM)wnd);
			return ret;

		}

	}

	return DefSubclassProc(wnd, msg, wParam, lParam);

}
bool CALLBACK SetFont(HWND child, LPARAM font) {
	SendMessage(child, WM_SETFONT, font, true);
	return true;
}


HWND combo(HWND parent, wchar_t* name, int x, int y) {

	CreateWindowW(
		L"STATIC", name,
		WS_CHILD | WS_VISIBLE,
		x, y, 300, 20, parent, NULL, hInst, NULL
	);
	return
		CreateWindow(
			L"COMBOBOX", nullptr,
			CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_OVERLAPPED | WS_TABSTOP,
			x, y + 20, 265, 240, parent, nullptr, hInst, nullptr
		);

}

sWins_t* slider(HWND parent, wchar_t* name, int x, int y, wchar_t* start, wchar_t* end, bool floatData) {

	sWins_t* wins = (sWins_t*)malloc(sizeof(sWins_t));

	wins->label = CreateWindowW(
		L"STATIC", name,
		WS_CHILD | WS_VISIBLE,
		x, y, 100, 14, parent, NULL, hInst, NULL
	);

	wins->value = CreateWindowW(
		L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_CENTER,
		x + 220, y, 45, 18, parent, NULL, hInst, NULL
	);

	SetWindowSubclass(wins->value, EditWndProc, floatData ? 1 : 0, 0);

	SendMessage(wins->value, EM_SETLIMITTEXT, 5, 0);

	wins->trackbar = CreateWindowExW(
		0, TRACKBAR_CLASS, name,
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_TOOLTIPS | TBS_TRANSPARENTBKGND,
		x + 40, y + 18, 170, 20,
		parent, NULL, hInst, NULL
	);

	wins->min = std::stof(start);
	wins->max = std::stof(end);

	SendMessage(wins->trackbar, TBM_SETRANGE, (WPARAM)TRUE, MAKELONG(std::stof(start), std::stof(end)));

	HWND buddyLeft = CreateWindowEx(
		0, L"STATIC", start,
		SS_LEFT | WS_CHILD | WS_VISIBLE,
		0, 0, 40, 15, parent, NULL, hInst, NULL
	);
	SendMessage(wins->trackbar, TBM_SETBUDDY, (WPARAM)TRUE, (LPARAM)buddyLeft);

	HWND buddyRight = CreateWindowEx(
		0, L"STATIC", end,
		SS_RIGHT | WS_CHILD | WS_VISIBLE,
		0, 0, 52, 15, parent, NULL, hInst, NULL
	);
	SendMessage(wins->trackbar, TBM_SETBUDDY, (WPARAM)FALSE, (LPARAM)buddyRight);

	return wins;

}

HWND slider(HWND parent, wchar_t* name, int x, int y, int width, int height, int start, int end) {

	HWND slider = CreateWindowExW(
		0, TRACKBAR_CLASS, name,
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_TOOLTIPS | TBS_TRANSPARENTBKGND | TBS_NOTICKS,
		x, y, width, height,
		parent, NULL, hInst, NULL
	);

	SendMessage(slider, TBM_SETRANGE, (WPARAM)TRUE, MAKELONG(start, end));
	return slider;

}

HWND checkbox(HWND parent, wchar_t* name, int x, int y, int width = 360, int height = 20) {

	HWND whd = CreateWindowEx(
		0, L"BUTTON", name,
		BS_CHECKBOX | BS_MULTILINE | WS_CHILD | WS_TABSTOP | WS_VISIBLE,
		x, y, width, height, parent, nullptr, hInst, nullptr
	);

	return whd;
}

HWND groupox(HWND parent, wchar_t* name, int x, int y, int width, int height) {

	return
		CreateWindowEx(
			0, L"BUTTON", name,
			BS_GROUPBOX | BS_MULTILINE | WS_CHILD | WS_TABSTOP | WS_VISIBLE,
			x, y, width, height, parent, nullptr, hInst, nullptr
		);

}

HWND progressbar(HWND parent, wchar_t* name, int x, int y, int width, int height, int max)
{

	if (name != nullptr)
		CreateWindowW(
			L"STATIC", name,
			WS_CHILD | WS_VISIBLE,
			x, y, width, 20, parent, NULL, hInst, NULL
		);

	HWND hWnd = ::CreateWindowEx(
		0,
		PROGRESS_CLASS,
		name,
		WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
		x,
		name == nullptr ? y : y + 20,
		width,
		height,
		parent,
		(HMENU)ID_SMOOTHPROGRESSCTRL,
		hInst,
		NULL);
	::SendMessage(hWnd, PBM_SETRANGE32, 0, (WPARAM)(INT)max);

	return hWnd;
}

void ActivateOverLayWindow()
{
	if (windowInitialExtendedState == NULL)
	{
		windowInitialExtendedState = GetWindowLongPtr(overlayWnd, GWL_EXSTYLE);
	}
	SetWindowPos(overlayWnd, NULL, settings.getWindowPosX(), settings.getWindowPosY(), 200, OVERLAY_WINDOW_HEIGHT, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);
	ShowWindow(settings.getOverlayMaxForceWnd(), SW_SHOW);
	SetWindowLongPtr(overlayWnd, GWL_EXSTYLE, windowInitialExtendedState ^ WS_EX_LAYERED);
}

void DeActivateOverlayWindow()
{
	if (windowInitialExtendedState == NULL)
	{
		windowInitialExtendedState = GetWindowLongPtr(overlayWnd, (int)GWL_EXSTYLE);
	}
	SetWindowLongPtr(overlayWnd, GWL_EXSTYLE, windowInitialExtendedState);
	ShowWindow(settings.getOverlayMaxForceWnd(), SW_HIDE);
	SetWindowPos(overlayWnd, NULL, settings.getWindowPosX(), settings.getWindowPosY(), 200, OVERLAY_WINDOW_HEIGHT - 20, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);
	SetLayeredWindowAttributes(overlayWnd, 0, settings.getOverlayTransparency(), LWA_ALPHA);
}

void CreateOverlayWindow(HINSTANCE hInstance)
{

	DWORD extendedStyle = WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
	overlayWnd = CreateWindowExW(extendedStyle,
		szWindowClass, L"irFFb Overlay",
		WS_VISIBLE | WS_POPUP,
		//WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME,
		CW_USEDEFAULT, CW_USEDEFAULT, 200, OVERLAY_WINDOW_HEIGHT,
		NULL, NULL, hInst, NULL
	);

	currentForceOverlayWnd = progressbar(overlayWnd, nullptr, 3, 3, 150, 15, IR_MAX);
	clippingForceOverlayWnd = progressbar(overlayWnd, nullptr, 150, 3, 46, 15, progressClippingMax);

	settings.setOverlayMaxForceWnd(slider(overlayWnd, L"max force", 4, 22, 192, 15, 5, 65));
	SendMessage(clippingForceOverlayWnd, PBM_SETSTATE, PBST_ERROR, 0);

	SetWindowSubclass(currentForceOverlayWnd, myNcHitTest, 0, 0);
	SetWindowSubclass(clippingForceOverlayWnd, myNcHitTest, 0, 0);
	SetWindowSubclass(overlayWnd, myPaint, 0, 0);

	SetLayeredWindowAttributes(overlayWnd, 0, 255, LWA_ALPHA);
	SetMenu(overlayWnd, NULL);
	ShowWindow(overlayWnd, SW_HIDE);
	UpdateWindow(overlayWnd);
}


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {

	DEV_BROADCAST_DEVICEINTERFACE devFilter;

	hInst = hInstance;

	mainWnd = CreateWindowW(
		szWindowClass, szTitle,
		WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME,
		CW_USEDEFAULT, CW_USEDEFAULT, 694, 640,
		NULL, NULL, hInst, NULL
	);

	if (!mainWnd)
		return FALSE;


	memset(&niData, 0, sizeof(niData));
	niData.uVersion = NOTIFYICON_VERSION;
	niData.cbSize = NOTIFYICONDATA_V1_SIZE;
	niData.hWnd = mainWnd;
	niData.uID = 1;
	niData.uFlags = NIF_ICON | NIF_MESSAGE;
	niData.uCallbackMessage = WM_TRAY_ICON;
	niData.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_SMALL));

	//left side UI
	settings.setDevWnd(combo(mainWnd, L"FFB device:", 44, 20));
	settings.setFfbWnd(combo(mainWnd, L"FFB type:", 44, 70));
	settings.setMinWnd(slider(mainWnd, L"Min force:", 44, 130, L"0", L"20", false));
	settings.setMaxWnd(slider(mainWnd, L"Max force:", 44, 170, L"5 Nm", L"65 Nm", false));
	settings.setDampingWnd(slider(mainWnd, L"Damping:", 44, 210, L"0", L"100", true));

	groupox(mainWnd, L"Overlay:", 32, 250, 295, 110);
	settings.setForceOverlayWnd(checkbox(mainWnd, L"Show overlay?", 44, 270, 180));
	overlayMoveWnd = checkbox(mainWnd, L"Enable input?", 44, 290, 180);
	EnableWindow(overlayMoveWnd, FALSE);

	settings.setOverlayTransparencyWnd(slider(mainWnd, L"Overlay transparency:", 44, 310, L"0", L"255", false));

	statusWnd = CreateWindowEx(
		0, STATUSCLASSNAME, NULL,
		WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, mainWnd, NULL, hInst, NULL
	);

	textWnd = CreateWindowEx(
		WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_VISIBLE | WS_VSCROLL | WS_CHILD | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
		32, 370, 295, 120,
		mainWnd, NULL, hInst, NULL
	);
	SendMessage(textWnd, EM_SETLIMITTEXT, WPARAM(256000), 0);
	//right side UI 
	auto groupoxWnd = groupox(mainWnd, L"Understeer Effect:", 344, 20, 295, 210);
	settings.setUndersteerWnd(slider(mainWnd, L"Understeer:", 364, 50, L"0", L"100", true));
	settings.setUndersteerOffsetWnd(slider(mainWnd, L"Offset:", 364, 90, L"0", L"100", true));
	settings.setUndersteerYawRateMultWnd(slider(mainWnd, L"Force multiplier:", 364, 130, L"0", L"60", true));
	settings.setUndersteerlatAccelDivWnd(slider(mainWnd, L"Release force:", 364, 170, L"60", L"130", true));

	settings.setBumpsWnd(slider(mainWnd, L"Suspension bumps:", 364, 240, L"0", L"100", true));
	settings.setSopWnd(slider(mainWnd, L"SoP effect:", 364, 280, L"0", L"100", true));
	settings.setSopOffsetWnd(slider(mainWnd, L"SoP offset:", 364, 320, L"0", L"100", true));
	settings.setUse360Wnd(checkbox(mainWnd, L"Use 360 Hz telemetry for suspension effects\r\n in direct modes?", 360, 360, 300, 38));

	settings.setCarSpecificWnd(
		checkbox(mainWnd, L"Use car specific settings?", 360, 395)
	);
	settings.setReduceWhenParkedWnd(
		checkbox(mainWnd, L"Reduce force when parked?", 360, 415)
	);
	settings.setRunOnStartupWnd(
		checkbox(mainWnd, L"Run on startup?", 360, 435)
	);
	settings.setStartMinimisedWnd(
		checkbox(mainWnd, L"Start minimised?", 360, 455)
	);
	settings.setDebugWnd(
		checkbox(mainWnd, L"Debug logging?", 360, 475)
	);

	//spanning UI
	currentForceWnd = progressbar(mainWnd, L"Current applied force", 32, 500, 480, 20, IR_MAX);
	clippingForceWnd = progressbar(mainWnd, L"Clipping force", 480, 500, 160, 20, progressClippingMax);

	SendMessage(clippingForceWnd, PBM_SETSTATE, PBST_ERROR, 0);

	int statusParts[] = { 256, 424, 864 };
	SendMessage(statusWnd, SB_SETPARTS, 3, LPARAM(statusParts));

	BOOL value = TRUE;
	DwmSetWindowAttribute(mainWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));

	ShowWindow(mainWnd, SW_HIDE);
	UpdateWindow(mainWnd);

	memset(&devFilter, 0, sizeof(devFilter));
	devFilter.dbcc_classguid = GUID_DEVINTERFACE_HID;
	devFilter.dbcc_size = sizeof(devFilter);
	devFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	RegisterDeviceNotificationW(mainWnd, &devFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

	CreateOverlayWindow(hInstance);

	HFONT hFont = CreateFont(14, 0, 0, 0, FW_DONTCARE | FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
		OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, TEXT("Microsoft Sans Serif"));

	EnumChildWindows(mainWnd, (WNDENUMPROC)SetFont, (LPARAM)hFont);

	return TRUE;

}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {

	HWND wnd = (HWND)lParam;

	switch (message) {

	case WM_COMMAND: {

		int wmId = LOWORD(wParam);
		switch (wmId) {

		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				if (wnd == settings.getDevWnd()) {
					GUID oldDevice = settings.getFfbDevice();
					DWORD vidpid = 0;
					if (oldDevice != GUID_NULL)
						vidpid = getDeviceVidPid(ffdevice);
					settings.setFfbDevice(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0));
				}
				else if (wnd == settings.getFfbWnd())
					settings.setFfbType(SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0));
			}
			else if (HIWORD(wParam) == BN_CLICKED) {
				bool oldValue = SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;
				if (wnd == settings.getUse360Wnd())
					settings.setUse360ForDirect(!oldValue);
				else if (wnd == settings.getCarSpecificWnd()) {
					if (!oldValue)
						getCarName();
					settings.setUseCarSpecific(!oldValue, car);
				}
				else if (wnd == settings.getReduceWhenParkedWnd())
					settings.setReduceWhenParked(!oldValue);
				else if (wnd == settings.getRunOnStartupWnd())
					settings.setRunOnStartup(!oldValue);
				else if (wnd == settings.getStartMinimisedWnd())
					settings.setStartMinimised(!oldValue);
				else if (wnd == settings.getForceOverlayWnd())
				{
					settings.setShowForceOverlay(!oldValue);
					if (!oldValue)
					{
						EnableWindow(overlayMoveWnd, TRUE);
						ShowWindow(overlayWnd, SW_SHOW);
					}
					else
					{
						EnableWindow(overlayMoveWnd, FALSE);
						ShowWindow(overlayWnd, SW_HIDE);
					}
				}
				else if (wnd == overlayMoveWnd)
				{
					if (!oldValue)
						ActivateOverLayWindow();
					else
						DeActivateOverlayWindow();
					SendMessage(overlayMoveWnd, BM_SETCHECK, !oldValue ? BST_CHECKED : BST_UNCHECKED, NULL);

				}


				else if (wnd == settings.getDebugWnd()) {
					settings.setDebug(!oldValue);
					if (settings.getDebug()) {
						debugHnd = CreateFileW(settings.getLogPath().c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
						int chars = SendMessageW(textWnd, WM_GETTEXTLENGTH, 0, 0);
						wchar_t* buf = new wchar_t[chars + 1], * str = buf;
						SendMessageW(textWnd, WM_GETTEXT, chars + 1, (LPARAM)buf);
						wchar_t* end = StrStrW(str, L"\r\n");
						while (end) {
							*end = '\0';
							debug(str);
							str = end + 2;
							end = StrStrW(str, L"\r\n");
						}
						delete[] buf;
					}
					else if (debugHnd != INVALID_HANDLE_VALUE) {
						CloseHandle(debugHnd);
						debugHnd = INVALID_HANDLE_VALUE;
					}
				}

			}
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
				   break;

	case WM_EDIT_VALUE: {
		if (wnd == settings.getMaxWnd()->value)
			settings.setMaxForce(wParam, wnd);
		else if (wnd == settings.getMinWnd()->value)
			settings.setMinForce(wParam, wnd);
		else if (wnd == settings.getBumpsWnd()->value)
			settings.setBumpsFactor(reinterpret_cast<float&>(wParam), wnd);
		else if (wnd == settings.getDampingWnd()->value)
			settings.setDampingFactor(reinterpret_cast<float&>(wParam), wnd);
		else if (wnd == settings.getSopWnd()->value)
			settings.setSopFactor(reinterpret_cast<float&>(wParam), wnd);
		else if (wnd == settings.getSopOffsetWnd()->value)
			settings.setSopOffset(reinterpret_cast<float&>(wParam), wnd);
		else if (wnd == settings.getUndersteerYawRateMultWnd()->value)
			settings.setUndersteerYawRateMult(reinterpret_cast<float&>(wParam), wnd);
		else if (wnd == settings.getUndersteerlatAccelDivWnd()->value)
			settings.setUndersteerlatAccelDiv(reinterpret_cast<float&>(wParam), wnd);
		else if (wnd == settings.getOverlayTransparencyWnd()->value)
		{
			settings.setOverlayTransparency(reinterpret_cast<float&>(wParam), wnd);
			SetLayeredWindowAttributes(overlayWnd, 0, settings.getOverlayTransparency(), LWA_ALPHA);
		}

	}
					  break;


	case WM_HSCROLL: {
		if (wnd == settings.getMaxWnd()->trackbar || wnd == settings.getOverlayMaxForceWnd())
			settings.setMaxForce(SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getMinWnd()->trackbar)
			settings.setMinForce(SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getBumpsWnd()->trackbar)
			settings.setBumpsFactor((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getDampingWnd()->trackbar)
			settings.setDampingFactor((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getSopWnd()->trackbar)
			settings.setSopFactor((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getSopOffsetWnd()->trackbar)
			settings.setSopOffset((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getUndersteerWnd()->trackbar)
			settings.setUndersteerFactor((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getUndersteerOffsetWnd()->trackbar)
			settings.setUndersteerOffset((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getUndersteerYawRateMultWnd()->trackbar)
			settings.setUndersteerYawRateMult((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getUndersteerlatAccelDivWnd()->trackbar)
			settings.setUndersteerlatAccelDiv((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
		else if (wnd == settings.getOverlayTransparencyWnd()->trackbar)
		{
			settings.setOverlayTransparency((float)SendMessage(wnd, TBM_GETPOS, 0, 0), wnd);
			SetLayeredWindowAttributes(overlayWnd, 0, settings.getOverlayTransparency(), LWA_ALPHA);
		}

	}
				   break;

	case WM_CTLCOLORSTATIC: {
		SetBkColor((HDC)wParam, RGB(0xff, 0xff, 0xff));
		return (LRESULT)CreateSolidBrush(RGB(0xff, 0xff, 0xff));
	}
						  break;

	case WM_PRINTCLIENT: {
		RECT r = { 0 };
		GetClientRect(hWnd, &r);
		FillRect((HDC)wParam, &r, CreateSolidBrush(RGB(0xff, 0xff, 0xff)));
	}
					   break;

	case WM_SIZE: {
		SendMessage(statusWnd, WM_SIZE, wParam, lParam);
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
				break;

	case WM_POWERBROADCAST: {
		int wmId = LOWORD(wParam);
		switch (wmId) {
		case PBT_APMSUSPEND:
			debug(L"Computer is suspending, release all");
			releaseAll();
			break;
		case PBT_APMRESUMESUSPEND:
			debug(L"Computer is resuming, init all");
			initAll();
			break;
		}
	}
						  break;

	case WM_TRAY_ICON: {
		switch (lParam) {
		case WM_LBUTTONUP:
			restore();
			break;
		case WM_RBUTTONUP: {
			HMENU trayMenu = CreatePopupMenu();
			POINT curPoint;
			AppendMenuW(trayMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
			GetCursorPos(&curPoint);
			SetForegroundWindow(hWnd);
			if (
				TrackPopupMenu(
					trayMenu, TPM_RETURNCMD | TPM_NONOTIFY,
					curPoint.x, curPoint.y, 0, hWnd, NULL
				) == ID_TRAY_EXIT
				)
				PostQuitMessage(0);
			DestroyMenu(trayMenu);
		}
						 break;
		}

	}
					 break;
	case WM_WINDOWPOSCHANGED:
	{
		if (hWnd == overlayWnd)
		{
			WINDOWPOS* winPos = (WINDOWPOS*)lParam;
			settings.setWindowPosX(winPos->x);
			settings.setWindowPosY(winPos->y);
		}
	}
	break;

	case WM_DEVICECHANGE: {
		DEV_BROADCAST_HDR* hdr = (DEV_BROADCAST_HDR*)lParam;
		if (wParam != DBT_DEVICEARRIVAL && wParam != DBT_DEVICEREMOVECOMPLETE)
			return 0;
		if (hdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
			return 0;
		deviceChange();
	}
						break;

	case WM_SYSCOMMAND: {
		switch (wParam & 0xfff0) {
		case SC_MINIMIZE:
			minimise();
			return 0;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
					  break;

	case WM_DESTROY: {
		debug(L"Exiting");
		Shell_NotifyIcon(NIM_DELETE, &niData);
		releaseAll();
		if (settings.getUseCarSpecific() && car[0] != 0)
			settings.writeSettingsForCar(car);
		else
			settings.writeGenericSettings();
		settings.writeRegSettings();
		if (debugHnd != INVALID_HANDLE_VALUE)
			CloseHandle(debugHnd);
		CloseHandle(globalMutex);
		exit(0);
	}
				   break;

	case WM_NCHITTEST:
	{
		LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
		if (hWnd == overlayWnd && hit == HTCLIENT)
			hit = HTCAPTION;

		return hit;
	}
	break;
	case WM_HOTKEY:
	{
		int wmId = LOWORD(wParam);
		if (wmId == ID_MAXFORCE_UP)
			settings.setMaxForce(settings.getMaxForce() - 1, (HWND)-1);

		if (wmId == ID_MAXFORCE_DOWN)
			settings.setMaxForce(settings.getMaxForce() + 1, (HWND)-1);

		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {

	UNREFERENCED_PARAMETER(lParam);

	switch (message) {

	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}

	return (INT_PTR)FALSE;

}
LRESULT CALLBACK myPaint(HWND hWnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg)
	{
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			RECT rectangle;
			HDC hdc = BeginPaint(hWnd, &ps);
			SendMessage(hWnd, WM_ERASEBKGND, (WPARAM)GetDC(hWnd), NULL); /* Erases the background */
			GetClientRect(hWnd, &rectangle); /* Gets the toolbar's area */
			FillRect(GetDC(hWnd), &rectangle, (HBRUSH)(COLOR_WINDOW + 2)); /* Fills the toolbar's background white */
			EndPaint(hWnd, &ps);
			
			break;
		}	
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}
LRESULT CALLBACK myNcHitTest(HWND hWnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg)
	{
	case WM_NCHITTEST:
	{
		LRESULT hit = DefSubclassProc(hWnd, uMsg, wParam, lParam);
		if (hit == HTCLIENT) hit = HTTRANSPARENT;
		return hit;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void text(wchar_t* fmt, ...) {

	va_list argp;
	wchar_t msg[512];
	va_start(argp, fmt);

	StringCbVPrintf(msg, sizeof(msg) - 2 * sizeof(wchar_t), fmt, argp);
	va_end(argp);
	StringCbCat(msg, sizeof(msg), L"\r\n");

	SendMessage(textWnd, EM_SETSEL, 0, -1);
	SendMessage(textWnd, EM_SETSEL, -1, 1);
	SendMessage(textWnd, EM_REPLACESEL, 0, (LPARAM)msg);
	SendMessage(textWnd, EM_SCROLLCARET, 0, 0);

	debug(msg);

}

void text(wchar_t* fmt, char* charstr) {

	int len = strlen(charstr) + 1;
	wchar_t* wstr = new wchar_t[len];
	mbstowcs_s(nullptr, wstr, len, charstr, len);
	text(fmt, wstr);
	delete[] wstr;
	return;
}

void debug(wchar_t* msg) {

	if (!settings.getDebug())
		return;

	DWORD written;
	wchar_t buf[512];
	SYSTEMTIME lt;

	GetLocalTime(&lt);
	StringCbPrintfW(
		buf, sizeof(buf), L"%d-%02d-%02d %02d:%02d:%02d.%03d ",
		lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds
	);

	StringCbCat(buf, sizeof(buf), msg);
	StringCbCat(buf, sizeof(buf), L"\r\n");

	if (!wcscmp(msg, debugLastMsg)) {
		debugRepeat++;
		return;
	}
	else if (debugRepeat) {
		wchar_t rm[256];
		StringCbPrintfW(rm, sizeof(rm), L"-- Last message repeated %d times --\r\n", debugRepeat);
		WriteFile(debugHnd, rm, wcslen(rm) * sizeof(wchar_t), &written, NULL);
		debugRepeat = 0;
	}

	StringCbCopy(debugLastMsg, sizeof(debugLastMsg), msg);
	WriteFile(debugHnd, buf, wcslen(buf) * sizeof(wchar_t), &written, NULL);

}

template <typename... T>
void debug(wchar_t* fmt, T... args) {

	if (!settings.getDebug())
		return;

	wchar_t msg[512];
	StringCbPrintf(msg, sizeof(msg), fmt, args...);
	debug(msg);

}

void setCarStatus(char* carStr) {

	if (!carStr || carStr[0] == 0) {
		SendMessage(statusWnd, SB_SETTEXT, STATUS_CAR_PART, LPARAM(L"Car: generic"));
		return;
	}

	int len = strlen(carStr) + 1;
	wchar_t* wstr = new wchar_t[len + 5];
	lstrcpy(wstr, L"Car: ");
	mbstowcs_s(nullptr, wstr + 5, len, carStr, len);
	SendMessage(statusWnd, SB_SETTEXT, STATUS_CAR_PART, LPARAM(wstr));
	delete[] wstr;

}

void setConnectedStatus(bool connected) {

	SendMessage(
		statusWnd, SB_SETTEXT, STATUS_CONNECTED_PART,
		LPARAM(connected ? L"iRacing connected" : L"iRacing disconnected")
	);

}

void setOnTrackStatus(bool onTrack) {

	SendMessage(
		statusWnd, SB_SETTEXT, STATUS_ONTRACK_PART,
		LPARAM(onTrack ? L"On track" : L"Not on track")
	);

	if (!onTrack && deviceChangePending) {
		debug(L"Processing deferred device change notification");
		deviceChange();
	}

}

void setLogiWheelRange(WORD prodId) {

	if (prodId == G25PID || prodId == DFGTPID || prodId == G27PID) {

		GUID hidGuid;
		HidD_GetHidGuid(&hidGuid);

		text(L"DFGT/G25/G27 detected, setting range using raw HID");

		HANDLE devInfoSet = SetupDiGetClassDevsW(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (devInfoSet == INVALID_HANDLE_VALUE) {
			text(L"LogiWheel: Error enumerating HID devices");
			return;
		}

		SP_DEVICE_INTERFACE_DATA intfData;
		SP_DEVICE_INTERFACE_DETAIL_DATA* intfDetail;
		DWORD idx = 0;
		DWORD error = 0;
		DWORD size;

		while (true) {

			intfData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

			if (!SetupDiEnumDeviceInterfaces(devInfoSet, NULL, &hidGuid, idx++, &intfData)) {
				if (GetLastError() == ERROR_NO_MORE_ITEMS)
					break;
				continue;
			}

			if (!SetupDiGetDeviceInterfaceDetailW(devInfoSet, &intfData, NULL, 0, &size, NULL))
				if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
					text(L"LogiWheel: Error getting intf detail");
					continue;
				}

			intfDetail = (SP_DEVICE_INTERFACE_DETAIL_DATA*)malloc(size);
			intfDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

			if (!SetupDiGetDeviceInterfaceDetailW(devInfoSet, &intfData, intfDetail, size, NULL, NULL)) {
				free(intfDetail);
				continue;
			}

			if (
				wcsstr(intfDetail->DevicePath, G25PATH) != NULL ||
				wcsstr(intfDetail->DevicePath, DFGTPATH) != NULL ||
				wcsstr(intfDetail->DevicePath, G27PATH) != NULL
				) {

				HANDLE file = CreateFileW(
					intfDetail->DevicePath,
					GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
				);

				if (file == INVALID_HANDLE_VALUE) {
					text(L"LogiWheel: Failed to open HID device");
					free(intfDetail);
					SetupDiDestroyDeviceInfoList(devInfoSet);
					return;
				}

				DWORD written;

				if (!WriteFile(file, LOGI_WHEEL_HID_CMD, LOGI_WHEEL_HID_CMD_LEN, &written, NULL))
					text(L"LogiWheel: Failed to write to HID device");
				else
					text(L"LogiWheel: Range set to 900 deg via raw HID");

				CloseHandle(file);
				free(intfDetail);
				SetupDiDestroyDeviceInfoList(devInfoSet);
				return;

			}

			free(intfDetail);

		}

		text(L"Failed to locate Logitech wheel HID device, can't set range");
		SetupDiDestroyDeviceInfoList(devInfoSet);
		return;

	}

	text(L"Attempting to set range via LGS");

	UINT msgId = RegisterWindowMessage(L"LGS_Msg_SetOperatingRange");
	if (!msgId) {
		text(L"Failed to register LGS window message, can't set range..");
		return;
	}

	HWND LGSmsgHandler =
		FindWindowW(
			L"LCore_MessageHandler_{C464822E-04D1-4447-B918-6D5EB33E0E5D}",
			NULL
		);

	if (LGSmsgHandler == NULL) {
		text(L"Failed to locate LGS msg handler, can't set range..");
		return;
	}

	SendMessageW(LGSmsgHandler, msgId, prodId, 900);
	text(L"Range of Logitech wheel set to 900 deg via LGS");

}

BOOL CALLBACK EnumFFDevicesCallback(LPCDIDEVICEINSTANCE diDevInst, VOID* wnd) {

	UNREFERENCED_PARAMETER(wnd);

	if (lstrcmp(diDevInst->tszProductName, L"vJoy Device") == 0)
		return true;

	settings.addFfbDevice(diDevInst->guidInstance, diDevInst->tszProductName);
	debug(L"Adding DI device: %s", diDevInst->tszProductName);

	return true;

}

BOOL CALLBACK EnumObjectCallback(const LPCDIDEVICEOBJECTINSTANCE inst, VOID* dw) {

	UNREFERENCED_PARAMETER(inst);

	(*(int*)dw)++;
	return DIENUM_CONTINUE;

}

void enumDirectInput() {

	settings.clearFfbDevices();

	if (
		FAILED(
			DirectInput8Create(
				GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8,
				(VOID**)&pDI, nullptr
			)
		)
		) {
		text(L"Failed to initialise DirectInput");
		return;
	}

	pDI->EnumDevices(
		DI8DEVCLASS_GAMECTRL, EnumFFDevicesCallback, settings.getDevWnd(),
		DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK
	);

}

void initDirectInput() {

	DIDEVICEINSTANCE di;
	HRESULT hr;

	numButtons = numPov = 0;
	di.dwSize = sizeof(DIDEVICEINSTANCE);

	if (ffdevice && effect && ffdevice->GetDeviceInfo(&di) >= 0 && di.guidInstance == settings.getFfbDevice())
		return;

	releaseDirectInput();

	if (
		FAILED(
			DirectInput8Create(
				GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8,
				(VOID**)&pDI, nullptr
			)
		)
		) {
		text(L"Failed to initialise DirectInput");
		return;
	}

	if (FAILED(pDI->CreateDevice(settings.getFfbDevice(), &ffdevice, nullptr))) {
		text(L"Failed to create DI device");
		text(L"Is it connected and powered on?");
		return;
	}
	if (FAILED(ffdevice->SetDataFormat(&c_dfDIJoystick))) {
		text(L"Failed to set DI device DataFormat!");
		return;
	}
	if (FAILED(ffdevice->SetCooperativeLevel(mainWnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND))) {
		text(L"Failed to set DI device CooperativeLevel!");
		return;
	}

	if (FAILED(ffdevice->GetDeviceInfo(&di))) {
		text(L"Failed to get info for DI device!");
		return;
	}

	if (FAILED(ffdevice->EnumObjects(EnumObjectCallback, (VOID*)&numButtons, DIDFT_BUTTON))) {
		text(L"Failed to enumerate DI device buttons");
		return;
	}

	if (FAILED(ffdevice->EnumObjects(EnumObjectCallback, (VOID*)&numPov, DIDFT_POV))) {
		text(L"Failed to enumerate DI device povs");
		return;
	}

	if (FAILED(ffdevice->SetEventNotification(wheelEvent))) {
		text(L"Failed to set event notification on DI device");
		return;
	}

	DWORD vidpid = getDeviceVidPid(ffdevice);
	if (LOWORD(vidpid) == 0x046d) {
		logiWheel = true;
		setLogiWheelRange(HIWORD(vidpid));
	}
	else
		logiWheel = false;

	if (FAILED(ffdevice->Acquire())) {
		text(L"Failed to acquire DI device");
		return;
	}

	text(L"Acquired DI device with %d buttons and %d POV", numButtons, numPov);

	EnterCriticalSection(&effectCrit);

	if (FAILED(ffdevice->CreateEffect(GUID_Sine, &dieff, &effect, nullptr))) {
		text(L"Failed to create sine periodic effect");
		LeaveCriticalSection(&effectCrit);
		return;
	}

	if (!effect) {
		text(L"Effect creation failed");
		LeaveCriticalSection(&effectCrit);
		return;
	}

	hr = effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
	if (hr == DIERR_NOTINITIALIZED || hr == DIERR_INPUTLOST || hr == DIERR_INCOMPLETEEFFECT || hr == DIERR_INVALIDPARAM)
		text(L"Error setting parameters of DIEFFECT: %d", hr);

	LeaveCriticalSection(&effectCrit);

}

void releaseDirectInput() {

	if (effect) {
		setFFB(0);
		EnterCriticalSection(&effectCrit);
		effect->Stop();
		effect->Release();
		effect = nullptr;
		LeaveCriticalSection(&effectCrit);
	}
	if (ffdevice) {
		ffdevice->Unacquire();
		ffdevice->Release();
		ffdevice = nullptr;
	}
	if (pDI) {
		pDI->Release();
		pDI = nullptr;
	}

}

void reacquireDIDevice() {

	if (ffdevice == nullptr) {
		debug(L"!! ffdevice was null during reacquire !!");
		return;
	}

	HRESULT hr;

	ffdevice->Unacquire();
	ffdevice->Acquire();

	EnterCriticalSection(&effectCrit);

	if (effect == nullptr) {
		if (FAILED(ffdevice->CreateEffect(GUID_Sine, &dieff, &effect, nullptr))) {
			text(L"Failed to create periodic effect during reacquire");
			LeaveCriticalSection(&effectCrit);
			return;
		}
	}

	hr = effect->SetParameters(&dieff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
	if (hr == DIERR_NOTINITIALIZED || hr == DIERR_INPUTLOST || hr == DIERR_INCOMPLETEEFFECT || hr == DIERR_INVALIDPARAM)
		text(L"Error setting parameters of DIEFFECT during reacquire: 0x%x", hr);

	LeaveCriticalSection(&effectCrit);

}

inline void sleepSpinUntil(PLARGE_INTEGER base, UINT sleep, UINT offset) {

	if (!settings.getUseAltTimer()) {
		nanosleep(offset);
	}
	else {
		int i = 0;
		LARGE_INTEGER time;
		LONGLONG until = base->QuadPart + (offset * freq.QuadPart) / 1000000;

		if (!sleepSpin || sleep > 1000)
			std::this_thread::sleep_for(std::chrono::microseconds(sleep));

		QueryPerformanceCounter(&time);
		while (time.QuadPart < until) {
			_mm_pause();
			QueryPerformanceCounter(&time);
			// i++;
		}
	}
	//text(L"Paused for %d", i);
}

inline void nanosleep(LONGLONG ns) {
	/* Declarations */
	HANDLE timer;     /* Timer handle */
	LARGE_INTEGER li; /* Time defintion */
	/* Create timer */

	if (!(timer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS))) {
		return;
	}

	li.QuadPart = -ns;
	if (!SetWaitableTimer(timer, &li, 0, NULL, NULL, FALSE)) {
		CloseHandle(timer);
		return;
	}
	/* Start & wait for timer */
	WaitForSingleObject(timer, INFINITE);
	/* Clean resources */
	CloseHandle(timer);
	/* Slept without problems */
	return;
}


inline int scaleTorque(float t) {

	return (int)(t * settings.getScaleFactor());

}

inline void setFFB(int mag) {

	if (!effect)
		return;

	int clippedAmmount = 0;

	if (mag <= -IR_MAX)
	{
		clippedAmmount = mag - -IR_MAX;
		mag = -IR_MAX;
		clippedSamples++;
	}
	else if (mag >= IR_MAX) {
		clippedAmmount = mag - IR_MAX;
		mag = IR_MAX;
		clippedSamples++;
	}

	samples++;
	int minForce = settings.getMinForce();

	if (stopped && settings.getReduceWhenParked())
		mag /= 4;
	else if (minForce) {
		if (mag > 0 && mag < minForce)
			mag = minForce;
		else if (mag < 0 && mag > -minForce)
			mag = -minForce;
	}

	ffbMag = mag;

	::SendMessage(currentForceWnd, PBM_SETPOS, (WPARAM)(INT)abs(mag), 0);
	if (clippedAmmount > progressClippingMax)
	{
		progressClippingMax = clippedAmmount;
		::SendMessage(clippingForceWnd, PBM_SETRANGE32, 0, (WPARAM)(INT)progressClippingMax);
		::SendMessage(clippingForceOverlayWnd, PBM_SETRANGE32, 0, (WPARAM)(INT)progressClippingMax);
	}

	::SendMessage(clippingForceWnd, PBM_SETPOS, (WPARAM)(INT)abs(clippedAmmount), 0);

	if (settings.getShowForceOverlay())
	{
		::SendMessage(currentForceOverlayWnd, PBM_SETPOS, (WPARAM)(INT)abs(mag), 0);
		::SendMessage(clippingForceOverlayWnd, PBM_SETPOS, (WPARAM)(INT)abs(clippedAmmount), 0);
	}
}

bool initVJD() {

	SHORT verDrv;
	int maxVjDev;
	VjdStat vjdStatus = VJD_STAT_UNKN;

	if (!vJoyEnabled()) {
		text(L"vJoy not enabled!");
		return false;
	}
	else {
		verDrv = GetvJoyVersion();
		text(L"vJoy driver version %04x init OK", verDrv);
	}


	vjDev = 1;

	if (!GetvJoyMaxDevices(&maxVjDev)) {
		text(L"Failed to determine max number of vJoy devices");
		return false;
	}

	while (vjDev <= maxVjDev) {

		vjdStatus = GetVJDStatus(vjDev);

		if (vjdStatus == VJD_STAT_BUSY || vjdStatus == VJD_STAT_MISS)
			goto NEXT;
		if (!GetVJDAxisExist(vjDev, HID_USAGE_X))
			goto NEXT;
		if (!IsDeviceFfb(vjDev))
			goto NEXT;
		if (
			!IsDeviceFfbEffect(vjDev, HID_USAGE_CONST) ||
			!IsDeviceFfbEffect(vjDev, HID_USAGE_SINE) ||
			!IsDeviceFfbEffect(vjDev, HID_USAGE_DMPR) ||
			!IsDeviceFfbEffect(vjDev, HID_USAGE_FRIC) ||
			!IsDeviceFfbEffect(vjDev, HID_USAGE_SPRNG)
			) {
			text(L"vjDev %d: Not all required FFB effects are enabled", vjDev);
			text(L"Enable all FFB effects to use this device");
			goto NEXT;
		}
		break;

	NEXT:
		vjDev++;

	}

	if (vjDev > maxVjDev) {
		text(L"Failed to find suitable vJoy device!");
		text(L"Create a device with an X axis and all FFB effects enabled");
		return false;
	}

	memset(&ffbPacket, 0, sizeof(ffbPacket));

	if (vjdStatus == VJD_STAT_OWN) {
		RelinquishVJD(vjDev);
		vjdStatus = GetVJDStatus(vjDev);
	}
	if (vjdStatus == VJD_STAT_FREE) {
		if (!AcquireVJD(vjDev, ffbEvent, &ffbPacket)) {
			text(L"Failed to acquire vJoy device %d!", vjDev);
			return false;
		}
	}
	else {
		text(L"ERROR: vJoy device %d status is %d", vjDev, vjdStatus);
		return false;
	}

	vjButtons = GetVJDButtonNumber(vjDev);
	vjPov = GetVJDContPovNumber(vjDev);
	vjPov += GetVJDDiscPovNumber(vjDev);

	text(L"Acquired vJoy device %d", vjDev);
	ResetVJD(vjDev);

	return true;

}

void initAll() {

	initVJD();
	initDirectInput();

}

void releaseAll() {

	releaseDirectInput();

	RelinquishVJD(vjDev);

	irsdk_shutdown();

}
