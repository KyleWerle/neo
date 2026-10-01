#ifndef NEO_HUD_CYBERBRAIN_SPECTATE_H
#define NEO_HUD_CYBERBRAIN_SPECTATE_H
#ifdef _WIN32
#pragma once
#endif

#include "neo_hud_childelement.h"
#include "hudelement.h"
#include <vgui_controls/Panel.h>

// The cyberbrain's ring for a spectator (neo_cyberbrain_ring.cpp, PaintSpectatorRing) as a HUD element: full screen,
// drawn in the cyberbrain styles while you're dead or spectating, in the stock compass's place and with only what it
// shows.
class CNEOHud_CyberbrainSpectate : public CNEOHud_ChildElement, public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE(CNEOHud_CyberbrainSpectate, Panel);

public:
	CNEOHud_CyberbrainSpectate(const char *pElementName, vgui::Panel *parent = nullptr);

	virtual void ApplySchemeSettings(vgui::IScheme *pScheme) override;
	virtual void Paint() override;
	virtual bool ShouldDraw() override;

protected:
	virtual void UpdateStateForNeoHudElementDraw() override {}
	virtual void DrawNeoHudElement() override;
	virtual ConVar *GetUpdateFrequencyConVar() const override;

private:
	int m_resX = 0, m_resY = 0;
	int m_lastFrame = -1;
	float m_shownTime = 0.0f;

	CPanelAnimationVar(Color, m_color, "fgcolor", "228 238 240 255");
};

#endif // NEO_HUD_CYBERBRAIN_SPECTATE_H
