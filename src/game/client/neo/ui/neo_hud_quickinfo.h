#ifndef NEO_HUD_QUICKINFO_H
#define NEO_HUD_QUICKINFO_H
#ifdef _WIN32
#pragma once
#endif

#include "neo_hud_childelement.h"
#include "hudelement.h"
#include <vgui_controls/Panel.h>

// The racer band (neo/neo_quickinfo.h) as a HUD element of its own: full screen, drawn in the Racer style over the
// crosshair (a scope's black borders included), in the crosshair's colour. The crosshair used to paint it, so the
// band went whenever the crosshair didn't draw (no weapon crosshair, paused, behind the camera).
class CNEOHud_QuickInfo : public CNEOHud_ChildElement, public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE(CNEOHud_QuickInfo, Panel);

public:
	CNEOHud_QuickInfo(const char *pElementName, vgui::Panel *parent = nullptr);

	virtual void ApplySchemeSettings(vgui::IScheme *pScheme) override;
	virtual void Paint() override;
	virtual bool ShouldDraw() override;

protected:
	virtual void UpdateStateForNeoHudElementDraw() override {}
	virtual void DrawNeoHudElement() override;
	virtual ConVar *GetUpdateFrequencyConVar() const override;

private:
	int m_resX = 0, m_resY = 0;
};

#endif // NEO_HUD_QUICKINFO_H
