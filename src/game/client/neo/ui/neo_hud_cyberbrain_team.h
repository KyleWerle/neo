#ifndef NEO_HUD_CYBERBRAIN_TEAM_H
#define NEO_HUD_CYBERBRAIN_TEAM_H
#ifdef _WIN32
#pragma once
#endif

#include "neo_hud_childelement.h"
#include "hudelement.h"
#include <vgui_controls/Panel.h>

// The cyberbrain's score, squad list and kill feed (neo/neo_cyberbrain_team.h) as a HUD element: full screen, drawn
// in the cyberbrain styles while you're on a team, alive or dead.
class CNEOHud_CyberbrainTeam : public CNEOHud_ChildElement, public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE(CNEOHud_CyberbrainTeam, Panel);

public:
	CNEOHud_CyberbrainTeam(const char *pElementName, vgui::Panel *parent = nullptr);

	virtual void ApplySchemeSettings(vgui::IScheme *pScheme) override;
	virtual void Paint() override;
	virtual bool ShouldDraw() override;
	virtual void Init() override;
	virtual void VidInit() override;
	virtual void FireGameEvent(IGameEvent *pEvent) override;

protected:
	virtual void UpdateStateForNeoHudElementDraw() override {}
	virtual void DrawNeoHudElement() override;
	virtual ConVar *GetUpdateFrequencyConVar() const override;

private:
	int m_resX = 0, m_resY = 0;

	CPanelAnimationVar(Color, m_color, "fgcolor", "228 238 240 255");
};

#endif // NEO_HUD_CYBERBRAIN_TEAM_H
