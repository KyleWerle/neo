#include "cbase.h"
#include "neo_hud_cyberbrain_spectate.h"

#include "c_neo_player.h"
#include "iclientmode.h"
#include "hud.h"
#include "view.h"
#include "neo_gamerules.h"
#include "neo_hud_cyberbrain.h"
#include "neo/neo_cyberbrain_internal.h"
#include "neo/neo_hud_model_view.h"
#include "neo/neo_hud_model_team.h"
#include "neo/neo_hud_style.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using vgui::surface;

// Spectating, the stock compass gave way to nothing of ours: this is the cyberbrain's ring in its place (Kyle,
// 2026-10-01), in the compact ring's place and size in both styles (there's no body to stand it on). It shows what the
// stock compass shows and no more: the view's heading (whoever's eyes or camera it is), the objective, the ghost's
// callouts to a dead teammate, the range while the watched player aims.

DECLARE_NAMED_HUDELEMENT(CNEOHud_CyberbrainSpectate, NHudCyberbrainSpectate);

NEO_HUD_ELEMENT_DECLARE_FREQ_CVAR(CyberbrainSpectate, 0.0)

namespace NC = NeoCyberbrain;

static constexpr float SPECTATE_REVEAL = 0.45f;	// seconds to come up, as the cyberbrain does on spawn

extern ConVar cl_neo_hud_rangefinder_enabled;

CNEOHud_CyberbrainSpectate::CNEOHud_CyberbrainSpectate(const char *pElementName, vgui::Panel *parent)
	: CHudElement(pElementName), Panel(parent, pElementName)
{
	SetAutoDelete(true);
	m_iHideHudElementNumber = NEO_HUD_ELEMENT_COMPASS;	// the rules hide it as they hide the compass
	SetParent(parent ? parent : g_pClientMode->GetViewport());
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetVisible(true);
}

void CNEOHud_CyberbrainSpectate::ApplySchemeSettings(vgui::IScheme *pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetZPos(85);
	SetPaintBackgroundEnabled(false);
	SetFgColor(Color(0, 0, 0, 0));
	SetBgColor(Color(0, 0, 0, 0));
}

bool CNEOHud_CyberbrainSpectate::ShouldDraw()
{
	return NeoHudCyberbrainStyle(NeoHudStyleCurrent()) && NeoHudSpectating() && NEORules() && CHudElement::ShouldDraw();
}

void CNEOHud_CyberbrainSpectate::Paint()
{
	PaintNeoElement();
	BaseClass::Paint();
}

// Whose eyes the view is: the watched player in first person, else you (chase and free cameras: the camera's).
static C_NEO_Player *ViewPlayer(C_NEO_Player *pLocal)
{
	if (pLocal->GetObserverMode() == OBS_MODE_IN_EYE)
	{
		auto *pTarget = dynamic_cast<C_NEO_Player *>(pLocal->GetObserverTarget());
		if (pTarget && !pTarget->IsObserver())
			return pTarget;
	}
	return pLocal;
}

void CNEOHud_CyberbrainSpectate::DrawNeoHudElement()
{
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	const auto *pCyberbrain = GET_NAMED_HUDELEMENT(CNEOHud_Cyberbrain, NHudCyberbrain);
	if (!ShouldDraw() || !pLocal || !pCyberbrain || NeoHudFadedOut())
	{
		m_lastFrame = -1;
		return;
	}
	const float now = gpGlobals->realtime;
	if (m_lastFrame != gpGlobals->framecount - 1)
		m_shownTime = now;
	m_lastFrame = gpGlobals->framecount;
	C_NEO_Player *pView = ViewPlayer(pLocal);

	// The senses the compass reads, and only those.
	NC::Senses s;
	s.yaw = MainViewAngles()[YAW];
	s.pitch = MainViewAngles()[PITCH];
	NeoHud::Objective objective;
	s.bObjective = NeoHud::ReadObjective(pView, objective) && !objective.bYours;	// the watched carrier: it's them
	Color objectiveColour = NC::WARN;
	if (s.bObjective)
	{
		const Vector d = objective.pos - MainViewOrigin();
		s.objectiveYaw = RAD2DEG(atan2f(d.y, d.x));
		s.objectiveMetres = d.Length() * METERS_PER_INCH;
		// By who carries it: a dead teammate as their own HUD (ours, theirs); a spectator by the carrier's team.
		const int carrier = objective.carrierTeam, team = GetLocalPlayerTeam();
		if (carrier == TEAM_JINRAI || carrier == TEAM_NSF)
		{
			if (team == TEAM_JINRAI || team == TEAM_NSF)
				objectiveColour = carrier == team ? NC::TEAM_OURS : NC::CRIT;
			else
				objectiveColour = NeoHud::TeamColour(carrier);
		}
	}
	NC::SenseCallouts(now, s);
	s.bRange = cl_neo_hud_rangefinder_enabled.GetBool() && pView != pLocal && pView->IsInAim();
	if (s.bRange)
	{
		float metres;
		s.rangeMetres = NeoHud::ReadRange(pView, metres) ? metres : -1.0f;
	}

	NC::Frame f;
	f.style = NEO_HUD_STYLE_COMPACT;	// the compact ring in both styles: no body to stand on
	f.color = m_color;
	f.wide = m_resX;
	f.tall = m_resY;
	f.s = m_resY / 1080.0f;
	f.centre.Init(m_resX * 0.5f, m_resY * 0.5f);
	f.now = now;
	f.alpha = NeoSmoothStep((now - m_shownTime) / SPECTATE_REVEAL) * NeoHudFadeVisible();
	f.hand = 1;
	f.pen.scale = f.s;
	f.pSenses = &s;
	f.pPlaces = nullptr;
	for (float &bright : f.bright)
		bright = 0.0f;
	f.contrast = 0.6f;			// over any scene: text and strokes keep their edge, as the team side's
	f.listen = 1.0f;
	f.listenFor = -1.0f;
	// The compact ring's place, opening a little as the view looks down (as the cyberbrain's), held inside the screen.
	const float down = clamp(s.pitch / 60.0f, -0.5f, 1.0f);
	f.ringCentre.Init(f.centre.x, static_cast<float>(pCyberbrain->RingY()));
	f.ringRadii.Init(pCyberbrain->RingRadius(), pCyberbrain->RingRadius() * clamp(0.24f + 0.16f * down, 0.18f, 0.40f));
	Vector2D centre, half;
	NC::GroupExtent(f, NC::BRIGHT_RING, centre, half);
	f.ringCentre += NC::Inside(f, centre, half);

	NeoGhostOutline(0.6f);
	NC::ProbeOwner(NC::PROBE_RING);
	NC::PaintSpectatorRing(f, objectiveColour);
	NeoGhostFlush();
	NeoGhostOutline(-1.0f);
}
