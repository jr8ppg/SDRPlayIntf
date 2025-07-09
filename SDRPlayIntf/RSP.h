#pragma once
#define DLLIMPORT __declspec(dllimport)
#include <queue>
#include <sdrplay_api.h>

namespace SDRPlayIntf
{
	class RSP_Access
	{
		static std::queue <int> rsp_input_i;
		static std::queue <int> rsp_input_q;
	public:
		RSP_Access(void);
		void push(int i, int q);
		int front_and_pop_i();
		int front_and_pop_q();
		bool is_empty();
		long size();
	private:
	
	};

	class RSP
	{
	public:
		RSP(void);
		~RSP(void);
		int LoadApi(void);
		void StartRx();
		void StopRx(void);
		void GetDevices(void);
		void SetFreq(double);
		int nDecimateFactor;
		int nGainReduction;
		int nLNAstate;
		int nLastRSPIndex;
		int chosen_rsp_idx;
	private:
		sdrplay_api_Open_t              sdrplay_api_Open_fn;
		sdrplay_api_Close_t             sdrplay_api_Close_fn;
		sdrplay_api_GetDeviceParams_t   sdrplay_api_GetDeviceParams_fn;
		sdrplay_api_Init_t              sdrplay_api_Init_fn;
		sdrplay_api_Uninit_t            sdrplay_api_Uninit_fn;
		sdrplay_api_LockDeviceApi_t     sdrplay_api_LockDeviceApi_fn;
		sdrplay_api_UnlockDeviceApi_t   sdrplay_api_UnlockDeviceApi_fn;
		sdrplay_api_GetDevices_t        sdrplay_api_GetDevices_fn;
		sdrplay_api_SelectDevice_t      sdrplay_api_SelectDevice_fn;
		sdrplay_api_ReleaseDevice_t     sdrplay_api_ReleaseDevice_fn;
		sdrplay_api_GetErrorString_t    sdrplay_api_GetErrorString_fn;
		sdrplay_api_Update_t			sdrplay_api_Update_fn;

		sdrplay_api_DeviceT devices_found[10];
		sdrplay_api_CallbackFnsT cbparams;
		double _rspfreq;
		bool _rxstarted;

		std::string GetRspName(int hwVer);
	};
}