#include "BaseVSShader.h"

#include "neo_hudbacking_vs30.inc"
#include "neo_hudbacking_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The HUD's backings (HUD-SYSTEM.md step 4): drawn in the HUD's 2D pass over a quarter-size copy of the finished
// frame the client makes at paint time (never a second render of the scene). The look rides on the vertices (see
// neo_hudbacking_vs30.fxc), so each backing can differ without a material each.

BEGIN_SHADER_FLAGS(Neo_HudBacking, "The HUD's backings: the frame behind, blurred and darkened.", SHADER_NOT_EDITABLE)

BEGIN_SHADER_PARAMS
END_SHADER_PARAMS

SHADER_INIT
{
	if (params[BASETEXTURE]->IsDefined())
	{
		LoadTexture(BASETEXTURE);
	}
}

SHADER_FALLBACK
{
	if (!g_pHardwareConfig->SupportsShaderModel_3_0())
	{
		return "Wireframe";
	}
	return 0;
}

SHADER_DRAW
{
	SHADOW_STATE
	{
		pShaderShadow->EnableTexture(SHADER_SAMPLER0, true);
		pShaderShadow->VertexShaderVertexFormat(VERTEX_POSITION | VERTEX_COLOR, 2, NULL, 0);
		pShaderShadow->EnableDepthWrites(false);
		pShaderShadow->EnableDepthTest(false);
		pShaderShadow->EnableCulling(false);
		pShaderShadow->EnableBlending(true);
		pShaderShadow->BlendFunc(SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA);

		DECLARE_STATIC_VERTEX_SHADER(neo_hudbacking_vs30);
		SET_STATIC_VERTEX_SHADER(neo_hudbacking_vs30);

		DECLARE_STATIC_PIXEL_SHADER(neo_hudbacking_ps30);
		SET_STATIC_PIXEL_SHADER(neo_hudbacking_ps30);
	}

	DYNAMIC_STATE
	{
		BindTexture(SHADER_SAMPLER0, BASETEXTURE);
		float texel[4] = { 1.0f / 256.0f, 1.0f / 256.0f, 0.0f, 0.0f };
		if (ITexture *pTexture = params[BASETEXTURE]->GetTextureValue())
		{
			texel[0] = 1.0f / Max(pTexture->GetActualWidth(), 1);
			texel[1] = 1.0f / Max(pTexture->GetActualHeight(), 1);
		}
		pShaderAPI->SetPixelShaderConstant(0, texel);

		DECLARE_DYNAMIC_VERTEX_SHADER(neo_hudbacking_vs30);
		SET_DYNAMIC_VERTEX_SHADER(neo_hudbacking_vs30);

		DECLARE_DYNAMIC_PIXEL_SHADER(neo_hudbacking_ps30);
		SET_DYNAMIC_PIXEL_SHADER(neo_hudbacking_ps30);
	}
	Draw();
}
END_SHADER
