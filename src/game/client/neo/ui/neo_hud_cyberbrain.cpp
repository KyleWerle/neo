#include "cbase.h"
#include "neo_hud_cyberbrain.h"

#include "c_neo_player.h"
#include "iclientmode.h"
#include "hud.h"
#include "igameevents.h"
#include "neo/neo_hud_style.h"
#include "neo_ironsights.h"
#include "neo_ironsight_profile.h"
#include "neo_hud_profile.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>
#include <algorithm>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using vgui::surface;

// The cyberbrain HUD (HUD-REDESIGN.md, "In game: the build"): each frame it senses, works out each receptor group's
// attention and place, and draws the surround ring and the groups, most present on top. Its layout lives in its
// HudLayout.res block (NHudCyberbrain).

// The chat's place in the cyberbrain styles: up under the squad list, narrower (proportional: x, y, wide, tall), off
// the body group it covered at the bottom left (HUD-REDESIGN.md, "Layout check"). layout_check.py checks it.
static constexpr int CHAT_X = 10, CHAT_Y = 116, CHAT_W = 200, CHAT_H = 84;

static vgui::Panel *ChatPanel()
{
	CHudElement *pChat = gHUD.FindElement("CHudChat");
	return pChat ? dynamic_cast<vgui::Panel *>(pChat) : nullptr;
}

// Moves the chat each frame the cyberbrain shows (its own layout would put it back on a scheme change).
void NeoCyberbrainPlaceChat()
{
	static int s_placed = -1;	// the vitals and the team side both ask: once a frame
	if (s_placed == gpGlobals->framecount)
		return;
	s_placed = gpGlobals->framecount;
	vgui::Panel *pChat = ChatPanel();
	if (!pChat)
		return;
	int wide, tall;
	surface()->GetScreenSize(wide, tall);
	const float sc = tall / 480.0f;
	const int x = RoundFloatToInt(CHAT_X * sc), y = RoundFloatToInt(CHAT_Y * sc), w = RoundFloatToInt(CHAT_W * sc),
		h = RoundFloatToInt(CHAT_H * sc);
	int cx, cy, cw, ch;
	pChat->GetBounds(cx, cy, cw, ch);
	if (cx != x || cy != y || cw != w || ch != h)
		pChat->SetBounds(x, y, w, h);
}

// Leaving the cyberbrain styles, the chat goes back where its own layout puts it.
static void CyberbrainStyleChanged(NeoHudStyle style)
{
	if (!NeoHudCyberbrainStyle(style))
	{
		if (vgui::Panel *pChat = ChatPanel())
			pChat->InvalidateLayout(false, true);
	}
}
static const bool s_bListensForStyle = (NeoHudOnStyleChange(CyberbrainStyleChanged), true);
ConVar cl_neo_hud_kanji("cl_neo_hud_kanji", "1", FCVAR_ARCHIVE,
	"The cyberbrain HUD's kanji beside its plate labels (needs a Japanese font: NHudCyberKanji in ClientScheme.res).",
	true, 0, true, 1);

DECLARE_NAMED_HUDELEMENT(CNEOHud_Cyberbrain, NHudCyberbrain);

NEO_HUD_ELEMENT_DECLARE_FREQ_CVAR(Cyberbrain, 0.0)

namespace NC = NeoCyberbrain;

static constexpr float CYBERBRAIN_REVEAL = 0.45f;		// seconds to come up on spawn
static const NC::Senses *s_pPublished = nullptr;
static Color s_publishedColor;

const NC::Senses *NC::PublishedSenses(Color &color)
{
	color = s_publishedColor;
	return NeoCyberbrainShowing() ? s_pPublished : nullptr;
}

// Whether the cyberbrain draws its vitals now: asked directly (the style, you alive and in your own eyes), not "did
// it draw last frame", which let the stock panels flash through on the first frame after a spawn or whenever they
// painted before it (Kyle: stock HUD returning at weird parts of game start).
bool NeoCyberbrainShowing()
{
	if (!NeoHudCyberbrainStyle(NeoHudStyleCurrent()))
		return false;
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	return pPlayer && pPlayer->IsAlive() && !pPlayer->IsObserver();
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

bool CNEOHud_Cyberbrain::ShouldDraw()
{
	if (!NeoHudCyberbrainStyle(NeoHudStyleCurrent()))
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
	homes[NC::GROUP_LINK] = { v(m_linkFarX, m_linkFarY), v(m_linkNearX, m_linkNearY), 0.5f, true };
}

void CNEOHud_Cyberbrain::DrawNeoHudElement()
{
	NEO_HUD_PROFILE(NEO_HUD_PROFILE_VITALS, "CNEOHud_Cyberbrain");
	CNeoIronsightProfileScope ironsightHud(NEO_PROFILE_HUD);	// the ironsight bench's hud column
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	if (!ShouldDraw() || !pPlayer)
	{
		m_lastFrame = -1;
		return;
	}
	// Held back under a fade to black (the stock panels stay down meanwhile); comes up again after it.
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

	NeoCyberbrainPlaceChat();
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
		// It stays at its height and opens only a little as you look down (Kyle: the climb and the full opening were
		// too much and distracting; they also ran it into the chat and the keep-out).
		f.ringCentre.Init(f.centre.x, static_cast<float>(m_ringY));
		f.ringRadii.Init(m_ringRadius, m_ringRadius * clamp(0.24f + 0.16f * down, 0.18f, 0.40f));
		// Opening as you look down, it mustn't swing past the screen's bottom: held inside with its marks.
		Vector2D centre, half;
		NC::GroupExtent(f, NC::BRIGHT_RING, centre, half);
		f.ringCentre += NC::Inside(f, centre, half);
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
