#include "cbase.h"
#include "neo_hud_competitive.h"

#include "c_neo_player.h"
#include "hud.h"
#include "iclientmode.h"
#include "neo_gamerules.h"
#include "neo/neo_competitive.h"
#include "neo/neo_cyberbrain.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>
#include <wctype.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using vgui::surface;

DECLARE_NAMED_HUDELEMENT(CNEOHud_Competitive, NHudCompetitive);

NEO_HUD_ELEMENT_DECLARE_FREQ_CVAR(Competitive, 0.0)

namespace NCo = NeoCompetitive;

static int s_iVitalsFrame = -100, s_iTeamFrame = -100;

static bool CompetitiveStyle()
{
	return NeoHudStyleCurrent() == NEO_HUD_STYLE_COMPETITIVE;
}

bool NeoCompetitiveVitalsShowing()
{
	return CompetitiveStyle() && s_iVitalsFrame >= gpGlobals->framecount - 1;
}

bool NeoCompetitiveTeamShowing()
{
	return CompetitiveStyle() && s_iTeamFrame >= gpGlobals->framecount - 1;
}

const Color NCo::WHITE(255, 255, 255, 255), NCo::FADED(255, 255, 255, 150), NCo::RED(255, 64, 64, 255);

static vgui::HFont FaceFont(NCo::Face face)
{
	static vgui::HFont s_fonts[NCo::FACE__COUNT] = { vgui::INVALID_FONT, vgui::INVALID_FONT, vgui::INVALID_FONT };
	static const char *s_names[NCo::FACE__COUNT] = { "NHudCompText", "NHudCompLarge", "NHudKillfeedIcons" };
	vgui::HFont &font = s_fonts[face];
	if (font == vgui::INVALID_FONT)
	{
		// The HUD's faces live in the client scheme, not the engine's default one.
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));
		if (pScheme)
			font = pScheme->GetFont(s_names[face], true);
	}
	return font;
}

// Lowercased as Print draws it (unless bKeepCase or the icons), into text.
static void Cased(const wchar_t *pText, NCo::Face face, bool bKeepCase, wchar_t (&text)[128])
{
	V_wcsncpy(text, pText, sizeof(text));
	if (!bKeepCase && face != NCo::FACE_ICONS)
	{
		for (wchar_t *p = text; *p; ++p)
			*p = static_cast<wchar_t>(towlower(*p));
	}
}

float NCo::Width(const wchar_t *pText, Face face, bool bKeepCase)
{
	const vgui::HFont font = FaceFont(face);
	if (font == vgui::INVALID_FONT || !pText || !pText[0])
		return 0.0f;
	wchar_t text[128];
	Cased(pText, face, bKeepCase, text);
	int wide, tall;
	surface()->GetTextSize(font, text, wide, tall);
	return static_cast<float>(wide);
}

float NCo::Height(Face face)
{
	const vgui::HFont font = FaceFont(face);
	return font == vgui::INVALID_FONT ? 0.0f : static_cast<float>(surface()->GetFontTall(font));
}

float NCo::Print(const Pen &pen, const wchar_t *pText, float x, float y, int align, Face face, const Color &c, bool bKeepCase)
{
	const vgui::HFont font = FaceFont(face);
	if (font == vgui::INVALID_FONT || !pText || !pText[0])
		return 0.0f;
	wchar_t text[128];
	Cased(pText, face, bKeepCase, text);
	const int count = V_wcslen(text);
	int wide, tall;
	surface()->GetTextSize(font, text, wide, tall);
	const int tx = RoundFloatToInt(x) - (align < 0 ? wide : align == 0 ? wide / 2 : 0), ty = RoundFloatToInt(y);
	surface()->DrawSetTextFont(font);
	surface()->DrawSetTextColor(0, 0, 0, c.a() * 3 / 4);
	surface()->DrawSetTextPos(tx + 1, ty + 1);
	surface()->DrawPrintText(text, count);
	surface()->DrawSetTextColor(c);
	surface()->DrawSetTextPos(tx, ty);
	surface()->DrawPrintText(text, count);
	return static_cast<float>(wide);
}

CNEOHud_Competitive::CNEOHud_Competitive(const char *pElementName, vgui::Panel *parent)
	: CHudElement(pElementName), Panel(parent, pElementName)
{
	SetAutoDelete(true);
	SetParent(parent ? parent : g_pClientMode->GetViewport());
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetVisible(true);
}

void CNEOHud_Competitive::ApplySchemeSettings(vgui::IScheme *pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetZPos(90);
	SetPaintBackgroundEnabled(false);
	SetFgColor(Color(0, 0, 0, 0));
	SetBgColor(Color(0, 0, 0, 0));
}

bool CNEOHud_Competitive::ShouldDraw()
{
	return CompetitiveStyle() && C_NEO_Player::GetLocalNEOPlayer() && NEORules() && CHudElement::ShouldDraw();
}

void CNEOHud_Competitive::Paint()
{
	PaintNeoElement();
	BaseClass::Paint();
}

void CNEOHud_Competitive::DrawNeoHudElement()
{
	if (!ShouldDraw())
		return;
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	const NCo::Pen pen = { m_resY / 1080.0f, m_resX, m_resY };
	const int hidden = NEORules()->GetHiddenHudElements();

	// The vitals as the stock panels hide: dead, spectating, the rules' bits, their own switches.
	if (pPlayer->IsAlive() && !pPlayer->IsObserver() && !gHUD.IsHidden(HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD))
	{
		static ConVarRef cl_neo_hud_hta_enabled("cl_neo_hud_hta_enabled"), cl_neo_hud_ammo_enabled("cl_neo_hud_ammo_enabled");
		s_iVitalsFrame = gpGlobals->framecount;
		NCo::PaintVitals(pen, !(hidden & NEO_HUD_ELEMENT_HEALTH_THERMOPTIC_AUX) && cl_neo_hud_hta_enabled.GetBool(),
			!(hidden & NEO_HUD_ELEMENT_AMMO) && cl_neo_hud_ammo_enabled.GetBool(), !(hidden & NEO_HUD_ELEMENT_COMPASS));
	}
	// The team side on a team, dead included; a spectator keeps the stock elements.
	const int team = GetLocalPlayerTeam();
	if (team == TEAM_JINRAI || team == TEAM_NSF)
	{
		s_iTeamFrame = gpGlobals->framecount;
		if (!(hidden & NEO_HUD_ELEMENT_ROUND_STATE))
		{
			NCo::PaintRound(pen);
			NCo::PaintSquad(pen);
		}
		if (!gHUD.IsHidden(HIDEHUD_MISCSTATUS))
			NCo::PaintFeed(pen);
	}
}
