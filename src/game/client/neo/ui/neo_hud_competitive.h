#ifndef NEO_HUD_COMPETITIVE_H
#define NEO_HUD_COMPETITIVE_H
#ifdef _WIN32
#pragma once
#endif

#include "neo_hud_childelement.h"
#include "hudelement.h"
#include <vgui_controls/Panel.h>

// The Competitive HUD (neo/neo_competitive.h) as a HUD element: full screen, the vitals while you're alive, the team
// side while you're on a team (dead included).
class CNEOHud_Competitive : public CNEOHud_ChildElement, public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE(CNEOHud_Competitive, Panel);

public:
	CNEOHud_Competitive(const char *pElementName, vgui::Panel *parent = nullptr);

	virtual void ApplySchemeSettings(vgui::IScheme *pScheme) override;
	virtual void Paint() override;
	virtual bool ShouldDraw() override;
	// The stock rounded box, for the painters (neo/neo_competitive.h's Box).
	void PaintBox(int x0, int y0, int x1, int y1, const Color &c, bool bFlushTop) const;

protected:
	virtual void UpdateStateForNeoHudElementDraw() override {}
	virtual void DrawNeoHudElement() override;
	virtual ConVar *GetUpdateFrequencyConVar() const override;

private:
	int m_resX = 0, m_resY = 0;
};

#endif // NEO_HUD_COMPETITIVE_H
