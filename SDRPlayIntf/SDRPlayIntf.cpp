// HermesIntf.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "SDRPlayIntf.h"
#include <stdexcept>
#include <string>
#include <iostream>
#include <sstream>
#include <queue>
#include "RSP.h"
#include <assert.h>
#include <time.h>
#include <math.h>
#include <tchar.h>

namespace SDRPlayIntf
{
	///////////////////////////////////////////////////////////////////////////////
	// Global variables

	// String buffer for device name
	char display_name[50];

	// Settings from Skimmer server
	SdrSettings gSet;

	volatile int ADC_overflow_count = 0;

	// Length of block for one call of IQProc
	int gBlockInSamples = 0;

	// Buffers for calling IQProc
	CmplxA gData[MAX_RX_COUNT];

	// Current length of data in Buffers for calling IQProc (in samples)
	int gDataSamples = 0;

	// Instance of RSP
	RSP myRSP;

	// Stop flag
	volatile bool gStopFlag = true;

	double frequencies[MAX_RX_COUNT];
	int num_frequencies;
	bool rotating = false;
	volatile int current_rx = 0;
	Cmplx *optr[MAX_RX_COUNT];

	HANDLE hTimer = NULL;
	HANDLE hTimerQueue = NULL;
	int arg = 123;

	bool debug = false;
	int nRotationInterval = 2;
	int nGainReduction = 50;
	int nLNAstate = 1;
	int nLastRSPIndex = 999;
	int nAntenna = 0;
	int nHiz = 1;

	bool allocated = false;

	char szSkimSrvIni[MAX_PATH];
	char szSkimSrvLog[MAX_PATH];
	char szSkimSrvFolder[MAX_PATH];

	struct Decimate {
		double fSampleRate;
		int nDecimateFactor;
	};
	struct Decimate decimate_table[] = {
		{ 48e3, 64, },
		{ 96e3, 32, },
		{ 192e3, 16, },
	};

	//////////////////////////////////////////////////////////////////////////////
	// Allocate working buffers & others
	BOOL Alloc(double fSampleRate)
	{
		if (!allocated)
		{
			int i;

			for (i = 0; i < MAX_RX_COUNT; i++)
			{
				// allocated ?
				if (gData[i] != NULL) {
					free(gData[i]);
				}

				gData[i] = NULL;
			}

			// compute length of block in samples
			gBlockInSamples = (int)((float)fSampleRate / (float)BLOCKS_PER_SEC);

			// allocate buffers for calling IQProc
			for (i = 0; i < MAX_RX_COUNT; i++)
			{
				// allocated ?
				if (gData[i] != NULL) free(gData[i]);

				// allocate buffer
				gData[i] = (CmplxA)malloc((gBlockInSamples + 100) * sizeof(Cmplx));

				// have we memory ?
				if (gData[i] == NULL)
				{
					// low memory
					rt_exception("Low memory");
					return(FALSE);
				}

				// clear it
				memset(gData[i], 0, (gBlockInSamples + 100) * sizeof(Cmplx));
			}

			gDataSamples = 0;
		}
		allocated = true;

		// success
		return(TRUE);
	}

	void SendDirect(int iq_i, int iq_q)
	{

		int j;
		int smplI, smplQ;

		int gNChan = gSet.RecvCount;

		smplI = iq_i << 8;
		smplQ = iq_q << 8;

		for (j = 0; j < gNChan; j++)
		{
			if (j != current_rx)
			{
				optr[j]->Re = (float)(smplI & 0x7FF); //psuedo white noise on the inactive virtual receivers
				optr[j]->Im = (float)(smplQ & 0x7FF); //psuedo white noise on the inactive virtual receivers
				(optr[j])++;
			}
			else
			{
				optr[current_rx]->Re = (float)smplI;
				optr[current_rx]->Im = (float)smplQ;
				(optr[current_rx])++;
			}
		}

		//write_text_to_log_file("SendDirect 2");
		gDataSamples++;

		// do we have enough data ?
		if (gDataSamples >= gBlockInSamples)
		{
			gDataSamples = 0;
			if (gSet.pIQProc != NULL) {
				(*gSet.pIQProc)(gSet.THandle, gData);
			}

			// start filling of new data
			for (int kount = 0; kount < gNChan; kount++) {
				optr[kount] = gData[kount];
			}
		}
	}

	extern "C" 
	{
		void LoadSettings(void);
		void SaveSettings(void);
		
		BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
		{
			char szBuffer[MAX_PATH];

			switch (ul_reason_for_call)
			{
				case DLL_PROCESS_ATTACH:

					ExpandEnvironmentStrings("%APPDATA%", szBuffer, sizeof(szBuffer));
					strcpy_s(szSkimSrvFolder, sizeof(szSkimSrvFolder), szBuffer);
					strcat_s(szSkimSrvFolder, sizeof(szSkimSrvFolder), "\\Afreet\\Products\\SkimSrv\\");

					strcpy_s(szSkimSrvIni, sizeof(szSkimSrvIni), szSkimSrvFolder);
					strcat_s(szSkimSrvIni, sizeof(szSkimSrvIni), "SkimSrv.ini");

					strcpy_s(szSkimSrvLog, sizeof(szSkimSrvLog), szSkimSrvFolder);
					strcat_s(szSkimSrvLog, sizeof(szSkimSrvLog), "SDRPlayIntf_log_file.txt");

					LoadSettings();

					write_text_to_log_file("RotationInterval = " + std::to_string(nRotationInterval));
					write_text_to_log_file("GainReduction    = " + std::to_string(nGainReduction));
					write_text_to_log_file("LNAstate         = " + std::to_string(nLNAstate));
					write_text_to_log_file("LastRSPIndex     = " + std::to_string(nLastRSPIndex));
					if (debug)
					{
						write_text_to_log_file("debug on");
					}

					break;
				case DLL_THREAD_ATTACH:
					break;
				case DLL_THREAD_DETACH:
					break;
				case DLL_PROCESS_DETACH:
					SaveSettings();
					break;
			}

			return TRUE;
		}

		void LoadSettings(void)
		{
			TCHAR ret_string[255];
			int len;

			ZeroMemory(ret_string, sizeof(ret_string));
			len = GetPrivateProfileString("General", "Debug", "0", ret_string, 255, ".\\SDRPlayIntf.ini");
			if (strcmp(ret_string, "1") == 0)
			{
				debug = true;
			}
			else
			{
				debug = false;
			}

			ZeroMemory(ret_string, sizeof(ret_string));
			len = GetPrivateProfileString("General", "RotationInterval", "2", ret_string, 255, ".\\SDRPlayIntf.ini");
			nRotationInterval = atoi(ret_string);

			ZeroMemory(ret_string, sizeof(ret_string));
			len = GetPrivateProfileString("General", "GainReduction", "50", ret_string, 255, ".\\SDRPlayIntf.ini");
			nGainReduction = atoi(ret_string);

			ZeroMemory(ret_string, sizeof(ret_string));
			len = GetPrivateProfileString("General", "LNAstate", "1", ret_string, 255, ".\\SDRPlayIntf.ini");
			nLNAstate = atoi(ret_string);

			// Get the previous chosen device number
			ZeroMemory(ret_string, sizeof(ret_string));
			len = GetPrivateProfileString("General", "RSPIndex", "999", ret_string, 255, ".\\SDRPlayIntf.ini");
			nLastRSPIndex = atoi(ret_string);

			// RSP2 or RSPDuo or RSPdx or RSPdxR2 to use Antenna B, C
			ZeroMemory(ret_string, sizeof(ret_string));
			len = GetPrivateProfileString("General", "Antenna", "A", ret_string, 255, ".\\SDRPlayIntf.ini");
			if (_stricmp(ret_string, "A") == 0)
			{
				nAntenna = 0;
			}
			else if (_stricmp(ret_string, "B") == 0)
			{
				nAntenna = 1;
			}
			else if (_stricmp(ret_string, "C") == 0)
			{
				nAntenna = 2;
			}
			else
			{
				nAntenna = 0;
			}

			// RSP2 or RSPDuo, set HIZ=1 if you are using the Hi-Z input.
			ZeroMemory(ret_string, sizeof(ret_string));
			len = GetPrivateProfileString("General", "HIZ", "1", ret_string, 255, ".\\SDRPlayIntf.ini");
			nHiz = atoi(ret_string);
		}

		void SaveSettings(void)
		{
			char buffer[255];

			sprintf_s(buffer, sizeof(buffer), "%d", nLastRSPIndex);
			WritePrivateProfileString("General", "RSPIndex", buffer, ".\\SDRPlayIntf.ini");
		}

		DLLEXPORT void __stdcall GetSdrInfo(PSdrInfo pInfo)
		{
			// did we get info ?
			if (pInfo == NULL) return;

			for (int i = 0; i < MAX_RX_COUNT; i++) {
				gData[i] = NULL;
			}

			// RSP test code
			strcpy(display_name, "SDRPlay RSP");
			pInfo->MaxRecvCount = 5;
			pInfo->ExactRates[RATE_48KHZ] = 48e3;
			pInfo->ExactRates[RATE_96KHZ] = 96e3;
			pInfo->ExactRates[RATE_192KHZ] = 192e3;
			pInfo->DeviceName = display_name;

			if (debug)
			{
				write_text_to_log_file("GetSdrInfo");
			}

			// Loading the SDRPlay API
			if (myRSP.LoadApi() == false) //failed
			{
				rt_exception("myRSP.LoadApi failed");
			}

			// Create the timer queue.
			if (hTimerQueue == NULL)
			{
				hTimerQueue = CreateTimerQueue();
			}
			if (NULL == hTimerQueue)
			{
				write_text_to_log_file("CreateTimerQueue failed = " + std::to_string(GetLastError()));
			}

			return;
		}

		// Start receivers
		DLLEXPORT void __stdcall StartRx(PSdrSettings pSettings)
		{
			int gNChan;
			int i;

			// Sample rate of Skimmer server
			double fSampleRate = 0;

			// have we settings ?
			if (pSettings == NULL) return;

			// make a copy of SDR settings
			memcpy(&gSet, pSettings, sizeof(gSet));

			gNChan = gSet.RecvCount;

			if (debug)
			{
				write_text_to_log_file("StartRx RecvCount = " + std::to_string(gSet.RecvCount));
			}


			// from skimmer server version 1.1 in high bytes is something strange
			gSet.RateID &= 0xFF;

			if (debug)
			{
				write_text_to_log_file("RateID = " + std::to_string(gSet.RateID));
			}

			// Enumerate devices and select one
			myRSP.GetDevices();

			if (myRSP.chosen_rsp_idx == 255)
			{
				rt_exception("Can't locate RSP device");
				return;
			}

			nLastRSPIndex = myRSP.nLastRSPIndex;

			// Adjust Rate ID
			if (gSet.RateID < 0 || gSet.RateID > 2)
			{
				gSet.RateID = 0;
			}

			// Show message
			UINT uiFlags = MB_OK | MB_SETFOREGROUND | MB_SYSTEMMODAL | MB_ICONINFORMATION;
			int iRet = MessageBoxTimeout(NULL, _T("Version 1.9 using SDRPlay API 3.15"), _T("SDRPlayIntf Info"), uiFlags, 0, 2000);

			// Allocate buffers & others
			if (!Alloc(decimate_table[gSet.RateID].fSampleRate))
			{
				// something wrong ...
				return;
			}

			// Prepare pointers to IQ buffer
			gDataSamples = 0;

			for (i = 0; i < gNChan; i++) {
				optr[i] = gData[i];
			}

			// Start RX
			myRSP.nDecimateFactor = decimate_table[gSet.RateID].nDecimateFactor;
			myRSP.nGainReduction = nGainReduction;
			myRSP.nLNAstate = nLNAstate;
			myRSP.nAntenna = nAntenna;
			myRSP.nHiz = nHiz;
			myRSP.StartRx();

			// Start worker thread
			gStopFlag = false;		
		}

		DLLEXPORT void __stdcall StopRx(void) 
		{
			gStopFlag = true;

			myRSP.StopRx();

			if (debug)
			{
				write_text_to_log_file("StopRx");
			}
		}

		// Set Rx frequency
		DLLEXPORT void __stdcall SetRxFrequency(int Frequency, int Receiver)
		{

			std::string dbg = "Received SetRxFrequency Rx# ";
#ifdef __MINGW32__
			dbg += patch::to_string(Receiver);
#else
			dbg += std::to_string(Receiver);
#endif
			dbg += " Frequency: ";
#ifdef __MINGW32__
			dbg += patch::to_string(Frequency);
#else
			dbg += std::to_string(Frequency);
#endif
			if (debug)
			{
				write_text_to_log_file(dbg);
			}

			if (Receiver < 0)
			{
				rt_exception("Too many receivers selected");
				return;
			}

			Stop_Rotation();
			Sleep(100);

			frequencies[Receiver] = (double)Frequency;

			num_frequencies = Receiver + 1;

			if (debug)
			{
				write_text_to_log_file("num_frequencies = " + std::to_string(num_frequencies) + " rotating = ");
			}

			if ((num_frequencies > 1) && (!rotating))
			{
				Start_Rotation();
			}

			if (Receiver == 0) // Other receivers set by timer
			{
				current_rx = Receiver;
				myRSP.SetFreq((double)frequencies[Receiver]);
			}
			
		}

		DLLEXPORT int __stdcall ReadPort(int PortNumber)
		{
			return(0);
		}

		DLLEXPORT void __stdcall SetCtrlBits(unsigned char Bits)
		{
		}

	}

	void Start_Rotation()
	{
		char buffer[255];

		if (gStopFlag)
		{
			return;
		}

		rotating = true;

		if (debug)
		{
			write_text_to_log_file("Rotation started");
		}

		if (debug)
		{
			sprintf_s(buffer, sizeof(buffer), "hTimer before = %p", hTimer);
			write_text_to_log_file(buffer);
		}

		// Set a timer to call the timer callback
		DWORD duetime = nRotationInterval * 60 * 1000;
		DWORD period = nRotationInterval * 60 * 1000;
		if (!CreateTimerQueueTimer(&hTimer, hTimerQueue, (WAITORTIMERCALLBACK)TimerRoutine, &arg, duetime, period, WT_EXECUTELONGFUNCTION))
		{
			write_text_to_log_file("CreateTimerQueueTimer failed = " + std::to_string(GetLastError()));
			return;
		}

		if (debug)
		{
			sprintf_s(buffer, sizeof(buffer), "hTimer after = %p", hTimer);
			write_text_to_log_file(buffer);
		}
	}

	void Stop_Rotation()
	{
		char buffer[255];

		if (debug)
		{
			sprintf_s(buffer, sizeof(buffer), "Entering Stop_Rotation hTimer = %p", hTimer);
			write_text_to_log_file(buffer);
		}

		if (hTimer != NULL)
		{
			if (!DeleteTimerQueueTimer(hTimerQueue, hTimer, NULL))
			{
				write_text_to_log_file("DeleteTimerQueueTimer failed = " + std::to_string(GetLastError()));
				return;
			}
			
			hTimer = NULL;
			rotating = false;

			if (debug)
			{
				write_text_to_log_file("Rotation stopped");
			}
		}
	}

	VOID CALLBACK TimerRoutine(PVOID lpParam, BOOLEAN TimerOrWaitFired)
	{
		if (lpParam == NULL)
		{
			write_text_to_log_file("TimerRoutine lpParam is NULL");
		}
 
		// set next receiver
		current_rx = (current_rx + 1) % num_frequencies;
		if (debug)
		{
			write_text_to_log_file("TimerRoutine current_rx = " + std::to_string(current_rx));
		}

		// set new frequency
		myRSP.SetFreq((double)frequencies[current_rx]);
	}

	void write_text_to_log_file( const std::string &text )
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		char buffer[30];

		sprintf_s(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

		std::ofstream log_file(szSkimSrvLog, std::ios_base::out | std::ios_base::app );

		log_file << buffer << ": " << text << std::endl;
	}

	void write_text_to_stream(const std::string &text)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		static char big_buff[10000];

		strcat(big_buff, text.c_str());
		strcat(big_buff, "\r\n");
		if (strlen(big_buff) >= 1000)
		{
			std::ofstream log_file(szSkimSrvLog, std::ios_base::out | std::ios_base::app);

			log_file << big_buff << ": " << text << std::endl;

			strcpy(big_buff, " ");
		}
	}

	void rt_exception(const std::string &text)
	{
		write_text_to_log_file(text);
		const char *error = text.c_str();
		if (gSet.pErrorProc != NULL) {
			(*gSet.pErrorProc)(gSet.THandle, (char*)error);
		}
				
		return;
	}

	int MessageBoxTimeout(HWND hWnd, LPCSTR lpText,
		LPCSTR lpCaption, UINT uType, WORD wLanguageId,
		DWORD dwMilliseconds)
	{
		static MSGBOXAAPI MsgBoxTOA = NULL;

		if (!MsgBoxTOA)
		{
			HMODULE hUser32 = GetModuleHandle(_T("user32.dll"));
			if (hUser32)
			{
				MsgBoxTOA = (MSGBOXAAPI)GetProcAddress(hUser32, "MessageBoxTimeoutA");
				//fall through to 'if (MsgBoxTOA)...'
			}
			else
			{
				//stuff happened, add code to handle it here 
				//(possibly just call MessageBox())
				return 0;
			}
		}

		if (MsgBoxTOA)
		{
			return MsgBoxTOA(hWnd, lpText, lpCaption,
				uType, wLanguageId, dwMilliseconds);
		}

		return 0;
	}
}
