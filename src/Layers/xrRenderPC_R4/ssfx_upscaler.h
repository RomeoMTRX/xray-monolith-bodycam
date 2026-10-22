#pragma once

// SSS24 upscaler support ( NVIDIA DLSS through Streamline ).
// sl.interposer.dll is loaded at runtime from the exe folder, nothing links against it, so the exe keeps
// working when the Streamline dlls are missing. The main viewport only: the SVP keeps its own TAA.

class CRenderTarget;
class IBlender;

namespace ssfx_upscaler
{
	// ssfx_upscaler token values ( same as the SSS exe )
	enum : int
	{
		UPSCALER_OFF = 0,
		UPSCALER_FSR = 1,
		UPSCALER_DLSS = 2,
	};

	void init(); // CRender::create, the D3D device exists ( SSS core only )
	void shutdown(); // CRender::destroy
	void reset(); // device reset, re-applies the options for the new output size

	bool dlss_available();

	// the main viewport runs DLSS this frame instead of the TAA
	bool active();

	// Halton(2,3) sub-pixel jitter for this frame, pixels in [-0.5, 0.5]
	Fvector2 jitter(u32 frame);

	// render size of a new main target for this display size ( lower for the DLSS quality modes )
	void render_size(u32 display_w, u32 display_h, u32& w, u32& h);

	// the upscaler / resolution settings ask for another render size than the main target has ( vid_restart )
	bool target_outdated();

	// position / heat upscaling + stretch blender for an upscaled main target
	IBlender* create_blender();

	// once per frame before the scene renders: new frame token, options when the console changed them
	void begin_frame();

	// color ( rt_Generic_0 ), depth, motion vectors -> rt_scene_aa, false when nothing was written
	bool evaluate(CRenderTarget* T);
}
