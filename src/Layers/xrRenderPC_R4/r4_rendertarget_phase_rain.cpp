#include "stdafx.h"

void CRenderTarget::phase_rain()
{
	if (!RImplementation.o.dx10_msaa)
		u_setrt(rt_Color,NULL,NULL, HW.pBaseZB);
	else
		u_setrt(rt_Color,NULL,NULL, rt_MSAADepth->pZRT);
	//u_setrt	(rt_Normal,NULL,NULL,HW.pBaseZB);
	RImplementation.rmNormal();
}

void CRenderTarget::phase_ssfx_rain()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	set_viewport_size(HW.pContext, w / 8.0f, h / 8.0f);

	u_setrt(rt_ssfx_rain, 0, 0, NULL);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
	pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
	pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
	pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(!RImplementation.o.dx10_msaa ? s_ssfx_rain->E[0] : s_ssfx_rain->E[1]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	set_viewport_size(HW.pContext, w, h);
}
// SSS24: rain ripples for the water shader ( s_rainsplash ), 512x512 mip chain, only while it rains
void CRenderTarget::phase_ssfx_rain_ripples()
{
	if (!rt_ssfx_rain_ripples || !g_pGamePersistent || g_pGamePersistent->Environment().CurrentEnv->rain_density < 0.001f)
		return;

	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	set_viewport_size(HW.pContext, 512.0f, 512.0f);

	u_setrt(rt_ssfx_rain_ripples, 0, 0, NULL);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
	pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
	pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
	pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_ssfx_rain->E[2]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	HW.pContext->GenerateMips(rt_ssfx_rain_ripples->pTexture->get_SRView());

	set_viewport_size(HW.pContext, w, h);
}

// SSS24: water waves, a 256x256 height field ( waves_tmp ) then the normals built from it ( water_waves, mip chain )
void CRenderTarget::phase_ssfx_water_waves_sss24()
{
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	const float wind_anim = g_pGamePersistent->Environment().wind_anim.w;
	const float wind_velocity = g_pGamePersistent->Environment().CurrentEnv->wind_velocity;

	set_viewport_size(HW.pContext, 256.0f, 256.0f);

	for (u32 pass = 0; pass < 2; ++pass)
	{
		u_setrt(pass == 0 ? rt_ssfx_water_waves_tmp : rt_ssfx_water_waves, 0, 0, NULL);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
		pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
		pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
		pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// pass 0: height field ( E[5] ssfx_water_waves ), pass 1: normals ( E[4] ssfx_water_waves_build )
		RCache.set_Element(s_ssfx_water_blur->E[pass == 0 ? 5 : 4]);
		RCache.set_c("wind_setup", wind_anim, wind_velocity, 0.0f, 0.0f);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	HW.pContext->GenerateMips(rt_ssfx_water_waves->pTexture->get_SRView());

	set_viewport_size(HW.pContext, w, h);
}
