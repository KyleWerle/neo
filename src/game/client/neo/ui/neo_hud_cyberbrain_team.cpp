#include "cbase.h"
#include "neo_hud_cyberbrain_team.h"

#include "c_neo_player.h"
#include "c_team.h"
#include "hud.h"
#include "iclientmode.h"
#include "igameevents.h"
#include "neo_gamerules.h"
#include "neo/neo_cyberbrain_team.h"
#include "neo/neo_gunplay_crosshair.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using vgui::surface;

DECLARE_NAMED_HUDELEMENT(CNEOHud_CyberbrainTeam, NHudCyberbrainTeam);

NEO_HUD_ELEMENT_DECLARE_FREQ_CVAR(CyberbrainTeam, 0.0)

namespace NC = NeoCyberbrain;

// Asked directly, as NeoCyberbrainShowing: the style and you on a team (dead included).
bool NeoCyberbrainTeamShowing()
{
	const NeoHudStyle style = NeoHudStyleCurrent();
	const int team = GetLocalPlayerTeam();
	return (style == NEO_HUD_STYLE_COMPACT || style == NEO_HUD_STYLE_BODY) && (team == TEAM_JINRAI || team == TEAM_NSF)
		&& C_NEO_Player::GetLocalNEOPlayer();
}

void NC::TeamSides(int &left, int &right)
{
	static ConVarRef cl_neo_hud_team_swap_sides("cl_neo_hud_team_swap_sides");
	const int local = GetLocalPlayerTeam();
	const bool bOnTeam = local == TEAM_JINRAI || local == TEAM_NSF;
	left = (cl_neo_hud_team_swap_sides.GetBool() && bOnTeam) ? local : TEAM_JINRAI;
	right = left == TEAM_JINRAI ? TEAM_NSF : TEAM_JINRAI;
}

Color NC::TeamColour(int team)
{
	return team == TEAM_JINRAI ? COLOR_NEO_GREEN : team == TEAM_NSF ? COLOR_NEO_BLUE : team == TEAM_SPECTATOR ? COLOR_NEO_ORANGE
		: COLOR_NEO_WHITE;
}

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

void CNEOHud_CyberbrainTeam::Init()
{
	// The death notice's events, for the feed's own copy.
	ListenForGameEvent("player_death");
	ListenForGameEvent("player_rankchange");
	ListenForGameEvent("ghost_capture");
	ListenForGameEvent("vip_extract");
	ListenForGameEvent("vip_death");
}

void CNEOHud_CyberbrainTeam::VidInit()
{
	NC::ResetFeed();
}

// Every map starts its feed empty (the entries' lifetimes are on game time, which restarts with the map).
void CNEOHud_CyberbrainTeam::LevelInit()
{
	NC::ResetFeed();
}

void CNEOHud_CyberbrainTeam::FireGameEvent(IGameEvent *pEvent)
{
	NC::FeedEvent(pEvent);
}

bool CNEOHud_CyberbrainTeam::ShouldDraw()
{
	const NeoHudStyle style = NeoHudStyleCurrent();
	const int team = GetLocalPlayerTeam();
	// A spectator keeps the stock elements (the avatar strips, the spectator overlay, its selection).
	return (style == NEO_HUD_STYLE_COMPACT || style == NEO_HUD_STYLE_BODY) && (team == TEAM_JINRAI || team == TEAM_NSF)
		&& C_NEO_Player::GetLocalNEOPlayer() && NEORules() && CHudElement::ShouldDraw();
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
		NC::PaintScore(f);
		NC::PaintSquad(f);
	}
	if (!gHUD.IsHidden(HIDEHUD_MISCSTATUS))
	{
		NC::PaintFeed(f);
	}
	NeoGhostFlush();
	NeoGhostOutline(-1.0f);
}
