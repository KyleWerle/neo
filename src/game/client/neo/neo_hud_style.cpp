#include "cbase.h"
#include "neo_hud_style.h"
#include "neo_quickinfo.h"
#include "c_neo_player.h"
#include "view.h"
#include "ivieweffects.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static void NeoHudStyleChanged(IConVar *pVar, const char *pOldValue, float flOldValue);
ConVar cl_neo_hud_style("cl_neo_hud_style", "2", FCVAR_ARCHIVE,
	"The HUD's style: 0 = original (the stock NT panels), 1 = cyberbrain with the compact ring, 2 = cyberbrain with the"
	" ring on the body, 3 = the racer band, 4 = competitive (the original layout as lowercase text).", true, 0, true, NEO_HUD_STYLE__COUNT - 1,
	NeoHudStyleChanged);

NeoHudStyle NeoHudStyleCurrent()
{
	return static_cast<NeoHudStyle>(clamp(cl_neo_hud_style.GetInt(), 0, NEO_HUD_STYLE__COUNT - 1));
}

bool NeoHudCyberbrainStyle(NeoHudStyle style)
{
	return style == NEO_HUD_STYLE_COMPACT || style == NEO_HUD_STYLE_BODY;
}

// You alive, and in your own eyes (not spectating anyone).
static bool LocalInOwnEyes()
{
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	return pPlayer && pPlayer->IsAlive() && !pPlayer->IsObserver();
}

// You on a team, dead included.
static bool LocalOnTeam()
{
	const int team = GetLocalPlayerTeam();
	return (team == TEAM_JINRAI || team == TEAM_NSF) && C_NEO_Player::GetLocalNEOPlayer();
}

bool NeoHudVitalsReplaced()
{
	const NeoHudStyle style = NeoHudStyleCurrent();
	if (style == NEO_HUD_STYLE_RACER)
	{
		return NeoQuickInfoShowing();
	}
	return (NeoHudCyberbrainStyle(style) || style == NEO_HUD_STYLE_COMPETITIVE) && LocalInOwnEyes();
}

bool NeoHudCompassReplaced()
{
	const NeoHudStyle style = NeoHudStyleCurrent();
	return (NeoHudCyberbrainStyle(style) || style == NEO_HUD_STYLE_COMPETITIVE) && LocalInOwnEyes();
}

bool NeoHudUplinkReplaced()
{
	return NeoHudCyberbrainStyle(NeoHudStyleCurrent()) && LocalInOwnEyes();
}

bool NeoHudTeamReplaced()
{
	const NeoHudStyle style = NeoHudStyleCurrent();
	return (NeoHudCyberbrainStyle(style) || style == NEO_HUD_STYLE_COMPETITIVE) && LocalOnTeam();
}

static constexpr int STYLE_LISTENERS = 4;
using StyleListener = void (*)(NeoHudStyle);

// A function's own static, so a listener registering during static initialisation finds it built.
static StyleListener *StyleListeners()
{
	static StyleListener s_listeners[STYLE_LISTENERS] = {};
	return s_listeners;
}

void NeoHudOnStyleChange(StyleListener pfnChanged)
{
	StyleListener *pListeners = StyleListeners();
	for (int i = 0; i < STYLE_LISTENERS; ++i)
	{
		if (!pListeners[i] || pListeners[i] == pfnChanged)
		{
			pListeners[i] = pfnChanged;
			return;
		}
	}
	AssertMsg(false, "NeoHudOnStyleChange: no room for another listener");
}

static void NeoHudStyleChanged(IConVar *pVar, const char *pOldValue, float flOldValue)
{
	const NeoHudStyle style = NeoHudStyleCurrent();
	StyleListener *pListeners = StyleListeners();
	for (int i = 0; i < STYLE_LISTENERS && pListeners[i]; ++i)
	{
		pListeners[i](style);
	}
}

float NeoHudFadeVisible()
{
	static int s_frame = -1;
	static float s_visible = 1.0f;
	if (s_frame == gpGlobals->framecount)
	{
		return s_visible;
	}
	s_frame = gpGlobals->framecount;
	// A map's black screen overlay counts as fully faded (the tutorials hold env_screenoverlay's tools/toolsblack over
	// the view until you walk in: not a view fade, so the fade params don't see it).
	IMaterial *pOverlay = view ? view->GetScreenOverlayMaterial() : nullptr;
	if (pOverlay && !pOverlay->IsErrorMaterial() && V_stristr(pOverlay->GetName(), "toolsblack"))
	{
		s_visible = 0.0f;
		return s_visible;
	}
	byte r, g, b, a;
	bool bBlend;
	vieweffects->GetFadeParams(&r, &g, &b, &a, &bBlend);
	s_visible = 1.0f - a / 255.0f;
	return s_visible;
}

bool NeoHudFadedOut()
{
	return NeoHudFadeVisible() < NEO_HUD_FADE_HIDDEN;
}
