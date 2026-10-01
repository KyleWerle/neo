#include "cbase.h"
#include "neo_hud_cyberbrain_team.h"

#include "c_neo_player.h"
#include "c_team.h"
#include "hud.h"
#include "iclientmode.h"
#include "igameevents.h"
#include "neo_gamerules.h"
#include "neo/neo_cyberbrain_team.h"
#include "neo/neo_hud_style.h"
#include "neo_hud_profile.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using vgui::surface;

DECLARE_NAMED_HUDELEMENT(CNEOHud_CyberbrainTeam, NHudCyberbrainTeam);

NEO_HUD_ELEMENT_DECLARE_FREQ_CVAR(CyberbrainTeam, 0.0)

namespace NC = NeoCyberbrain;

CNEOHud_CyberbrainTeam::CNEOHud_CyberbrainTeam(const char *pElementName, vgui::Panel *parent)
	: CHudElement(pElementName), Panel(parent, pElementName)
{
	SetAutoDelete(true);
	SetParent(parent ? parent : g_pClientMode->GetViewport());
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetVisible(true);
}

void CNEOHud_CyberbrainTeam::ApplySchemeSettings(vgui::IScheme *pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetZPos(90);
	SetPaintBackgroundEnabled(false);
	SetFgColor(Color(0, 0, 0, 0));
	SetBgColor(Color(0, 0, 0, 0));
}

void CNEOHud_CyberbrainTeam::VidInit()
{
	NeoHud::ResetFeed();
}

// Every map starts its feed empty (the entries' lifetimes are on game time, which restarts with the map).
void CNEOHud_CyberbrainTeam::LevelInit()
{
	NeoHud::ResetFeed();
}

bool CNEOHud_CyberbrainTeam::ShouldDraw()
{
	// A spectator keeps the stock elements (the avatar strips, the spectator overlay, its selection).
	return NeoHudCyberbrainStyle(NeoHudStyleCurrent()) && NeoHudTeamReplaced() && NEORules() && CHudElement::ShouldDraw();
}

void CNEOHud_CyberbrainTeam::Paint()
{
	PaintNeoElement();
	BaseClass::Paint();
}

void CNEOHud_CyberbrainTeam::DrawNeoHudElement()
{
	if (!ShouldDraw())
	{
		return;
	}
	NEO_HUD_PROFILE(NEO_HUD_PROFILE_TEAM, "CNEOHud_CyberbrainTeam");
	NeoCyberbrainPlaceChat();

	NC::Frame f;
	f.style = NeoHudStyleCurrent();
	f.color = m_color;
	f.wide = m_resX;
	f.tall = m_resY;
	f.s = m_resY / 1080.0f;
	f.centre.Init(m_resX * 0.5f, m_resY * 0.5f);
	f.now = gpGlobals->realtime;
	f.alpha = NeoHudFadeVisible();
	f.hand = 1;					// the match isn't the gun's: nothing here mirrors
	f.pen.scale = f.s;
	f.pSenses = nullptr;
	f.pPlaces = nullptr;
	f.contrast = 0.6f;			// over the sky as often as not: text keeps its edge
	for (float &bright : f.bright)
		bright = 0.0f;

	NeoGhostOutline(0.6f);
	// The score and squad list follow the rules' round state bit, as the stock element does; the feed has none.
	if (!(NEORules()->GetHiddenHudElements() & NEO_HUD_ELEMENT_ROUND_STATE))
	{
		NC::ProbeOwner(NC::PROBE_SCORE);
		NC::PaintScore(f);
		NC::ProbeOwner(NC::PROBE_SQUAD);
		NC::PaintSquad(f);
	}
	if (!gHUD.IsHidden(HIDEHUD_MISCSTATUS))
	{
		NC::ProbeOwner(NC::PROBE_FEED);
		NC::PaintFeed(f);
	}
	NeoGhostFlush();
	NeoGhostOutline(-1.0f);
}
