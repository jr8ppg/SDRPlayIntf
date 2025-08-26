#include "stdafx.h"
#include "SDRPlayIntf.h"
#include <stdexcept>
#include <string>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <queue>
#include "RSP.h"
#include <winreg.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <Windows.h>
#include <tchar.h>
#include <sdrplay_api.h>

// https://github.com/SDRplay/examples/blob/master/sdrplay_api_example.c

static bool init_cs = false;
static CRITICAL_SECTION cs;

char Version[8192];
HMODULE ApiDll = NULL;

namespace SDRPlayIntf
{
	std::queue <int> RSP_Access::rsp_input_i;
	std::queue <int> RSP_Access::rsp_input_q;

	static void streamcbfunc_a(short* xi, short* xq, sdrplay_api_StreamCbParamsT* params, unsigned int numSamples, unsigned int reset, void* cbContext)
	{
		unsigned int i;

		if (reset)
		{
			if (debug)
			{
				char buf[256];
				sprintf_s(buf, sizeof(buf), "sdrplay_api_StreamACallback: numSamples=%d, hwver=%d", numSamples, ((sdrplay_api_DeviceT*)cbContext)->hwVer);
				write_text_to_log_file(buf);
			}
		}

		for (i = 0; i < numSamples; i++)
		{
			/*write_text_to_log_file("Calling SendDirect");*/
			SendDirect(*(xi + i), *(xq + i));
		}
	}

	static void streamcbfunc_b(short* xi, short* xq, sdrplay_api_StreamCbParamsT* params, unsigned int numSamples, unsigned int reset, void* cbContext)
	{
		unsigned int i;

		if (reset)
		{
			if (debug)
			{
				char buf[256];
				sprintf_s(buf, sizeof(buf), "sdrplay_api_StreamBCallback: numSamples=%d, hwver=%d", numSamples, ((sdrplay_api_DeviceT*)cbContext)->hwVer);
				write_text_to_log_file(buf);
			}
		}

		for (i = 0; i < numSamples; i++)
		{
			/*write_text_to_log_file("Calling SendDirect");*/
			SendDirect(*(xi + i), *(xq + i));
		}
	}

	static void evcbfunc(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT* params, void* cbContext)
	{
		switch (eventId)
		{
		case sdrplay_api_GainChange:
			if (debug)
			{
				char buf[256];
				sprintf_s(buf, sizeof(buf), "sdrplay_api_GainChange: %f", params->gainParams.currGain);
				write_text_to_log_file(buf);
			}
			break;
		case sdrplay_api_PowerOverloadChange:
			if (debug)
			{
				if (params->powerOverloadParams.powerOverloadChangeType == sdrplay_api_Overload_Detected)
				{
					write_text_to_log_file("sdrplay_api_PowerOverloadChange detected");
				}
				else
				{
					write_text_to_log_file("sdrplay_api_PowerOverloadChange corrected");
				}
			}
			break;
		case sdrplay_api_RspDuoModeChange:
			if (debug)
			{
				char buf[256];
				sprintf_s(buf, 256, "sdrplay_api_EventCb: %s, tuner=%s modeChangeType=%s",
					"sdrplay_api_RspDuoModeChange", (tuner == sdrplay_api_Tuner_A) ?
					"sdrplay_api_Tuner_A" : "sdrplay_api_Tuner_B",
					(params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterInitialised) ?
					"sdrplay_api_MasterInitialised" :
					(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveAttached) ?
					"sdrplay_api_SlaveAttached" :
					(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveDetached) ?
					"sdrplay_api_SlaveDetached" :
					(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveInitialised) ?
					"sdrplay_api_SlaveInitialised" :
					(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveUninitialised) ?
					"sdrplay_api_SlaveUninitialised" :
					(params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterDllDisappeared) ?
					"sdrplay_api_MasterDllDisappeared" :
					(params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveDllDisappeared) ?
					"sdrplay_api_SlaveDllDisappeared" : "unknown type");
				write_text_to_log_file(buf);

				//if (params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterInitialised)
				//	masterInitialised = 1;
				//if (params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveUninitialised)
				//	slaveUninitialised = 1;
			}
			break;
		case sdrplay_api_DeviceRemoved:
			break;
		default:
			break;
		}
	}

	static void gccbfunc(unsigned int gRdB, unsigned int lnaGRdB, void* cbContext)
	{

	}

	RSP_Access::RSP_Access()
	{
		if (init_cs == false)
		{
			InitializeCriticalSection(&cs);
			init_cs = true;
		}
	};

	void RSP_Access::push(int i, int q)
	{
		EnterCriticalSection(&cs);
		rsp_input_i.push(i);
		rsp_input_q.push(q);
		LeaveCriticalSection(&cs);
	}

	bool RSP_Access::is_empty()
	{
		if (rsp_input_i.empty())
		{
			return true;
		}
		return false;
	}

	int RSP_Access::front_and_pop_i()
	{
		int val;
		EnterCriticalSection(&cs);
		val = rsp_input_i.front();
		rsp_input_i.pop();
		LeaveCriticalSection(&cs);
		return val;
	}

	int RSP_Access::front_and_pop_q()
	{
		int val;
		EnterCriticalSection(&cs);
		val = rsp_input_q.front();
		rsp_input_q.pop();
		LeaveCriticalSection(&cs);
		return val;
	}

	long RSP_Access::size()
	{
		return rsp_input_i.size();
	}

	RSP::RSP(void)
	{
		sdrplay_api_Open_fn = NULL;
		sdrplay_api_Close_fn = NULL;
		sdrplay_api_GetDeviceParams_fn = NULL;
		sdrplay_api_Init_fn = NULL;
		sdrplay_api_Uninit_fn = NULL;
		sdrplay_api_LockDeviceApi_fn = NULL;
		sdrplay_api_UnlockDeviceApi_fn = NULL;
		sdrplay_api_GetDevices_fn = NULL;
		sdrplay_api_SelectDevice_fn = NULL;
		sdrplay_api_ReleaseDevice_fn = NULL;
		sdrplay_api_GetErrorString_fn = NULL;
		sdrplay_api_Update_fn = NULL;

		cbparams.StreamACbFn = NULL;
		cbparams.StreamBCbFn = NULL;
		cbparams.EventCbFn = NULL;

		/*_rsp_access = new RSP_Access;*/

		memset(devices_found, 0, sizeof(devices_found));
		_rspfreq = 0;
		_rxstarted = false;
		chosen_rsp_idx = 255;
		nDecimateFactor = 16;
		nGainReduction = 50;
		nLNAstate = 1;
		memset(szLastRspSerial, 0, sizeof(szLastRspSerial));
		memset(szDeviceName, 0, sizeof(szDeviceName));
		nAntenna = 0;
		nHiz = 1;
		nRecvCount = 0;
	}

	std::string GetSdrPlayApiGlobalInstallPath()
	{
		HKEY APIkey;
		char APIkeyValue[1024];
		char tmpStringA[1024];
		DWORD APIkeyValue_length = sizeof(APIkeyValue);
		int error;

		if (RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\SDRPlay\\Service\\API", &APIkey) != ERROR_SUCCESS)
		{
			error = GetLastError();
			MessageBox(NULL, "SDRPlay API Not Installed", "Install 3.15 API from SDRPlay web site", MB_OK | MB_ICONEXCLAMATION);
			rt_exception("Failed to locate API registry entry error = " + error);
			return std::string("");
		}

		RegQueryValueEx(APIkey, "Install_Dir", NULL, NULL, (LPBYTE)&APIkeyValue, &APIkeyValue_length);
		//RegQueryValueEx(APIkey, "Version", NULL, NULL, (LPBYTE)&APIVersion, &APIVersion_length);
		RegCloseKey(APIkey);

#ifndef _WIN64
		sprintf_s(tmpStringA, sizeof(tmpStringA), "%s\\x86\\sdrplay_api.dll", APIkeyValue);
#else
		sprintf_s(tmpStringA, 8192, "%s\\x64\\sdrplay_api.dll", APIkeyValue);
#endif
		//LPCSTR ApiDllName = (LPCSTR)tmpStringA;
		std::string apipath = std::string((LPCSTR)tmpStringA);

		return(apipath);
	}

	std::string GetSdrPlayApiLocalInstallPath()
	{
		char* p = NULL;
		char szBuffer[MAX_PATH];
		char szApiFileName[MAX_PATH];

		GetModuleFileName(NULL, szBuffer, sizeof(szBuffer));
		strcpy_s(szApiFileName, sizeof(szApiFileName), szBuffer);

		p = strrchr(szApiFileName, '\\');
		if (p != NULL)
		{
			*(p + 1) = '\0';
		}
		else
		{
			szApiFileName[0] = '\0';
		}

		strcat_s(szApiFileName, sizeof(szApiFileName), "sdrplay_api.dll");

		std::string apipath = std::string(szApiFileName);

		return(apipath);
	}

	int RSP::LoadApi(void)
	{
		std::string apipath;
		int error;
		sdrplay_api_ErrT ret;
		float apiver = 0;

		if (debug)
		{
			write_text_to_log_file("BEGIN - RSP::LoadAPI()");
		}

		apipath = GetSdrPlayApiLocalInstallPath();

		if (debug)
		{
			write_text_to_log_file("local api path = " + apipath);
		}

		ApiDll = LoadLibrary(apipath.c_str());
		if (ApiDll == NULL)
		{
			apipath = GetSdrPlayApiGlobalInstallPath();

			if (debug)
			{
				write_text_to_log_file("global api path = " + apipath);
			}

			ApiDll = LoadLibrary(apipath.c_str());
			if (ApiDll == NULL)
			{
				error = GetLastError();
				rt_exception("Failed to locate sdrplay_api.dll = " + error);
				return false;
			}
		}

		// Get the address of each API function
		sdrplay_api_Open_fn = (sdrplay_api_Open_t)GetProcAddress(ApiDll, "sdrplay_api_Open");
		sdrplay_api_Close_fn = (sdrplay_api_Close_t)GetProcAddress(ApiDll, "sdrplay_api_Close");
		sdrplay_api_GetDeviceParams_fn = (sdrplay_api_GetDeviceParams_t)GetProcAddress(ApiDll, "sdrplay_api_GetDeviceParams");
		sdrplay_api_Init_fn = (sdrplay_api_Init_t)GetProcAddress(ApiDll, "sdrplay_api_Init");
		sdrplay_api_Uninit_fn = (sdrplay_api_Uninit_t)GetProcAddress(ApiDll, "sdrplay_api_Uninit");
		sdrplay_api_LockDeviceApi_fn = (sdrplay_api_LockDeviceApi_t)GetProcAddress(ApiDll, "sdrplay_api_LockDeviceApi");
		sdrplay_api_UnlockDeviceApi_fn = (sdrplay_api_UnlockDeviceApi_t)GetProcAddress(ApiDll, "sdrplay_api_UnlockDeviceApi");
		sdrplay_api_GetDevices_fn = (sdrplay_api_GetDevices_t)GetProcAddress(ApiDll, "sdrplay_api_GetDevices");
		sdrplay_api_SelectDevice_fn = (sdrplay_api_SelectDevice_t)GetProcAddress(ApiDll, "sdrplay_api_SelectDevice");
		sdrplay_api_ReleaseDevice_fn = (sdrplay_api_ReleaseDevice_t)GetProcAddress(ApiDll, "sdrplay_api_ReleaseDevice");
		sdrplay_api_GetErrorString_fn = (sdrplay_api_GetErrorString_t)GetProcAddress(ApiDll, "sdrplay_api_GetErrorString");
		sdrplay_api_Update_fn = (sdrplay_api_Update_t)GetProcAddress(ApiDll, "sdrplay_api_Update");
		sdrplay_api_ApiVersion_fn = (sdrplay_api_ApiVersion_t)GetProcAddress(ApiDll, "sdrplay_api_ApiVersion");

		if ((sdrplay_api_Open_fn == NULL) ||
			(sdrplay_api_Close_fn == NULL) ||
			(sdrplay_api_GetDeviceParams_fn == NULL) ||
			(sdrplay_api_Init_fn == NULL) ||
			(sdrplay_api_Uninit_fn == NULL) ||
			(sdrplay_api_LockDeviceApi_fn == NULL) ||
			(sdrplay_api_UnlockDeviceApi_fn == NULL) ||
			(sdrplay_api_GetDevices_fn == NULL) ||
			(sdrplay_api_SelectDevice_fn == NULL) ||
			(sdrplay_api_ReleaseDevice_fn == NULL) ||
			(sdrplay_api_Update_fn == NULL) ||
			(sdrplay_api_ApiVersion_fn == NULL))
		{
			rt_exception("Failed to set function pointers for mir API functions");
			FreeLibrary(ApiDll);
			return false;
		}

		// Open the SDRPlay API
		ret = sdrplay_api_Open_fn();
		if (ret == sdrplay_api_Success)
		{
			write_text_to_log_file("***** sdrplay_api_Open() success *****");
		}
		else
		{
			write_text_to_log_file("sdrplay_api_Open() failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
		}

		if (debug)
		{
			sdrplay_api_ApiVersion_fn(&apiver);
			write_text_to_log_file("***** SDRPlay API Version = " + std::to_string(apiver) + " *****");

			write_text_to_log_file("END - RSP::LoadAPI()");
		}

		return true;
	}

	void RSP::SetFreq(int Receiver, double freq)
	{
		sdrplay_api_ErrT ret;
		sdrplay_api_TunerSelectT selTuner;

		if (_rxstarted == false)
		{
			return;
		}

		if (debug)
		{
			char buffer[256];

			write_text_to_log_file("****** begin - RSP::SetFreq() ******");
			sprintf_s(buffer, 256, "Setting RSP frequency[%d] to %f", Receiver, freq);
			write_text_to_log_file(buffer);
		}

		// Get device information for the chosen device
		sdrplay_api_DeviceT* chosenDevice = &devices_found[chosen_rsp_idx];

		// Get DeviceParams from chosen device
		sdrplay_api_DeviceParamsT* devparams;
		ret = sdrplay_api_GetDeviceParams_fn(chosenDevice->dev, &devparams);

		// Set to new frequency
		_rspfreq = freq;
		selTuner = sdrplay_api_Tuner_A;
		SetTunerFreq(chosenDevice->hwVer, devparams->rxChannelA, _rspfreq);

		// Update the frequency of the chosen device
		ret = sdrplay_api_Update_fn(chosenDevice->dev, selTuner, sdrplay_api_Update_Tuner_Frf, sdrplay_api_Update_Ext1_None);
		if (ret != sdrplay_api_Success)
		{
			rt_exception("sdrplay_api_Update(chosenDevice->dev, chosenDevice->tuner, sdrplay_api_Update_Tuner_Frf, sdrplay_api_Update_Ext1_None) failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
			return;
		}

		if (debug)
		{
			write_text_to_log_file("_rspfreq    = " + std::to_string(_rspfreq));
			write_text_to_log_file("****** end - RSP::SetFreq() ******");
		}
	}

	void RSP::StartRx()
	{
		sdrplay_api_ErrT ret;

		if (debug)
		{
			write_text_to_log_file("****** begin - StartRx() ****** chosen_rsp_idx=" + std::to_string(chosen_rsp_idx));
		}

		// Get device information for the chosen device
		sdrplay_api_DeviceT* chosenDevice = &devices_found[chosen_rsp_idx];

		// Get DeviceParams from chosen device
		sdrplay_api_DeviceParamsT* devparams;
		ret = sdrplay_api_GetDeviceParams_fn(chosenDevice->dev, &devparams);
		if (ret != sdrplay_api_Success)
		{
			rt_exception("sdrplay_api_GetDeviceParams() failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
			return;
		}

		if (devparams == NULL)
		{
			rt_exception("devparams is null");
			return;
		}

		// Set a sampling frequency
		devparams->devParams->fsFreq.fsHz = 3072000.0;

		// Set the ChannelParams
		SetTunerParams(chosenDevice->hwVer, devparams->rxChannelA, _rspfreq);

		// Set the callback functions
		cbparams.StreamACbFn = &streamcbfunc_a;
		cbparams.StreamBCbFn = &streamcbfunc_b;
		cbparams.EventCbFn = &evcbfunc;

		ret = sdrplay_api_Init_fn(chosenDevice->dev, &cbparams, chosenDevice);
		if (ret != sdrplay_api_Success)
		{
			rt_exception("sdrplay_api_Init() failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
			return;
		}

		if (debug)
		{
			write_text_to_log_file("sdrplay_api_Init() succeeded.");
		}

		// When using RSPdx/RSPdxR2, select the antenna
		if (chosenDevice->hwVer == SDRPLAY_RSPdx_ID || chosenDevice->hwVer == SDRPLAY_RSPdxR2_ID)
		{
			devparams->devParams->rspDxParams.antennaSel = (nAntenna == 0) ? sdrplay_api_RspDx_ANTENNA_A : (nAntenna == 1) ? sdrplay_api_RspDx_ANTENNA_B : sdrplay_api_RspDx_ANTENNA_C;
			ret = sdrplay_api_Update_fn(chosenDevice->dev, chosenDevice->tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_AntennaControl);
			if (ret != sdrplay_api_Success)
			{
				write_text_to_log_file("sdrplay_api_Update(chosenDevice->dev, chosenDevice->tuner, sdrplay_api_Update_None, sdrplay_api_Update_RspDx_AntennaControl) failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
			}
		}

		_rxstarted = true;

		if (debug)
		{
			write_text_to_log_file("DecimateFactor = " + std::to_string(nDecimateFactor));
			write_text_to_log_file("_rspfreq       = " + std::to_string(_rspfreq));
			write_text_to_log_file("****** end - StartRx() ******");
		}
	}

	void RSP::SetTunerParams(unsigned char hwVer, sdrplay_api_RxChannelParamsT *chParams, double rxfreq)
	{
		chParams->tunerParams.rfFreq.rfHz = rxfreq;			// receive frequency
		chParams->tunerParams.bwType = sdrplay_api_BW_0_200;
		chParams->tunerParams.ifType = sdrplay_api_IF_Zero;
		chParams->tunerParams.gain.gRdB = nGainReduction;
		chParams->tunerParams.gain.LNAstate = nLNAstate;
		chParams->tunerParams.gain.minGr = sdrplay_api_NORMAL_MIN_GR;

		// Disable AGC
		chParams->ctrlParams.agc.enable = sdrplay_api_AGC_5HZ;// sdrplay_api_AGC_DISABLE;

		// Decimation
		chParams->ctrlParams.decimation.enable = 1;
		chParams->ctrlParams.decimation.decimationFactor = nDecimateFactor;		// 2, 4, 8, 16 or 32 only
		chParams->ctrlParams.decimation.wideBandSignal = 0;						// 0:Use averaging 1:Use half-band filter

		chParams->ctrlParams.adsbMode = sdrplay_api_ADSB_DECIMATION;

		if (hwVer == SDRPLAY_RSP2_ID)
		{
			chParams->rsp2TunerParams.antennaSel = (nAntenna == 0) ? sdrplay_api_Rsp2_ANTENNA_A : sdrplay_api_Rsp2_ANTENNA_B;
			chParams->rsp2TunerParams.amPortSel = (nHiz == 0) ? sdrplay_api_Rsp2_AMPORT_2 : sdrplay_api_Rsp2_AMPORT_1;
		}
		if (hwVer == SDRPLAY_RSPduo_ID)
		{
			chParams->rspDuoTunerParams.tuner1AmPortSel = (nHiz == 0) ? sdrplay_api_RspDuo_AMPORT_2 : sdrplay_api_RspDuo_AMPORT_1;
		}
	}

	void RSP::SetTunerFreq(unsigned char hwVer, sdrplay_api_RxChannelParamsT *chParams, double rxfreq)
	{
		chParams->tunerParams.rfFreq.rfHz = rxfreq;
	}

	void RSP::StopRx()
	{
		sdrplay_api_ErrT ret;

		if (debug)
		{
			write_text_to_log_file("****** begin - StopRx() ******");
		}

		_rxstarted = false;

		ret = sdrplay_api_Uninit_fn(devices_found[chosen_rsp_idx].dev);
		if (ret != sdrplay_api_Success)
		{
			write_text_to_log_file("sdrplay_api_Uninit_fn() failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
		}

		if (debug)
		{
			write_text_to_log_file("****** end - StopRx() ******");
		}
	}

	void RSP::GetDevices()
	{
		sdrplay_api_ErrT ret;
		unsigned int i;
		unsigned num_found = 0;
		unsigned max = 10;
		std::string tempstr;
		std::string tempstr2;

		if (debug)
		{
			write_text_to_log_file("begin - RSP::GetDevices()");
		}

		chosen_rsp_idx = 255;
		
		// lock
		ret = sdrplay_api_LockDeviceApi_fn();
		if (ret != sdrplay_api_Success)
		{
			rt_exception("sdrplay_api_LockDeviceApi failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
			write_text_to_log_file("sdrplay_api_LockDeviceApi failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
			return;
		}

		// Enumerate devices
		ret = sdrplay_api_GetDevices_fn(&devices_found[0], &num_found, max);
		if (ret != sdrplay_api_Success)
		{
			rt_exception("sdrplay_api_GetDevices failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
			write_text_to_log_file("sdrplay_api_GetDevices failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
			return;
		}

		if (debug)
		{
			char buffer[256];
			write_text_to_log_file("num of devices = " + std::to_string(num_found));

			for (i = 0; i < num_found; i++)
			{
				std::string hwnm = GetRspName(devices_found[i].hwVer);
				if (devices_found[i].hwVer == SDRPLAY_RSPduo_ID)
				{
					sprintf_s(buffer, sizeof(buffer), "Dev%d: SerNo=%s hwVer=%s(%d) tuner=0x%.2x rspDuoMode=0x%.2x", i, devices_found[i].SerNo, hwnm.c_str(), devices_found[i].hwVer, devices_found[i].tuner, devices_found[i].rspDuoMode);
				}
				else {
					sprintf_s(buffer, sizeof(buffer), "Dev%d: SerNo=%s hwVer=%s(%d) tuner=0x%.2x", i, devices_found[i].SerNo, hwnm.c_str(), devices_found[i].hwVer, devices_found[i].tuner);
				}
				write_text_to_log_file(std::string(buffer));
			}
		}

		if (num_found == 0)
		{
			MessageBox(NULL, "No RSP Devices Found", "No RSP Devices", MB_OK | MB_ICONEXCLAMATION);
		}

		// If szLastRspSerial is a NULL string, it means that it was not selected last.
		if (strnlen(szLastRspSerial, sizeof(szLastRspSerial)) == 0)
		{
			if (num_found == 1)
			{
				chosen_rsp_idx = 0;
			}
			else if (num_found > 1)
			{
				// Since MessageBox() cannot be used in the context of GetSdrInfo(), the top of the device list is automatically selected.
				chosen_rsp_idx = 0;
/*
				int mb_ret;
				i = 0;
				while (i < num_found)
				{
					tempstr2 = GetRspName(devices_found[i].hwVer);

					tempstr += "Do you want to use ";
					tempstr += tempstr2;
					tempstr += " [" +  std::string(devices_found[i].SerNo) +"]";

					write_text_to_log_file(tempstr);

					mb_ret = MessageBox(NULL, tempstr.c_str(), "Multiple RSP Devices Found", MB_YESNO);
					if (mb_ret == IDYES)
					{
						chosen_rsp_idx = i;
						break;
					}
					tempstr = "";
					i++;
				}

				if (chosen_rsp_idx == 255)
				{
					MessageBox(NULL, "You Have Not Chosen a Device", "Multiple RSP Devices Found", MB_OK | MB_ICONHAND);
				}
*/
			}
		}
		else
		{
			// Get the index of the serial number from the device list
			chosen_rsp_idx = GetRspIndexBySerial(szLastRspSerial);
		}

		// select a device
		if (chosen_rsp_idx != 255)
		{
			// Get device information for the chosen device
			sdrplay_api_DeviceT* chosenDevice = &devices_found[chosen_rsp_idx];

			if (chosenDevice->hwVer == SDRPLAY_RSPduo_ID)
			{
				chosenDevice->rspDuoMode = sdrplay_api_RspDuoMode_Single_Tuner;
				chosenDevice->tuner = sdrplay_api_Tuner_A;
				chosenDevice->rspDuoSampleFreq = 3072000.0;
			}

			ret = sdrplay_api_SelectDevice_fn(chosenDevice);
			if (ret != sdrplay_api_Success)
			{
				rt_exception("sdrplay_api_SelectDevice failed = " + (std::string)sdrplay_api_GetErrorString_fn(ret));
			}

			if (debug)
			{
				char buf[256];
				sprintf_s(buf, 256, "chosenDevice->rspDuoSampleFreq=%f", chosenDevice->rspDuoSampleFreq);
				write_text_to_log_file(buf);
			}

			strcpy(szLastRspSerial, chosenDevice->SerNo);
			strcpy(szDeviceName, GetRspName(chosenDevice->hwVer).c_str());
		}
		else
		{
			memset(szLastRspSerial, 0, sizeof(szLastRspSerial));
			memset(szDeviceName, 0, sizeof(szDeviceName));
		}

		if (debug)
		{
			write_text_to_log_file("chosen_rsp_idx = " + std::to_string(chosen_rsp_idx));
			write_text_to_log_file("szLastRspSerial = " + std::string(szLastRspSerial));
		}

		// unlock
		sdrplay_api_UnlockDeviceApi_fn();

		if (debug)
		{
			write_text_to_log_file("end - RSP::GetDevices()");
		}
	}

	int RSP::GetRspIndexBySerial(char *szSerial)
	{
		int i;

		for (i = 0; i < 10; i++)
		{
			if (_strnicmp(devices_found[i].SerNo, szSerial, SDRPLAY_MAX_SER_NO_LEN) == 0)
			{
				return(i);
			}
		}

		return 255;
	}

	std::string RSP::GetRspName(int hwVer)
	{
		std::string tempstr2;

		switch (hwVer)
		{
		case SDRPLAY_RSP1_ID:
			tempstr2 = "RSP1";
			break;
		case SDRPLAY_RSP2_ID:
			tempstr2 = "RSP2";
			break;
		case SDRPLAY_RSPduo_ID:
			tempstr2 = "RSPDuo";
			break;
		case SDRPLAY_RSPdx_ID:
			tempstr2 = "RSPdx";
			break;
		case SDRPLAY_RSP1B_ID:
			tempstr2 = "RSP1B";
			break;
		case SDRPLAY_RSPdxR2_ID:
			tempstr2 = "RSPdxR2";
			break;
		case SDRPLAY_RSP1A_ID:
			tempstr2 = "RSP1A";
			break;
		default:
			tempstr2 = "Unknown Device";
		}

		return(tempstr2);
	}

	RSP::~RSP()
	{
		sdrplay_api_LockDeviceApi_fn();
		sdrplay_api_ReleaseDevice_fn(&devices_found[chosen_rsp_idx]);
		sdrplay_api_UnlockDeviceApi_fn();
		sdrplay_api_Close_fn();
		FreeLibrary(ApiDll);
	}
}