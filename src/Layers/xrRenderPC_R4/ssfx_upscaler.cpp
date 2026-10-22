#include "stdafx.h"
#include "ssfx_upscaler.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

// Streamline 2.10.0 headers ( MIT, NVIDIAGameWorks/Streamline ), declarations only: every entry point is
// resolved from sl.interposer.dll with GetProcAddress
#include "../../3rd party/streamline/include/sl.h"
#include "../../3rd party/streamline/include/sl_dlss.h"
#include "../../3rd party/streamline/include/sl_reflex.h"
#include "../../xrEngine/sl_latency.h"
#include "../xrRenderDX10/StateManager/dx10SamplerStateCache.h"

// SSS24 upscalers: NVIDIA DLSS ( Streamline ), reconstructed from the SSS24 RC2 exe
// ( Streamline init 140a87770, options 140a769d0, evaluate 140a76af0, jitter table at Device+0x64 ).
// DLSS replaces the TAA on the main view. DLAA keeps one resolution; the quality modes build the main target
// at a lower render size ( SSS device 1400395f0 / 14003a170: 0.666, 0.58, 0.5, 0.33 of the display ) and
// switch to the display size after the upscaler ( CRenderTarget::ssfx_size_mode ), the position and heat
// buffers are upscaled for the passes after it ( SSS POS_UPSCALE, blender 140b711c0 ).
// Reflex / PC Latency ( SSS 140a766a0, 140a66620, markers in CRenderDevice::on_idle 140031320 and
// CLevel::OnFrame 1401a5630 ): the engine calls the sl_latency hooks, installed here once Streamline is up.

namespace ssfx_upscaler
{
namespace
{
	HMODULE s_sl_module = nullptr;
	bool s_sl_ready = false; // slInit + slSetD3DDevice done
	bool s_dlss_ok = false; // DLSS supported on this adapter
	bool s_reflex_ok = false; // Reflex low latency available

	PFun_slInit* p_slInit = nullptr;
	PFun_slShutdown* p_slShutdown = nullptr;
	PFun_slSetD3DDevice* p_slSetD3DDevice = nullptr;
	PFun_slIsFeatureSupported* p_slIsFeatureSupported = nullptr;
	PFun_slGetFeatureFunction* p_slGetFeatureFunction = nullptr;
	PFun_slGetNewFrameToken* p_slGetNewFrameToken = nullptr;
	PFun_slSetConstants* p_slSetConstants = nullptr;
	PFun_slSetTagForFrame* p_slSetTagForFrame = nullptr;
	PFun_slEvaluateFeature* p_slEvaluateFeature = nullptr;
	PFun_slDLSSSetOptions* p_slDLSSSetOptions = nullptr;
	PFun_slReflexGetState* p_slReflexGetState = nullptr;
	PFun_slReflexSetOptions* p_slReflexSetOptions = nullptr;
	PFun_slReflexSleep* p_slReflexSleep = nullptr;
	PFun_slPCLSetMarker* p_slPCLSetMarker = nullptr;

	sl::FrameToken* s_frame = nullptr; // this frame's token ( sl_latency::new_frame at the start of on_idle )
	const sl::ViewportHandle s_viewport(0u);
	u32 s_reflex_mode = u32(-1); // last ssfx_reflex value handed to Streamline
	u32 s_target_token = 0; // ssfx_upscaler_resolution the main target was built with ( 0 = native )

	// last options handed to DLSS, re-sent when any of them changes
	struct AppliedOptions
	{
		u32 width = 0, height = 0, preset = u32(-1), mode = u32(-1);
		bool hdr = false;
	} s_applied;

	Fvector2 s_jitter[72];

	LPCSTR result_name(sl::Result r)
	{
		switch (r)
		{
		case sl::Result::eOk: return "eOk";
		case sl::Result::eErrorIO: return "eErrorIO";
		case sl::Result::eErrorDriverOutOfDate: return "eErrorDriverOutOfDate";
		case sl::Result::eErrorOSOutOfDate: return "eErrorOSOutOfDate";
		case sl::Result::eErrorOSDisabledHWS: return "eErrorOSDisabledHWS";
		case sl::Result::eErrorDeviceNotCreated: return "eErrorDeviceNotCreated";
		case sl::Result::eErrorNoSupportedAdapterFound: return "eErrorNoSupportedAdapterFound";
		case sl::Result::eErrorAdapterNotSupported: return "eErrorAdapterNotSupported";
		case sl::Result::eErrorNoPlugins: return "eErrorNoPlugins";
		case sl::Result::eErrorVulkanAPI: return "eErrorVulkanAPI";
		case sl::Result::eErrorDXGIAPI: return "eErrorDXGIAPI";
		case sl::Result::eErrorD3DAPI: return "eErrorD3DAPI";
		case sl::Result::eErrorNRDAPI: return "eErrorNRDAPI";
		case sl::Result::eErrorNVAPI: return "eErrorNVAPI";
		case sl::Result::eErrorReflexAPI: return "eErrorReflexAPI";
		case sl::Result::eErrorNGXFailed: return "eErrorNGXFailed";
		case sl::Result::eErrorJSONParsing: return "eErrorJSONParsing";
		case sl::Result::eErrorMissingProxy: return "eErrorMissingProxy";
		case sl::Result::eErrorMissingResourceState: return "eErrorMissingResourceState";
		case sl::Result::eErrorInvalidIntegration: return "eErrorInvalidIntegration";
		case sl::Result::eErrorMissingInputParameter: return "eErrorMissingInputParameter";
		case sl::Result::eErrorNotInitialized: return "eErrorNotInitialized";
		case sl::Result::eErrorComputeFailed: return "eErrorComputeFailed";
		case sl::Result::eErrorInitNotCalled: return "eErrorInitNotCalled";
		case sl::Result::eErrorExceptionHandler: return "eErrorExceptionHandler";
		case sl::Result::eErrorInvalidParameter: return "eErrorInvalidParameter";
		case sl::Result::eErrorMissingConstants: return "eErrorMissingConstants";
		case sl::Result::eErrorDuplicatedConstants: return "eErrorDuplicatedConstants";
		case sl::Result::eErrorMissingOrInvalidAPI: return "eErrorMissingOrInvalidAPI";
		case sl::Result::eErrorCommonConstantsMissing: return "eErrorCommonConstantsMissing";
		case sl::Result::eErrorUnsupportedInterface: return "eErrorUnsupportedInterface";
		case sl::Result::eErrorFeatureMissing: return "eErrorFeatureMissing";
		case sl::Result::eErrorFeatureNotSupported: return "eErrorFeatureNotSupported";
		case sl::Result::eErrorFeatureMissingHooks: return "eErrorFeatureMissingHooks";
		case sl::Result::eErrorFeatureFailedToLoad: return "eErrorFeatureFailedToLoad";
		case sl::Result::eErrorFeatureWrongPriority: return "eErrorFeatureWrongPriority";
		case sl::Result::eErrorFeatureMissingDependency: return "eErrorFeatureMissingDependency";
		case sl::Result::eErrorFeatureManagerInvalidState: return "eErrorFeatureManagerInvalidState";
		case sl::Result::eErrorInvalidState: return "eErrorInvalidState";
		case sl::Result::eWarnOutOfVRAM: return "eWarnOutOfVRAM";
		default: return "unknown";
		}
	}

	template <typename T>
	bool resolve(T*& fn, LPCSTR name)
	{
		fn = reinterpret_cast<T*>(GetProcAddress(s_sl_module, name));
		if (!fn)
			Msg("! NV Streamline : %s missing in sl.interposer.dll", name);
		return fn != nullptr;
	}

	// Halton(2,3) minus 0.5 for the indices 1..72, same table as the SSS exe
	void build_jitter()
	{
		for (u32 i = 0; i < 72; ++i)
		{
			const u32 index = i + 1;
			float f = 1.f, r = 0.f;
			for (u32 n = index; n; n /= 2)
			{
				f *= 0.5f;
				r += f * float(n % 2);
			}
			s_jitter[i].x = r - 0.5f;
			f = 1.f;
			r = 0.f;
			for (u32 n = index; n; n /= 3)
			{
				f /= 3.f;
				r += f * float(n % 3);
			}
			s_jitter[i].y = r - 0.5f;
		}
	}

	// render scale of a ssfx_upscaler_resolution token ( SSS 14003a170 )
	float render_scale(u32 token)
	{
		switch (token)
		{
		case 2: return 0.666f; // quality
		case 3: return 0.58f; // balanced
		case 4: return 0.5f; // performance
		case 5: return 0.33f; // ultra performance
		default: return 1.f; // dlaa / native
		}
	}

	// the resolution token a main target built now would get
	u32 wanted_token()
	{
		if (!s_dlss_ok || ps_ssfx_upscaler != UPSCALER_DLSS || !RImplementation.o.ssfx_core || RImplementation.o.dx10_msaa)
			return 0;
		return render_scale(ps_ssfx_upscaler_resolution) < 1.f ? ps_ssfx_upscaler_resolution : 0;
	}

	sl::DLSSMode dlss_mode()
	{
		// follows the main target's render size, a changed token only applies once the targets are rebuilt
		switch (s_target_token)
		{
		case 2: return sl::DLSSMode::eMaxQuality;
		case 3: return sl::DLSSMode::eBalanced;
		case 4: return sl::DLSSMode::eMaxPerformance;
		case 5: return sl::DLSSMode::eUltraPerformance;
		default: return sl::DLSSMode::eDLAA;
		}
	}

	// position / heat upscaling and the plain stretch ( SSS blender 140b711c0 elements 3 and 4 )
	class CBlender_ssfx_upscale : public IBlender
	{
	public:
		virtual LPCSTR getComment() { return "SSS24 upscaling"; }
		virtual BOOL canBeDetailed() { return FALSE; }
		virtual BOOL canBeLMAPped() { return FALSE; }

		CBlender_ssfx_upscale() { description.CLS = 0; }
		virtual ~CBlender_ssfx_upscale() {}

		virtual void Compile(CBlender_Compile& C)
		{
			IBlender::Compile(C);
			switch (C.iElement)
			{
			case 0: // position + heat at the display resolution, temporally accumulated
				C.r_Pass("stub_screen_space", "ssfx_upscale_pos", FALSE, FALSE, FALSE);
				C.r_dx10Texture("s_pos", r2_RT_P);
				C.r_dx10Texture("s_pos_prev", r2_RT_upscaled_pos_pv);
				C.r_dx10Texture("s_heat", r2_RT_heat);
				C.r_dx10Texture("s_data_prev", r2_RT_upscaled_data_pv);
				C.r_dx10Texture("s_mv", r2_RT_ssfx_motion_vectors);
				C.r_dx10Texture("s_fullres_hud", r2_RT_ssfx_taa_data);
				C.r_dx10Sampler("smp_base");
				C.r_dx10Sampler("smp_nofilter");
				C.r_dx10Sampler("smp_rtlinear");
				C.r_dx10Sampler("smp_point");
				C.r_End();
				break;
			case 1: // the result becomes the next frame's history
				C.r_Pass("stub_screen_space", "ssfx_upscale_pos_pack", FALSE, FALSE, FALSE);
				C.r_dx10Texture("s_pos", r2_RT_upscaled_pos);
				C.r_dx10Texture("s_data", r2_RT_upscaled_heat);
				C.r_dx10Sampler("smp_nofilter");
				C.r_End();
				break;
			case 2: // first frame: the histories start from the current render size buffers
				C.r_Pass("stub_screen_space", "ssfx_upscale_pos_pack", FALSE, FALSE, FALSE);
				C.r_dx10Texture("s_pos", r2_RT_P);
				C.r_dx10Texture("s_data", r2_RT_heat);
				C.r_dx10Sampler("smp_nofilter");
				C.r_End();
				break;
			case 3: // the aliased image stretched to the display size ( sharpening shader at zero strength )
				C.r_Pass("stub_screen_space", "ssfx_taa_sharp", FALSE, FALSE, FALSE);
				C.r_dx10Texture("s_current", r2_RT_generic0);
				C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
				C.r_dx10Sampler("smp_point");
				C.r_dx10Sampler("smp_linear");
				C.r_dx10Sampler("smp_rtlinear");
				C.r_dx10Sampler("smp_nofilter");
				C.r_End();
				break;
			}
		}
	};

	// fullscreen quad in the display pixels, stub_screen_space maps it with screen_res ( Device size )
	u32 fill_quad(const ref_geom& geom, float w, float h)
	{
		u32 offset = 0;
		const u32 C = color_rgba(0, 0, 0, 255);
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, geom->vb_stride, offset);
		pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
		pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
		pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
		pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
		RCache.Vertex.Unlock(4, geom->vb_stride);
		return offset;
	}

	template <typename T>
	bool feature_function(sl::Feature feature, LPCSTR name, T*& fn)
	{
		void* p = nullptr;
		fn = (p_slGetFeatureFunction(feature, name, p) == sl::Result::eOk) ? reinterpret_cast<T*>(p) : nullptr;
		return fn != nullptr;
	}

	void apply_reflex(bool force)
	{
		if (!s_reflex_ok || !p_slReflexSetOptions)
			return;
		const u32 mode = ps_ssfx_reflex;
		if (!force && mode == s_reflex_mode)
			return;
		s_reflex_mode = mode;

		sl::ReflexOptions options{};
		options.mode = mode == 2 ? sl::ReflexMode::eLowLatencyWithBoost : (mode == 1 ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff);
		options.frameLimitUs = 0;
		options.useMarkersToOptimize = true;
		const sl::Result res = p_slReflexSetOptions(options);
		if (res != sl::Result::eOk)
			Msg("! NV Streamline : Unable to set the Reflex option [ %s ]", result_name(res));
		else
			Msg("- NV Streamline : Reflex set to [ %s ].", mode == 2 ? "ON+BOOST" : (mode == 1 ? "ON" : "OFF"));
	}

	// sl_latency hooks, main thread
	void hook_new_frame()
	{
		sl::FrameToken* token = nullptr;
		s_frame = (p_slGetNewFrameToken(token, nullptr) == sl::Result::eOk) ? token : nullptr;
		if (sl_latency::g_hooks.ping_pending)
		{
			sl_latency::g_hooks.ping_pending = false;
			if (s_frame && p_slPCLSetMarker)
				p_slPCLSetMarker(sl::PCLMarker::ePCLatencyPing, *s_frame);
		}
	}

	void hook_marker(u32 marker)
	{
		if (s_frame && p_slPCLSetMarker)
			p_slPCLSetMarker(sl::PCLMarker(marker), *s_frame);
	}

	void hook_sleep()
	{
		if (s_reflex_ok && s_frame && p_slReflexSleep)
			p_slReflexSleep(*s_frame);
	}

	void init_reflex(const sl::AdapterInfo* adapter)
	{
		feature_function(sl::kFeaturePCL, "slPCLSetMarker", p_slPCLSetMarker);

		if (adapter)
		{
			const sl::Result res = p_slIsFeatureSupported(sl::kFeatureReflex, *adapter);
			if (res != sl::Result::eOk)
			{
				Msg("! NV Streamline : Reflex is not supported [ %s ]", result_name(res));
				return;
			}
		}

		bool ok = feature_function(sl::kFeatureReflex, "slReflexGetState", p_slReflexGetState);
		ok &= feature_function(sl::kFeatureReflex, "slReflexSetOptions", p_slReflexSetOptions);
		ok &= feature_function(sl::kFeatureReflex, "slReflexSleep", p_slReflexSleep);
		if (!ok)
		{
			Msg("! NV Streamline : Reflex functions missing.");
			return;
		}

		sl::ReflexState* state = xr_new<sl::ReflexState>();
		const sl::Result res = p_slReflexGetState(*state);
		if (res != sl::Result::eOk)
			Msg("! NV Streamline : Reflex can't get the current state [ %s ]", result_name(res));
		else
		{
			s_reflex_ok = state->lowLatencyAvailable;
			sl_latency::g_hooks.stats_message = state->statsWindowMessage;
		}
		xr_delete(state);

		if (s_reflex_ok)
		{
			Msg("- NV Streamline : Reflex low Latency available.");
			apply_reflex(true);
		}
	}

	void apply_options(bool force)
	{
		if (!s_dlss_ok || !p_slDLSSSetOptions)
			return;

		// the main target's output size ( Device.dwWidth follows whichever target was bound last )
		u32 w = Device.dwWidth, h = Device.dwHeight;
		if (RImplementation.TargetMain && RImplementation.TargetMain->rt_scene_aa)
		{
			w = RImplementation.TargetMain->rt_scene_aa->dwWidth;
			h = RImplementation.TargetMain->rt_scene_aa->dwHeight;
		}
		const u32 preset = ps_ssfx_dlss_preset;
		const u32 mode = u32(dlss_mode());
		const bool hdr = RImplementation.o.dx11_hdr10 != 0;
		if (!force && s_applied.width == w && s_applied.height == h && s_applied.preset == preset && s_applied.mode == mode && s_applied.hdr == hdr)
			return;

		sl::DLSSOptions options{};
		options.mode = sl::DLSSMode(mode);
		options.outputWidth = w;
		options.outputHeight = h;
		options.preExposure = 1.f;
		options.exposureScale = 1.f;
		options.colorBuffersHDR = hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		options.dlaaPreset = sl::DLSSPreset(preset);
		options.qualityPreset = sl::DLSSPreset(preset);
		options.balancedPreset = sl::DLSSPreset(preset);
		options.performancePreset = sl::DLSSPreset(preset);
		options.ultraPerformancePreset = sl::DLSSPreset(preset);
		options.ultraQualityPreset = sl::DLSSPreset(preset);
		options.useAutoExposure = sl::Boolean::eTrue;
		options.alphaUpscalingEnabled = sl::Boolean::eFalse;

		const sl::Result res = p_slDLSSSetOptions(s_viewport, options);
		if (res != sl::Result::eOk)
			Msg("! NV Streamline : Failed to set DLSS options [ %s ]", result_name(res));
		else
			Msg("- NV Streamline : DLSS options [ mode %u, preset %u, %ux%u ]", mode, preset, w, h);

		s_applied.width = w;
		s_applied.height = h;
		s_applied.preset = preset;
		s_applied.mode = mode;
		s_applied.hdr = hdr;
	}
}

void init()
{
	build_jitter();

	if (s_sl_module || !RImplementation.o.ssfx_core)
		return;

	s_sl_module = LoadLibraryW(L"sl.interposer.dll");
	if (!s_sl_module)
	{
		Msg("- NV Streamline : sl.interposer.dll not found, DLSS disabled.");
		return;
	}

	bool ok = resolve(p_slInit, "slInit");
	ok &= resolve(p_slShutdown, "slShutdown");
	ok &= resolve(p_slSetD3DDevice, "slSetD3DDevice");
	ok &= resolve(p_slIsFeatureSupported, "slIsFeatureSupported");
	ok &= resolve(p_slGetFeatureFunction, "slGetFeatureFunction");
	ok &= resolve(p_slGetNewFrameToken, "slGetNewFrameToken");
	ok &= resolve(p_slSetConstants, "slSetConstants");
	ok &= resolve(p_slSetTagForFrame, "slSetTagForFrame");
	ok &= resolve(p_slEvaluateFeature, "slEvaluateFeature");
	if (!ok)
	{
		FreeLibrary(s_sl_module);
		s_sl_module = nullptr;
		return;
	}

	// same preferences as the SSS exe: DLSS + Reflex + PCL, frame based tagging, D3D11, its application id
	static const sl::Feature features[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeaturePCL };
	sl::Preferences pref{};
	pref.showConsole = false;
	pref.logLevel = sl::LogLevel::eOff;
	pref.flags = sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	pref.featuresToLoad = features;
	pref.numFeaturesToLoad = sizeof(features) / sizeof(features[0]);
	pref.applicationId = 231313132; // 0xdc98eec
	pref.engine = sl::EngineType::eCustom;
	pref.renderAPI = sl::RenderAPI::eD3D11;

	sl::Result res = p_slInit(pref, sl::kSDKVersion);
	if (res != sl::Result::eOk)
	{
		Msg("! NV Streamline : Initialization failed [ %s ]", result_name(res));
		return;
	}
	Msg("- NV Streamline : SDK %u.%u.%u Initialized", SL_VERSION_MAJOR, SL_VERSION_MINOR, SL_VERSION_PATCH);

	res = p_slSetD3DDevice(HW.pDevice);
	if (res != sl::Result::eOk)
	{
		Msg("! NV Streamline : Failed to set D3DDevice [ %s ]", result_name(res));
		return;
	}
	s_sl_ready = true;

	// the engine marks the frame from now on ( frame token, Reflex sleep, PC Latency markers )
	sl_latency::g_hooks.new_frame = &hook_new_frame;
	sl_latency::g_hooks.marker = &hook_marker;
	sl_latency::g_hooks.sleep = &hook_sleep;

	DXGI_ADAPTER_DESC desc{};
	sl::AdapterInfo adapter{};
	const bool have_adapter = HW.m_pAdapter && SUCCEEDED(HW.m_pAdapter->GetDesc(&desc));
	if (have_adapter)
	{
		adapter.deviceLUID = reinterpret_cast<uint8_t*>(&desc.AdapterLuid);
		adapter.deviceLUIDSizeInBytes = sizeof(LUID);
	}

	init_reflex(have_adapter ? &adapter : nullptr);

	if (have_adapter)
	{
		res = p_slIsFeatureSupported(sl::kFeatureDLSS, adapter);
		if (res != sl::Result::eOk)
		{
			Msg("! NV Streamline : DLSS is not supported [ %s ]", result_name(res));
			return;
		}
	}

	feature_function(sl::kFeatureDLSS, "slDLSSSetOptions", p_slDLSSSetOptions);

	s_dlss_ok = p_slDLSSSetOptions != nullptr;
	if (s_dlss_ok)
	{
		Msg("- NV Streamline : DLSS is available.");
		apply_options(true);
	}
}

void shutdown()
{
	if (!s_sl_module)
		return;

	sl_latency::g_hooks.new_frame = nullptr;
	sl_latency::g_hooks.marker = nullptr;
	sl_latency::g_hooks.sleep = nullptr;
	sl_latency::g_hooks.stats_message = 0;
	sl_latency::g_hooks.ping_pending = false;

	if (s_sl_ready && p_slShutdown)
	{
		const sl::Result res = p_slShutdown();
		if (res != sl::Result::eOk)
			Msg("! NV Streamline : Failed to shutdown [ %s ]", result_name(res));
		else
			Msg("- NV Streamline : Successful shutdown");
	}
	s_sl_ready = false;
	s_dlss_ok = false;
	s_reflex_ok = false;
	p_slDLSSSetOptions = nullptr;
	p_slReflexGetState = nullptr;
	p_slReflexSetOptions = nullptr;
	p_slReflexSleep = nullptr;
	p_slPCLSetMarker = nullptr;
	s_frame = nullptr;
	s_reflex_mode = u32(-1);
	s_applied = AppliedOptions();
	FreeLibrary(s_sl_module);
	s_sl_module = nullptr;
}

void reset()
{
	apply_options(true);
}

bool dlss_available()
{
	return s_dlss_ok;
}

bool active()
{
	return s_dlss_ok && ps_ssfx_upscaler == UPSCALER_DLSS && RImplementation.o.ssfx_core && !RImplementation.o.dx10_msaa;
}

Fvector2 jitter(u32 frame)
{
	return s_jitter[frame % 72];
}

void render_size(u32 display_w, u32 display_h, u32& w, u32& h)
{
	s_target_token = wanted_token();
	w = display_w;
	h = display_h;
	if (!s_target_token)
		return;
	const float scale = render_scale(s_target_token);
	w = _max(u32(float(display_w) * scale), 64u);
	h = _max(u32(float(display_h) * scale), 64u);
	Msg("- UPSCALING : Resolution [ %ux%u ] -> [ %ux%u ]", display_w, display_h, w, h);
}

bool target_outdated()
{
	return s_sl_ready && wanted_token() != s_target_token;
}

IBlender* create_blender()
{
	return xr_new<CBlender_ssfx_upscale>();
}

void begin_frame()
{
	if (!s_sl_ready)
		return;
	if (!s_frame)
		hook_new_frame(); // the engine hook has not run yet

	apply_reflex(false);
	if (active())
		apply_options(false);

	// SSS automatic mip bias ( 140a92b80 ): log2( render / display ) - 1 while DLSS runs, r__tf_mipbias otherwise
	float bias = ps_r__tf_Mipbias;
	const CRenderTarget* T = RImplementation.TargetMain;
	if (active() && ps_ssfx_upscaler_automipmap && T && T->DisplayWidth)
		bias = log2f(float(T->Width) / float(T->DisplayWidth)) - 1.f;
	SSManager.SetMipLODBias(bias);
}

bool evaluate(CRenderTarget* T)
{
	if (!active() || !T || !T->rt_scene_aa || !T->rt_Generic_0 || !T->rt_ssfx_motion_vectors || !T->rt_dlss_depth)
		return false;

	begin_frame();
	if (!s_frame)
		return false;

	const u32 w = T->rt_Generic_0->dwWidth, h = T->rt_Generic_0->dwHeight;
	const u32 ow = T->rt_scene_aa->dwWidth, oh = T->rt_scene_aa->dwHeight;

	// depth: the scene depth buffer is copied into a typeless target DLSS can read
	{
		ID3DResource* zb = nullptr;
		HW.pBaseZB->GetResource(&zb);
		if (zb)
		{
			HW.pContext->CopyResource(T->rt_dlss_depth->pSurface, zb);
			_RELEASE(zb);
		}
	}

	// constants ( matrices are left invalid like the SSS exe, DLSS only needs these )
	sl::Constants consts{};
	const Fvector2 j = jitter(Device.dwFrame);
	consts.jitterOffset = sl::float2(j.x, -j.y);
	consts.mvecScale = sl::float2(-1.f, -1.f); // the motion vectors are current - previous in UV
	consts.cameraPinholeOffset = sl::float2(0.f, 0.f);
	consts.cameraNear = VIEWPORT_NEAR;
	consts.cameraFar = g_pGamePersistent ? g_pGamePersistent->Environment().CurrentEnv->far_plane : 1000.f;
	consts.cameraFOV = deg2rad(Device.fFOV);
	consts.cameraAspectRatio = float(w) / float(h);
	consts.motionVectorsInvalidValue = -FLT_MAX;
	consts.depthInverted = sl::Boolean::eFalse;
	consts.cameraMotionIncluded = sl::Boolean::eTrue;
	consts.motionVectors3D = sl::Boolean::eFalse;
	consts.reset = sl::Boolean::eFalse;
	consts.minRelativeLinearDepthObjectSeparation = 40.f;
	sl::Result res = p_slSetConstants(consts, *s_frame, s_viewport);
	if (res != sl::Result::eOk)
	{
		Msg("! NV Streamline : Failed to set the DLSS constants [ %s ]", result_name(res));
		return false;
	}

	sl::Resource color(sl::ResourceType::eTex2d, T->rt_Generic_0->pSurface, 0);
	sl::Resource output(sl::ResourceType::eTex2d, T->rt_scene_aa->pSurface, 0);
	sl::Resource depth(sl::ResourceType::eTex2d, T->rt_dlss_depth->pSurface, 0);
	sl::Resource mvec(sl::ResourceType::eTex2d, T->rt_ssfx_motion_vectors->pSurface, 0);

	sl::Extent render_extent{};
	render_extent.width = w;
	render_extent.height = h;
	sl::Extent display_extent{};
	display_extent.width = ow;
	display_extent.height = oh;

	const sl::ResourceTag tags[] = {
		sl::ResourceTag(&color, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &render_extent),
		sl::ResourceTag(&output, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &display_extent),
		sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &render_extent),
		sl::ResourceTag(&mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &render_extent),
	};

	sl::CommandBuffer* cmd = reinterpret_cast<sl::CommandBuffer*>(static_cast<ID3D11DeviceContext*>(HW.pContext));
	res = p_slSetTagForFrame(*s_frame, s_viewport, tags, sizeof(tags) / sizeof(tags[0]), cmd);
	if (res != sl::Result::eOk)
	{
		Msg("! NV Streamline : Failed to tag the DLSS resources [ %s ]", result_name(res));
		return false;
	}

	const sl::BaseStructure* inputs[] = { &s_viewport };
	res = p_slEvaluateFeature(sl::kFeatureDLSS, *s_frame, inputs, 1, cmd);

	// Streamline restores the D3D state it touched, the backend cache still has to forget it
	RCache.Invalidate();

	if (res != sl::Result::eOk)
	{
		static u32 s_last_error_frame = 0;
		if (Device.dwFrame - s_last_error_frame > 600)
		{
			s_last_error_frame = Device.dwFrame;
			Msg("! NV Streamline : Failed the DLSS evaluation [ %s ]", result_name(res));
		}
		return false;
	}
	return true;
}
}

// SSS24: sharpening after the upscaler ( SSS 140bff3d0 ), the upscaled image is copied aside and sharpened
// back into scene_aa ( rt_upscaler_tmp has the scene_aa format and size )
void CRenderTarget::phase_ssfx_upscaler_sharp()
{
	if (ps_ssfx_upscaler_sharp <= 0.f || !rt_scene_aa || !rt_upscaler_tmp)
		return;

	const float w = float(rt_scene_aa->dwWidth);
	const float h = float(rt_scene_aa->dwHeight);

	HW.pContext->CopyResource(rt_upscaler_tmp->pSurface, rt_scene_aa->pSurface);

	set_viewport_size(HW.pContext, w, h);
	u_setrt(rt_scene_aa, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	const u32 Offset = ssfx_upscaler::fill_quad(g_combine, w, h);

	RCache.set_Element(s_ssfx_taa->E[3]);
	RCache.set_c("taa_setup", 0.f, 0.f, ps_ssfx_upscaler_sharp, 0.f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

// SSS24: the main target renders at its own size up to the upscaler, the display size after it ( post process,
// UI, present ). Only the device size, the base RT / depth and the viewport change, nothing else of SetActive.
void CRenderTarget::ssfx_size_mode(bool display)
{
	if (!m_upscaled || !rt_baseRT || !rt_baseZB)
		return;

	const u32 w = display ? DisplayWidth : Width;
	const u32 h = display ? DisplayHeight : Height;
	Device.dwWidth = w;
	Device.dwHeight = h;
	Device.fWidth_2 = float(w >> 1);
	Device.fHeight_2 = float(h >> 1);

	// the members keep the render size pair, SetActive re-binds them for the scene passes
	HW.pBaseRT = display ? HW.secret_pBaseRT : baseRT;
	HW.pBaseZB = display ? HW.secret_pBaseZB : baseZB;
	dwWidth = w;
	dwHeight = h;

	set_viewport_size(HW.pContext, float(w), float(h));
	RCache.set_RT(HW.pBaseRT, 0);
	RCache.set_RT(nullptr, 1);
	RCache.set_RT(nullptr, 2);
	RCache.set_RT(nullptr, 3);
	RCache.set_ZB(HW.pBaseZB);
}

// SSS24: position and heat at the display size for the passes after the upscaler ( SSS POS_UPSCALE )
void CRenderTarget::phase_ssfx_upscale_pos()
{
	if (!m_upscaled || !b_ssfx_upscale || !rt_upscaled_pos || !rt_upscaled_heat || !rt_upscaled_pos_pv || !rt_upscaled_data_pv)
		return;

	PIX_EVENT(SSFX_UPSCALE_POS);
	const float w = float(rt_upscaled_pos->dwWidth);
	const float h = float(rt_upscaled_pos->dwHeight);
	const u32 Offset = ssfx_upscaler::fill_quad(g_combine, w, h);

	set_viewport_size(HW.pContext, w, h);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);
	RCache.set_Geometry(g_combine);

	if (m_upscale_seed)
	{
		u_setrt(rt_upscaled_pos_pv, rt_upscaled_data_pv, nullptr, nullptr);
		RCache.set_Element(s_ssfx_upscale->E[2]);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
		m_upscale_seed = false;
	}

	u_setrt(rt_upscaled_pos, rt_upscaled_heat, nullptr, nullptr);
	RCache.set_Element(s_ssfx_upscale->E[0]);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	u_setrt(rt_upscaled_pos_pv, rt_upscaled_data_pv, nullptr, nullptr);
	RCache.set_Element(s_ssfx_upscale->E[1]);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

// SSS24: the render size image stretched into scene_aa, for frames DLSS did not upscale ( disabled until the
// targets are rebuilt, an evaluation error, the menu )
void CRenderTarget::phase_ssfx_upscale_stretch()
{
	if (!b_ssfx_upscale || !rt_scene_aa)
		return;

	const float w = float(rt_scene_aa->dwWidth);
	const float h = float(rt_scene_aa->dwHeight);
	const u32 Offset = ssfx_upscaler::fill_quad(g_combine, w, h);

	set_viewport_size(HW.pContext, w, h);
	u_setrt(rt_scene_aa, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);
	RCache.set_Element(s_ssfx_upscale->E[3]);
	RCache.set_c("taa_setup", 0.f, 0.f, 0.f, 0.f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}
