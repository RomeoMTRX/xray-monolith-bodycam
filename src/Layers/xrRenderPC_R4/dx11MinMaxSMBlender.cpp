#include "stdafx.h"
#include "dx11MinMaxSMBlender.h"

void CBlender_createminmax::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:
		C.r_Pass("stub_notransform_2uv", "create_minmax_sm", false, FALSE, FALSE, FALSE);
		C.PassSET_ZB(FALSE,FALSE,FALSE);

		C.r_dx10Texture("s_smap", RImplementation.o.ssfx_core ? r2_RT_sun_smap_depth0 : r2_RT_smap_depth); // SSS24

		C.r_dx10Sampler("smp_nofilter");

		C.r_End();

		break;
	}
}
