#include "cbase.h"
#include "neo_hud_quickinfo.h"

#include "c_neo_player.h"
#include "hud.h"
#include "iclientmode.h"
#include "neo/neo_quickinfo.h"
#include "neo/neo_hud_style.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using vgui::surface;

DECLARE_NAMED_HUDELEMENT(CNEOHud_QuickInfo, NHudQuickInfo);

NEO_HUD_ELEMENT_DECLARE_FREQ_CVAR(QuickInfo, 0.0)

CNEOHud_QuickInfo::CNEOHud_QuickInfo(const char *pElementName, vgui::Panel *parent)
	: CHudElement(pElementName), Panel(parent, pElementName)
{
	SetAutoDelete(true);
	m_iHideHudElementNumber = NEO_HUD_ELEMENT_HEALTH_THERMOPTIC_AUX;
	SetParent(parent ? parent : g_pClientMode->GetViewport());
	// Hidden as the panels it replaces are (the health panel's bits).
	SetHiddenBits(HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT);
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetVisible(true);
}

void CNEOHud_QuickInfo::ApplySchemeSettings(vgui::IScheme *pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);
	surface()->GetScreenSize(m_resX, m_resY);
	SetBounds(0, 0, m_resX, m_resY);
	SetZPos(85);	// over the crosshair and a scope's borders
	SetPaintBackgroundEnabled(false);
	SetFgColor(Color(0, 0, 0, 0));
	SetBgColor(Color(0, 0, 0, 0));
}

bool CNEOHud_QuickInfo::ShouldDraw()
{
	return NeoHudStyleCurrent() == NEO_HUD_STYLE_RACER && C_NEO_Player::GetLocalNEOPlayer() && CHudElement::ShouldDraw();
}

void CNEOHud_QuickInfo::Paint()
{
	PaintNeoElement();
	BaseClass::Paint();
}

void CNEOHud_QuickInfo::DrawNeoHudElement()
{
	if (!ShouldDraw())
		return;
	NeoQuickInfoPaint(C_NEO_Player::GetLocalNEOPlayer(), NeoQuickInfoColour());
}
