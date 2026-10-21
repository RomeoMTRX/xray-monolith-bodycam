#pragma once

// SSS24: NVIDIA Reflex / PC Latency frame markers. The engine and the game only call these, the renderer
// installs the hooks once Streamline is loaded ( xrRenderPC_R4/ssfx_upscaler.cpp ), so nothing happens
// when sl.interposer.dll is missing. Marker values are sl::PCLMarker.

namespace sl_latency
{
	enum : u32
	{
		SimulationStart = 0,
		SimulationEnd = 1,
		RenderSubmitStart = 2,
		RenderSubmitEnd = 3,
		PresentStart = 4,
		PresentEnd = 5,
		PCLatencyPing = 8,
	};

	struct hooks
	{
		void (*new_frame)() = nullptr; // start of the frame: new frame token, pending latency ping
		void (*marker)(u32 marker) = nullptr;
		void (*sleep)() = nullptr; // Reflex sleep, before the simulation
		u32 stats_message = 0; // PC Latency stats window message ( 0 = none )
		bool ping_pending = false; // main thread only ( message loop -> next frame )
	};

	extern ENGINE_API hooks g_hooks;

	inline void new_frame()
	{
		if (g_hooks.new_frame)
			g_hooks.new_frame();
	}

	inline void marker(u32 m)
	{
		if (g_hooks.marker)
			g_hooks.marker(m);
	}

	inline void sleep()
	{
		if (g_hooks.sleep)
			g_hooks.sleep();
	}

	inline void on_message(u32 message)
	{
		if (g_hooks.stats_message && message == g_hooks.stats_message)
			g_hooks.ping_pending = true;
	}

	// simulation start / end around a scope
	struct simulation_scope
	{
		simulation_scope() { marker(SimulationStart); }
		~simulation_scope() { marker(SimulationEnd); }
	};
}
