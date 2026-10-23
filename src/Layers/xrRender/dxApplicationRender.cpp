#include "stdafx.h"

#include "dxApplicationRender.h"
#include "../../xrEngine/GameFont.h"

void draw_multiline_text(CGameFont* F, float fTargetWidth, LPCSTR pszText);

void dxApplicationRender::Copy(IApplicationRender& _in)
{
	*this = *(dxApplicationRender*)&_in;
}

void dxApplicationRender::LoadBegin()
{
	ll_hGeom.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);
	sh_progress.create("hud\\default", "ui\\ui_actor_loadgame_screen");
	hLevelLogo_Add.create("hud\\default", "ui\\ui_actor_widescreen_sidepanels.dds");

	string_path jr_white;
	if (FS.exist(jr_white, "$game_textures$", "ui\\jr_ls_white", ".dds"))
		sh_jr_white.create("hud\\default", "ui\\jr_ls_white");

	ll_hGeom2.create(FVF::F_TL, RCache.Vertex.Buffer(),NULL);

	//Discord
	discord_gameinfo.loadscreen = true;

	// demonized: handled in separate thread
	/*if (psDeviceFlags2.test(rsDiscord))
		updateDiscordPresence();*/
}

void dxApplicationRender::destroy_loading_shaders()
{
	hLevelLogo.destroy();
	sh_progress.destroy();
	hLevelLogo_Add.destroy();
	sh_jr_white.destroy();
	hLevelLogoBlur.destroy();

	//Discord
	discord_gameinfo.loadscreen = false;
	discord_gameinfo.ex_update = true;

	// demonized: handled in separate thread
	/*if (psDeviceFlags2.test(rsDiscord))
	{
		discord_gameinfo.ex_update = true;
		updateDiscordPresence();
	}*/
}

void dxApplicationRender::setLevelLogo(LPCSTR pszLogoName)
{
	hLevelLogo.create("hud\\default", pszLogoName);

	string_path blur_name, blur_path;
	strconcat(sizeof(blur_name), blur_name, pszLogoName, "_blur");
	if (FS.exist(blur_path, "$game_textures$", blur_name, ".dds"))
		hLevelLogoBlur.create("hud\\default", blur_name);
	else
		hLevelLogoBlur.destroy();
}

void dxApplicationRender::KillHW()
{
	ZeroMemory(&HW, sizeof(CHW));
}

u32 calc_progress_color(u32, u32, int, int);
extern bool use_reshade;
extern void render_reshade_effects();

void dxApplicationRender::load_draw_internal(CApplication& owner)
{
#if defined(USE_DX10) || defined(USE_DX11)
	//	TODO: DX10: remove this???
	RImplementation.rmNormal();
	RCache.set_RT(HW.pBaseRT);
	RCache.set_ZB(HW.pBaseZB);
#endif	//	USE_DX10

#if defined(USE_DX10) || defined(USE_DX11)
	FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	HW.pContext->ClearRenderTargetView(RCache.get_RT(), ColorRGBA);
#else	//	USE_DX10
	CHK_DX(HW.pDevice->Clear(0,0,D3DCLEAR_TARGET,D3DCOLOR_ARGB(0,0,0,0),1,0));
#endif	//	USE_DX10

	if (use_reshade)
		render_reshade_effects();

	if (!sh_progress)
	{
		return;
	}

	if (sh_jr_white)
		load_draw_jr(owner);
	else
		load_draw_classic(owner);
}

void dxApplicationRender::load_draw_classic(CApplication& owner)
{

#if defined(USE_DX10) || defined(USE_DX11)
	//	TODO: DX10: remove this
	//	FLOAT ColorRGBA[4] = {0.0f, 0.0f, 1.0f, 0.0f};
	//	HW.pContext->ClearRenderTargetView( RCache.get_RT(), ColorRGBA);
	//	HW.pContext->ClearDepthStencilView( RCache.get_ZB(), D3D_CLEAR_DEPTH|D3D_CLEAR_STENCIL, 1.0f, 0);
#endif	//	USE_DX10

	float _w = (float)Device.dwWidth;
	float _h = (float)Device.dwHeight;
	bool b_ws = (_w / _h) > 1.34f;
	bool b_16x9 = b_ws && ((_w / _h) > 1.77f);
	float ws_k = (b_16x9) ? 0.75f : 0.8333f; //16:9 or 16:10
	float ws_w = b_ws ? (b_16x9 ? 171.0f : 102.6f) : 0.0f;

	float bw = 1024.0f;
	float bh = 768.0f;
	Fvector2 k;
	k.set(_w / bw, _h / bh);

	Fvector2 tsz;
	tsz.set(1024, 1024);
	Frect back_tex_coords;
	Frect back_coords;
	Fvector2 back_size;
	Fvector2 back_tex_size;

	static float offs = -0.5f;

	Fvector2 back_offset;
	if (b_ws)
		back_offset.set(ws_w * ws_k, 0.0f); //ws_w == 171
	else
		back_offset.set(0.0f, 0.0f);

	//progress bar

	back_tex_size.set(506, 4);
	back_size.set(506, 4);
	if (b_ws)
		back_size.x *= ws_k; //ws

	back_tex_coords.lt.set(0, 772);
	back_tex_coords.rb.add(back_tex_coords.lt, back_tex_size);

	back_coords.lt.set(260, 599);
	if (b_ws)
		back_coords.lt.x *= ws_k;
	back_coords.lt.add(back_offset);

	back_coords.rb.add(back_coords.lt, back_size);
	back_coords.lt.mul(k);
	back_coords.rb.mul(k);

	back_tex_coords.lt.x /= tsz.x;
	back_tex_coords.lt.y /= tsz.y;
	back_tex_coords.rb.x /= tsz.x;
	back_tex_coords.rb.y /= tsz.y;

	u32 Offset;
	u32 C = 0xffffffff;
	FVF::TL* pv = NULL;
	u32 v_cnt = 40;
	pv = (FVF::TL*)RCache.Vertex.Lock(2 * (v_cnt + 1), ll_hGeom2.stride(), Offset);
	FVF::TL* _pv = pv;
	float pos_delta = back_coords.width() / v_cnt;
	float tc_delta = back_tex_coords.width() / v_cnt;
	u32 clr = C;

	for (u32 idx = 0; idx < v_cnt + 1; ++idx)
	{
		clr = calc_progress_color(idx, v_cnt, owner.load_stage, owner.max_load_stage);
		pv->set(back_coords.lt.x + pos_delta * idx + offs, back_coords.rb.y + offs, 0 + EPS_S, 1, clr,
		        back_tex_coords.lt.x + tc_delta * idx, back_tex_coords.rb.y);
		pv++;
		pv->set(back_coords.lt.x + pos_delta * idx + offs, back_coords.lt.y + offs, 0 + EPS_S, 1, clr,
		        back_tex_coords.lt.x + tc_delta * idx, back_tex_coords.lt.y);
		pv++;
	}
	VERIFY(u32(pv-_pv)==2*(v_cnt+1));
	RCache.Vertex.Unlock(2 * (v_cnt + 1), ll_hGeom2.stride());

	RCache.set_Shader(sh_progress);
	RCache.set_Geometry(ll_hGeom2);
	RCache.Render(D3DPT_TRIANGLESTRIP, Offset, 2 * v_cnt);

	//background picture

	back_tex_size.set(1024, 768);
	back_size.set(1024, 768);
	if (b_ws)
		back_size.x *= ws_k; //ws

	back_tex_coords.lt.set(0, 0);
	back_tex_coords.rb.add(back_tex_coords.lt, back_tex_size);

	back_coords.lt.set(offs, offs);
	back_coords.lt.add(back_offset);
	back_coords.rb.add(back_coords.lt, back_size);

	back_coords.lt.mul(k);
	back_coords.rb.mul(k);
	draw_face(sh_progress, back_coords, back_tex_coords, tsz);

	if (b_ws) //draw additional frames (left&right)
	{
		//left
		back_size.set(ws_w * ws_k, 768.0f);

		if (b_16x9)
		{
			back_tex_coords.lt.set(0, 0);
			back_tex_coords.rb.set(128, 768);
		}
		else
		{
			back_tex_coords.lt.set(0, 0);
			back_tex_coords.rb.set(128, 768);
		}
		back_coords.lt.set(offs, offs);
		back_coords.rb.add(back_coords.lt, back_size);
		back_coords.lt.mul(k);
		back_coords.rb.mul(k);

		draw_face(hLevelLogo_Add, back_coords, back_tex_coords, tsz);

		//right
		if (b_16x9)
		{
			back_tex_coords.lt.set(128, 0);
			back_tex_coords.rb.set(256, 768);
		}
		else
		{
			back_tex_coords.lt.set(128, 0);
			back_tex_coords.rb.set(256, 768);
		}

		back_coords.lt.set(1024.0f - back_size.x + offs, offs);
		back_coords.rb.add(back_coords.lt, back_size);
		back_coords.lt.mul(k);
		back_coords.rb.mul(k);

		draw_face(hLevelLogo_Add, back_coords, back_tex_coords, tsz);
	}


	// Draw title
	VERIFY(owner.pFontSystem);
	owner.pFontSystem->Clear();
	owner.pFontSystem->SetColor(color_rgba(103, 103, 103, 255));
	owner.pFontSystem->SetAligment(CGameFont::alCenter);
	back_size.set(_w / 2, 622.0f * k.y);
	owner.pFontSystem->OutSet(back_size.x, back_size.y);
	owner.pFontSystem->OutNext(owner.ls_header);
	owner.pFontSystem->OutNext("");
	owner.pFontSystem->OutNext(owner.ls_tip_number);

	float fTargetWidth = 600.0f * k.x * (b_ws ? 0.8f : 1.0f);
	draw_multiline_text(owner.pFontSystem, fTargetWidth, owner.ls_tip);

	owner.pFontSystem->OnRender();

	//draw level-specific screenshot
	if (hLevelLogo)
	{
		Frect r;
		r.lt.set(0, 173);

		if (b_ws)
			r.lt.x *= ws_k;
		r.lt.add(back_offset);

		r.lt.x += offs;
		r.lt.y += offs;
		back_size.set(1024, 399);

		if (b_ws)
			back_size.x *= ws_k; //ws 0.625

		r.rb.add(r.lt, back_size);
		r.lt.mul(k);
		r.rb.mul(k);
		Frect logo_tex_coords;
		logo_tex_coords.lt.set(0, 0);
		logo_tex_coords.rb.set(1.0f, 0.77926f);

		draw_face(hLevelLogo, r, logo_tex_coords, Fvector2().set(1, 1));
	}
}

namespace
{
Frect jr_rect(float x0, float y0, float x1, float y1)
{
	Frect r;
	r.lt.set(x0, y0);
	r.rb.set(x1, y1);
	return r;
}

// Upper case for headings: ASCII and cp1251 Cyrillic.
void jr_upper_copy(char* dst, size_t dst_size, LPCSTR src)
{
	size_t i = 0;
	for (; src && src[i] && i + 1 < dst_size; ++i)
	{
		unsigned char c = (unsigned char)src[i];
		if (c >= 'a' && c <= 'z')
			c = (unsigned char)(c - 32);
		else if (c >= 0xE0)
			c = (unsigned char)(c - 32);
		else if (c == 0xB8)
			c = 0xA8;
		dst[i] = (char)c;
	}
	dst[i] = 0;
}
}

// A quad with one colour along its top edge and one along its bottom edge (vertical fades).
void dxApplicationRender::draw_quad(ref_shader& sh, const Frect& r, const Frect& uv, u32 color_top, u32 color_bottom)
{
	u32 Offset;
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, ll_hGeom.stride(), Offset);
	pv->set(r.lt.x, r.rb.y, color_bottom, uv.lt.x, uv.rb.y);
	pv++;
	pv->set(r.lt.x, r.lt.y, color_top, uv.lt.x, uv.lt.y);
	pv++;
	pv->set(r.rb.x, r.rb.y, color_bottom, uv.rb.x, uv.rb.y);
	pv++;
	pv->set(r.rb.x, r.lt.y, color_top, uv.rb.x, uv.lt.y);
	pv++;
	RCache.Vertex.Unlock(4, ll_hGeom.stride());

	RCache.set_Shader(sh);
	RCache.set_Geometry(ll_hGeom);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

// JR loading screen. Two layouts, picked by [jr_loadscreen] in system.ltx (the JR - Loading Screen mod):
//  band: the level picture as a full width cinematic band over a dark ambient copy of itself.
//  full: the picture covers the screen, with a dark fade at the bottom for the text.
// Then a thin progress line, the level name and the tip. Neutral palette with the eGUI tan accent.
void dxApplicationRender::load_draw_jr(CApplication& owner)
{
	const float W = (float)Device.dwWidth;
	const float H = (float)Device.dwHeight;
	const Frect full_uv = jr_rect(0.f, 0.f, 1.f, 1.f);

	LPCSTR S = "jr_loadscreen";
	const bool cfg = !!pSettings->section_exist(S);
	auto cfg_float = [&](LPCSTR key, float def) { return cfg && pSettings->line_exist(S, key) ? pSettings->r_float(S, key) : def; };
	// Texture aspect and the picture's rectangle inside it. The defaults fit the vanilla style shots
	// (2:1, a framed vignette baked into the top ~78%).
	const float kTexAspect = cfg_float("tex_aspect", 2.f);
	Fvector4 pic;
	pic.set(.04f, .07f, .96f, .70f);
	if (cfg && pSettings->line_exist(S, "uv_rect"))
		pic = pSettings->r_fvector4(S, "uv_rect");
	const float U0 = pic.x, V0 = pic.y, U1 = pic.z, V1 = pic.w;
	const bool full = cfg && pSettings->line_exist(S, "layout") && !xr_strcmp(pSettings->r_string(S, "layout"), "full");
	const float pic_aspect = (U1 - U0) * kTexAspect / _max(V1 - V0, .01f);
	const float scr_aspect = W / H;

	const u32 bg = color_rgba(10, 11, 11, 255);
	draw_quad(sh_jr_white, jr_rect(0.f, 0.f, W, H), full_uv, bg, bg);

	// The picture's uv rectangle cropped to cover a box of the given aspect.
	auto cover_uv = [&](float box_aspect) {
		Frect r = jr_rect(U0, V0, U1, V1);
		if (pic_aspect > box_aspect)
		{
			const float keep = (U1 - U0) * box_aspect / pic_aspect, mid = (U0 + U1) * .5f;
			r.lt.x = mid - keep * .5f;
			r.rb.x = mid + keep * .5f;
		}
		else
		{
			const float keep = (V1 - V0) * pic_aspect / box_aspect, mid = (V0 + V1) * .5f;
			r.lt.y = mid - keep * .5f;
			r.rb.y = mid + keep * .5f;
		}
		return r;
	};

	// Gap between the progress line and the level name, as a fraction of the screen height (text_gap).
	const float text_gap = H * cfg_float("text_gap", .03f);
	float bar_y, text_y;
	if (full)
	{
		if (hLevelLogo)
		{
			const u32 white = color_rgba(255, 255, 255, 255);
			draw_quad(hLevelLogo, jr_rect(0.f, 0.f, W, H), cover_uv(scr_aspect), white, white);
		}
		// Fades for the text: a light one at the top, a deep one over the lower half.
		const u32 top_dark = color_rgba(10, 11, 11, 120), clear = color_rgba(10, 11, 11, 0);
		draw_quad(sh_jr_white, jr_rect(0.f, 0.f, W, floorf(H * .14f)), full_uv, top_dark, clear);
		const float f0 = floorf(H * .40f), f1 = floorf(H * .70f);
		draw_quad(sh_jr_white, jr_rect(0.f, f0, W, f1), full_uv, clear, color_rgba(10, 11, 11, 205));
		draw_quad(sh_jr_white, jr_rect(0.f, f1, W, H), full_uv, color_rgba(10, 11, 11, 205), color_rgba(10, 11, 11, 245));
		bar_y = floorf(H * .70f);
		text_y = bar_y + text_gap;
	}
	else
	{
		// Ambient: the same picture covering the whole screen, dark and out of focus.
		const float a = cfg_float("ambient", .14f);
		const u32 dim_rgb = u32(255.f * _min(_max(a, 0.f), 1.f));
		if (hLevelLogoBlur)
		{
			// Pre-blurred copy of just the picture: cover fit over its whole uv range.
			Frect r = jr_rect(0.f, 0.f, 1.f, 1.f);
			if (pic_aspect > scr_aspect)
			{
				const float keep = scr_aspect / pic_aspect;
				r.lt.x = .5f - keep * .5f;
				r.rb.x = .5f + keep * .5f;
			}
			else
			{
				const float keep = pic_aspect / scr_aspect;
				r.lt.y = .5f - keep * .5f;
				r.rb.y = .5f + keep * .5f;
			}
			const u32 dim = color_rgba(dim_rgb, dim_rgb, dim_rgb, 255);
			draw_quad(hLevelLogoBlur, jr_rect(0.f, 0.f, W, H), r, dim, dim);
		}
		else if (hLevelLogo)
		{
			// No blurred copy: average jittered taps of the picture (layer k drawn at alpha 1/k gives the
			// mean of all taps), sampled from an inset window so no tap reaches the frame.
			const int taps = 32;
			const float radius = W * cfg_float("ambient_blur", .025f);
			const Frect base = cover_uv(scr_aspect);
			const float du = (base.rb.x - base.lt.x) / W, dv = (base.rb.y - base.lt.y) / H;
			Frect win = base;
			win.lt.x += radius * du;
			win.rb.x -= radius * du;
			win.lt.y += radius * dv;
			win.rb.y -= radius * dv;
			for (int k = 0; k < taps; ++k)
			{
				const float rr = radius * _sqrt((k + .5f) / taps), ang = k * 2.39996323f;
				const float ox = rr * _cos(ang) * du, oy = rr * _sin(ang) * dv;
				const u32 alpha = u32(255.f / float(k + 1) + .5f);
				const u32 c = color_rgba(dim_rgb, dim_rgb, dim_rgb, alpha);
				draw_quad(hLevelLogo, jr_rect(0.f, 0.f, W, H),
				          jr_rect(win.lt.x + ox, win.lt.y + oy, win.rb.x + ox, win.rb.y + oy), c, c);
			}
		}
		// Band: full width at the picture's aspect, at most band_max_height of the screen (cropped to fit).
		const float band_max = H * cfg_float("band_max_height", .62f);
		const float band_top = floorf(H * .045f);
		const float band_h = _min(W / pic_aspect, band_max);
		const Frect buv = cover_uv(W / band_h);
		const float band_bottom = band_top + band_h;
		if (hLevelLogo)
		{
			const float y1 = band_top + band_h * .10f;
			const float y2 = band_bottom - band_h * .32f;
			const float dv = (buv.rb.y - buv.lt.y) / band_h;
			const float v1y = buv.lt.y + (y1 - band_top) * dv, v2y = buv.lt.y + (y2 - band_top) * dv;
			const u32 clear = color_rgba(255, 255, 255, 0), solid = color_rgba(255, 255, 255, 255);
			draw_quad(hLevelLogo, jr_rect(0.f, band_top, W, y1), jr_rect(buv.lt.x, buv.lt.y, buv.rb.x, v1y), clear, solid);
			draw_quad(hLevelLogo, jr_rect(0.f, y1, W, y2), jr_rect(buv.lt.x, v1y, buv.rb.x, v2y), solid, solid);
			draw_quad(hLevelLogo, jr_rect(0.f, y2, W, band_bottom), jr_rect(buv.lt.x, v2y, buv.rb.x, buv.rb.y), solid, clear);
		}
		bar_y = floorf(band_bottom - band_h * .06f);
		text_y = bar_y + text_gap;
	}

	// Progress: a thin line.
	const float bar_h = _max(2.f, floorf(H / 480.f));
	const float bx0 = floorf(W * .2f), bx1 = floorf(W * .8f);
	float frac = owner.max_load_stage > 0 ? float(owner.load_stage + 1) / float(owner.max_load_stage) : 0.f;
	clamp(frac, 0.f, 1.f);
	const u32 track = color_rgba(255, 255, 255, 46);
	const u32 accent = color_rgba(214, 190, 140, 240);
	draw_quad(sh_jr_white, jr_rect(bx0, bar_y, bx1, bar_y + bar_h), full_uv, track, track);
	if (frac > 0.f)
		draw_quad(sh_jr_white, jr_rect(bx0, bar_y, bx0 + (bx1 - bx0) * frac, bar_y + bar_h), full_uv, accent, accent);

	// Text: level name, tip heading, tip.
	CGameFont* F = owner.pFontSystem;
	if (!F)
		return;
	CGameFont* T = owner.pFontLoadTitle ? owner.pFontLoadTitle : F;

	string256 title;
	jr_upper_copy(title, sizeof(title), owner.ls_level[0] ? owner.ls_level : owner.ls_header);
	float y = text_y;
	if (title[0])
	{
		T->Clear();
		T->SetColor(color_rgba(236, 236, 232, 255));
		T->SetAligment(CGameFont::alCenter);
		T->OutSet(W * .5f, y);
		T->OutNext("%s", title);
		T->OnRender();
		y += T->CurrentHeight_() * 1.2f;
	}

	string2048 label;
	jr_upper_copy(label, sizeof(label), owner.ls_tip_number);
	for (size_t n = xr_strlen(label); n && (label[n - 1] == ':' || label[n - 1] == ' '); --n)
		label[n - 1] = 0;
	if (label[0])
	{
		F->Clear();
		F->SetColor(color_rgba(214, 190, 140, 230));
		F->SetAligment(CGameFont::alCenter);
		F->OutSet(W * .5f, y);
		F->OutNext("%s", label);
		F->OnRender();
		y += F->CurrentHeight_() * 1.35f;
	}

	if (owner.ls_tip[0])
	{
		F->Clear();
		F->SetColor(color_rgba(190, 192, 188, 235));
		F->SetAligment(CGameFont::alCenter);
		F->OutSet(W * .5f, y);
		draw_multiline_text(F, W * .58f, owner.ls_tip);
		F->OnRender();
	}
}

void dxApplicationRender::draw_face(ref_shader& sh, Frect& coords, Frect& tex_coords, const Fvector2& tsz)
{
	u32 Offset;
	u32 C = 0xffffffff;
	FVF::TL* pv = NULL;

	tex_coords.lt.x /= tsz.x;
	tex_coords.lt.y /= tsz.y;
	tex_coords.rb.x /= tsz.x;
	tex_coords.rb.y /= tsz.y;

	pv = (FVF::TL*)RCache.Vertex.Lock(4, ll_hGeom.stride(), Offset);
	pv->set(coords.lt.x, coords.rb.y, C, tex_coords.lt.x, tex_coords.rb.y);
	pv++;
	pv->set(coords.lt.x, coords.lt.y, C, tex_coords.lt.x, tex_coords.lt.y);
	pv++;
	pv->set(coords.rb.x, coords.rb.y, C, tex_coords.rb.x, tex_coords.rb.y);
	pv++;
	pv->set(coords.rb.x, coords.lt.y, C, tex_coords.rb.x, tex_coords.lt.y);
	pv++;
	RCache.Vertex.Unlock(4, ll_hGeom.stride());

	RCache.set_Shader(sh);
	RCache.set_Geometry(ll_hGeom);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

u32 calc_progress_color(u32 idx, u32 total, int stage, int max_stage)
{
	float kk = (float(stage + 1) / float(max_stage)) * (total);
	float f = 1 / (exp((float(idx) - kk) * 0.5f) + 1.0f);

	return color_argb_f(f, 1.0f, 1.0f, 1.0f);
}

#define IsSpace(ch)       ((ch) == ' ' || (ch) == '\t' || (ch) == '\r' || (ch) == '\n' || (ch) == ',' || (ch) == '.' || (ch) == ':' || (ch) == '!')

void parse_word(LPCSTR str, CGameFont* font, float& length, LPCSTR& next_word)
{
	length = 0.0f;
	while (*str && !IsSpace(*str))
	{
		//		length  += font->GetCharTC(*str).z;
		length += font->SizeOf_(*str);
		++str;
	}
	next_word = (*str) ? str + 1 : str;
}

void draw_multiline_text(CGameFont* F, float fTargetWidth, LPCSTR pszText)
{
	if (!pszText || xr_strlen(pszText) == 0)
		return;

	LPCSTR ch = pszText;
	float curr_word_len = 0.0f;
	LPCSTR next_word = NULL;

	float curr_len = 0.0f;
	string512 buff;
	buff[0] = 0;
	while (*ch)
	{
		parse_word(ch, F, curr_word_len, next_word);
		if (curr_len + curr_word_len > fTargetWidth)
		{
			F->OutNext(buff);
			curr_len = 0.0f;
			buff[0] = 0;
		}
		else
		{
			curr_len += curr_word_len;
			strncpy_s(buff + xr_strlen(buff), sizeof(buff) - xr_strlen(buff), ch, next_word - ch);
			ch = next_word;
		}
		if (0 == *next_word) //end of text
		{
			strncpy_s(buff + xr_strlen(buff), sizeof(buff) - xr_strlen(buff), ch, next_word - ch);
			F->OutNext(buff);
			break;
		}
	}
}
