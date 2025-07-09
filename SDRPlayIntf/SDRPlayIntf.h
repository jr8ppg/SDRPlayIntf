//#ifdef HERMESINTF_EXPORTS
#define DLLEXPORT __declspec(dllexport) 
//#else
#define DLLIMPORT __declspec(dllimport) 
//#endif

#include <fstream>
//#include "Complex.h"

//#using <System.dll>
//
//using namespace System;
//using namespace System::Diagnostics;

#pragma once


namespace SDRPlayIntf
{

    // This class is exported from the HermesIntf.dll
#define RATE_48KHZ    0
#define RATE_96KHZ    1
#define RATE_192KHZ   2

		// IqProc must be called BLOCKS_PER_SEC times per second
#define BLOCKS_PER_SEC  93.75

#define MAX_RX_COUNT  8
//#define NUM_RX	1

#pragma pack(push, 16) 
		typedef struct {float Re, Im;} Cmplx;
		typedef Cmplx *CmplxA;
		typedef CmplxA *CmplxAA;
#pragma pack(pop) 

		// this callback procedure passes 7-channel I/Q data from the receivers for
		// the waterfall and decoding
		typedef void (__stdcall *tIQProc)(int RxHandle, CmplxAA Data);

		// this callback procedure passes I/Q data from the 1-st receiver in small chunks
		// for DSP processing, and receives processed audio back
		typedef void (__stdcall *tAudioProc)(int RxHandle, CmplxA InIq, CmplxA OutLR, int OutCount);

		// this callback procedure passes the total number of bytes to be sent to FPGA
		// and the number of already sent bytes. The client application may use it
		// to display a progress bar
		typedef void (__stdcall *tLoadProgressProc)(int RxHandle, int Current, int Total);

		// if an error occurs, call this callback procedure and pass it the error
		// message as a parameter, then stop the radio
		typedef void (__stdcall *tErrorProc)(int RxHandle, char *ErrText);

		//Optional. Call this procedure when the status bits change
		typedef void (__stdcall *tStatusBitsProc)(int RxHandle, unsigned char Bits);

		typedef struct {

			char *DeviceName;
			int   MaxRecvCount;
			float ExactRates[3];

		} SdrInfo, *PSdrInfo;


		typedef struct {

			int  THandle;
			int  RecvCount;
			int  RateID;
			BOOL LowLatency; //32-bit boolean

			tIQProc           pIQProc;
			tAudioProc        pAudioProc;
			tStatusBitsProc   pStatusBitProc;
			tLoadProgressProc pLoadProgressProc;
			tErrorProc        pErrorProc;

		} SdrSettings, *PSdrSettings;

		typedef int(__stdcall *MSGBOXAAPI)(IN HWND hWnd,
			IN LPCSTR lpText, IN LPCSTR lpCaption,
			IN UINT uType, IN WORD wLanguageId, IN DWORD dwMilliseconds);

		extern "C"
		{
			// return the radio name and the number of supported receivers.
			DLLEXPORT void __stdcall GetSdrInfo(PSdrInfo pInfo); 

			// Start receivers
			DLLEXPORT void __stdcall StartRx(PSdrSettings pSettings);

			// Stop receivers
			DLLEXPORT void __stdcall StopRx(void); 

			// Set Rx frequency
			DLLEXPORT void __stdcall SetRxFrequency(int Frequency, int Receiver);

			// Set ctrl bits (do nothing)
			DLLEXPORT void __stdcall SetCtrlBits(unsigned char Bits);

			// Set read port (do nothing)
			DLLEXPORT int __stdcall ReadPort(int PortNumber);
		};
		void write_text_to_log_file( const std::string &text );
		void write_text_to_stream(const std::string &text);
		void rt_exception(const std::string &text);
		void Start_Rotation(void);
		void Stop_Rotation(void);
		void SendDirect(int, int);
		VOID CALLBACK TimerRoutine(PVOID, BOOLEAN);
		int MessageBoxTimeout(HWND hWnd, LPCSTR lpText,
			LPCSTR lpCaption, UINT uType,
			WORD wLanguageId, DWORD dwMilliseconds);
		extern bool debug;
}

