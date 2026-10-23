#ifndef dxApplicationRender_included
#define dxApplicationRender_included
#pragma once

#include "..\..\Include\xrRender\ApplicationRender.h"
#include "..\..\xrEngine\x_ray.h"

class dxApplicationRender : public IApplicationRender
{
public:
	virtual void Copy(IApplicationRender& _in);

	virtual void LoadBegin();
	virtual void destroy_loading_shaders();
	virtual void setLevelLogo(LPCSTR pszLogoName);
	virtual void load_draw_internal(CApplication& owner);
	//	?????
	virtual void KillHW();
private:
	ref_shader hLevelLogo;
	ref_shader hLevelLogo_Add;
	ref_geom ll_hGeom;
	ref_geom ll_hGeom2;

	ref_shader sh_progress;
	// JR loading screen: a plain white texture shipped by the "JR - Loading Screen" mod. Without it the
	// classic layout is drawn.
	ref_shader sh_jr_white;
	void draw_face(ref_shader& sh, Frect& coords, Frect& tex_coords, const Fvector2& tex_size);
	void draw_quad(ref_shader& sh, const Frect& coords, const Frect& uv, u32 color_top, u32 color_bottom);
	void load_draw_classic(CApplication& owner);
	void load_draw_jr(CApplication& owner);
};

#endif	//	ApplicationRender_included
