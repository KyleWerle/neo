#include "cbase.h"
#include "neo_hud_cyberbrain.h"

#include "c_neo_player.h"
#include "iclientmode.h"
#include "igameevents.h"
#include "neo/neo_gunplay_crosshair.h"
#include "neo_ironsights.h"
#include "neo_ironsight_profile.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>
#include <algorithm>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using vgui::surface;

// The cyberbrain HUD (HUD-REDESIGN.md, "In game: the build"): each frame it senses, works out each receptor group's
// attention and place, and draws the surround ring and the groups, most present on top. Its layout lives in its
// HudLayout.res block (NHudCyberbrain).

ConVar cl_neo_hud_style("cl_neo_hud_style", "2", FCVAR_ARCHIVE,
	"The HUD's style: 0 = original (the stock NT panels), 1 = cyberbrain with the compact ring, 2 = cyberbrain with the"
	" ring on the body, 3 = the racer band.", true, 0, true, NEO_HUD_STYLE__COUNT - 1);
ConVar cl_neo_hud_kanji("cl_neo_hud_kanji", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's kanji beside its plate labels (needs a Japanese font: NHudCyberKanji in ClientScheme.res).",
	true, 0, true, 1);

DECLARE_NAMED_HUDELEMENT(CNEOHud_Cyberbrain, NHudCyberbrain);

NEO_HUD_ELEMENT_DECLARE_FREQ_CVAR(Cyberbrain, 0.0)

namespace NC = NeoCyberbrain;

static constexpr float CYBERBRAIN_REVEAL = 0.45f;		// seconds to come up on spawn
static int s_iCyberbrainDrawnFrame = -100;

static const NC::Senses *s_pPublished = nullptr;
static Color s_publishedColor;

const NC::Senses *NC::PublishedSenses(Color &color)
{
	color = s_publishedColor;
	return NeoCyberbrainShowing() ? s_pPublished : nullptr;
}

NeoHudStyle NeoHudStyleCurrent()
{
	return static_cast<NeoHudStyle>(clamp(cl_neo_hud_style.GetInt(), 0, NEO_HUD_STYLE__COUNT - 1));
}

bool NeoCyberbrainShowing()
{
	const NeoHudStyle style = NeoHudStyleCurrent();
	return (style == NEO_HUD_STYLE_COMPACT || style == NEO_HUD_STYLE_BODY) && s_iCyberbrainDrawnFrame >= gpGlobals->framecount - 1;
}

CNEOHud_Cyberbrain::CNEOHud_Cyberbrain(const char *pElementName, vgui::Panel *parent)
	: CHudElement(pElementName), EditablePanel(parent, pElementName)
{
	SetAutoDelete(true);
	m_iHideHudElementNumber = NEO_HUD_ELEMENT_HEALTH_THERMOPTIC_AUX;
	SetParent(parent ? parent : g_pClientMode->GetViewport());
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetVisible(true);
	SetHiddenBits(HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT);
}

void CNEOHud_Cyberbrain::ApplySchemeSettings(vgui::IScheme *pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);
	LoadControlSettings("scripts/HudLayout.res");
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetZPos(85);
	SetPaintBackgroundEnabled(false);
}

void CNEOHud_Cyberbrain::LevelInit()
{
	m_lastFrame = -1;
	m_senses = NC::Senses();
	NC::ResetCallouts();
}

// The ghost's enemy callouts, for the ring (the compass keeps its own).
void CNEOHud_Cyberbrain::Init()
{
	ListenForGameEvent("ghost_enemy_callout");
	ListenForGameEvent("round_start");
	ListenForGameEvent("player_team");
}

void CNEOHud_Cyberbrain::FireGameEvent(IGameEvent *pEvent)
{
	NC::CalloutEvent(pEvent);
}

bool CNEOHud_Cyberbrain::ShouldDraw()
{
	const NeoHudStyle style = NeoHudStyleCurrent();
	if (style != NEO_HUD_STYLE_COMPACT && style != NEO_HUD_STYLE_BODY)
	{
		return false;
	}
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	return pPlayer && pPlayer->IsAlive() && !pPlayer->IsObserver() && CHudElement::ShouldDraw();
}

void CNEOHud_Cyberbrain::Paint()
{
	PaintNeoElement();
	BaseClass::Paint();
}

void CNEOHud_Cyberbrain::UpdateStateForNeoHudElementDraw()
{
}

void CNEOHud_Cyberbrain::HomesOf(NC::Home homes[NC::GROUP__COUNT], NeoHudStyle style) const
{
	const auto v = [](int x, int y) { return Vector2D(static_cast<float>(x), static_cast<float>(y)); };
	if (style == NEO_HUD_STYLE_BODY)
	{
		homes[NC::GROUP_BODY] = { v(m_bodyRingFarX, m_bodyRingFarY), v(m_bodyRingNearX, m_bodyRingNearY), 1.0f };
		homes[NC::GROUP_MOTION] = { v(m_motionRingFarX, m_motionRingFarY), v(m_motionRingNearX, m_motionRingNearY), 0.6f };
	}
	else
	{
		homes[NC::GROUP_BODY] = { v(m_bodyFarX, m_bodyFarY), v(m_bodyNearX, m_bodyNearY), 1.0f };
		homes[NC::GROUP_MOTION] = { v(m_motionFarX, m_motionFarY), v(m_motionNearX, m_motionNearY), 0.6f };
	}
	homes[NC::GROUP_OPTICS] = { v(m_opticsFarX, m_opticsFarY), v(m_opticsNearX, m_opticsNearY), 0.7f };
	homes[NC::GROUP_WEAPON] = { v(m_weaponFarX, m_weaponFarY), v(m_weaponNearX, m_weaponNearY), 0.8f };
	homes[NC::GROUP_LINK] = { v(m_linkFarX, m_linkFarY), v(m_linkNearX, m_linkNearY), 0.5f };
}

void CNEOHud_Cyberbrain::DrawNeoHudElement()
{
	NEO_IRONSIGHT_PROFILE(NEO_PROFILE_HUD, "CNEOHud_Cyberbrain");
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (!ShouldDraw() || !pPlayer)
	{
		m_lastFrame = -1;
		return;
	}
	// Held back under a fade to black (the stock panels stay down meanwhile); comes up again after it.
	s_iCyberbrainDrawnFrame = gpGlobals->framecount;
	if (NeoHudFadedOut())
	{
		m_lastFrame = -1;
		return;
	}
	const NeoHudStyle style = NeoHudStyleCurrent();
	const float now = gpGlobals->realtime;
	const bool bBoot = m_lastFrame != gpGlobals->framecount - 1 || pPlayer->GetClass() != m_lastClass || style != m_lastStyle;
	const float dt = bBoot ? 0.0f : clamp(now - m_lastTime, 0.0f, 0.1f);
	m_lastFrame = gpGlobals->framecount;
	m_lastTime = now;
	m_lastClass = pPlayer->GetClass();
	m_lastStyle = style;

	NC::Sense(pPlayer, dt, now, bBoot, m_senses);
	s_pPublished = &m_senses;
	s_publishedColor = m_color;

	NC::Frame f;
	f.style = style;
	f.color = m_color;
	f.wide = m_resX;
	f.tall = m_resY;
	f.s = m_resY / 1080.0f;
	f.centre.Init(m_resX * 0.5f, m_resY * 0.5f);
	f.now = now;
	f.alpha = NeoSmoothStep((now - m_senses.spawnTime) / CYBERBRAIN_REVEAL) * NeoHudFadeVisible();
	static ConVarRef cl_righthand("cl_righthand");
	f.hand = cl_righthand.IsValid() && !cl_righthand.GetBool() ? -1 : 1;	// the layout follows the gun hand
	f.pen.scale = f.s;
	f.pSenses = &m_senses;
	f.pPlaces = m_places;

	NC::Home homes[NC::GROUP__COUNT];
	HomesOf(homes, style);
	NC::Attend(m_senses, homes, f, dt, bBoot, m_places);

	// The ring: small at the bottom centre, or the body's ground disc; it opens as you look down.
	const float down = clamp(m_senses.pitch / 60.0f, -0.5f, 1.0f);
	if (style == NEO_HUD_STYLE_BODY)
	{
		const float k = f.s * NC::LookOf(f, NC::GROUP_BODY).scale;
		f.ringCentre = m_places[NC::GROUP_BODY].pos + Vector2D(0.0f, 60.0f * k);
		f.ringRadii.Init(m_bodyRingRadius, m_bodyRingRadius * clamp(0.3f + 0.3f * down, 0.12f, 0.66f));
	}
	else
	{
		f.ringCentre.Init(f.centre.x, m_ringY - clamp(m_senses.pitch, -30.0f, 60.0f) * 1.5f * f.s);
		f.ringRadii.Init(m_ringRadius, m_ringRadius * clamp(0.24f + 0.45f * down, 0.12f, 0.66f));
	}

	NC::MeasureBrightness(pPlayer, f, dt, bBoot);
	NC::PaintBackings(f);
	NC::PaintChassis(f);
	// The compact ring sits under the groups; the ring on the body goes over it (the body stands in front of where
	// you're facing on it), outlined so it reads across the capsule.
	const bool bRingOnBody = style == NEO_HUD_STYLE_BODY;
	if (!bRingOnBody)
	{
		NC::PaintRing(NC::ForGroup(f, NC::BRIGHT_RING));
	}
	NC::Group order[NC::GROUP__COUNT] = { NC::GROUP_BODY, NC::GROUP_OPTICS, NC::GROUP_WEAPON, NC::GROUP_LINK, NC::GROUP_MOTION };
	std::sort(order, order + NC::GROUP__COUNT, [&](NC::Group a, NC::Group b) { return m_places[a].att < m_places[b].att; });
	for (const NC::Group g : order)
	{
		switch (g)
		{
		case NC::GROUP_BODY:	NC::PaintBody(NC::ForGroup(f, g)); break;
		case NC::GROUP_OPTICS:	NC::PaintOptics(NC::ForGroup(f, g)); break;
		case NC::GROUP_WEAPON:	NC::PaintWeapon(NC::ForGroup(f, g)); break;
		case NC::GROUP_MOTION:	NC::PaintMotion(NC::ForGroup(f, g)); break;
		default:			NC::PaintLink(NC::ForGroup(f, g)); break;
		}
	}
	if (bRingOnBody)
	{
		NC::Frame ring = NC::ForGroup(f, NC::BRIGHT_RING);
		ring.contrast = Max(ring.contrast, 0.8f);
		NeoGhostOutline(0.8f);
		NC::PaintRing(ring);
	}
	NeoGhostFlush();
	NeoGhostOutline(-1.0f);	// back to the setting for everything else
}
