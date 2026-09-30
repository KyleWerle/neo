#ifndef NEO_HUD_CYBERBRAIN_H
#define NEO_HUD_CYBERBRAIN_H
#ifdef _WIN32
#pragma once
#endif

#include "neo_hud_childelement.h"
#include "hudelement.h"
#include "neo/neo_cyberbrain_internal.h"
#include <vgui_controls/EditablePanel.h>

// The cyberbrain HUD (neo/neo_cyberbrain.h) as a HUD element: full screen, laid out from its HudLayout.res block
// (NHudCyberbrain), hidden as the health panel is (dead, spectating, the rules' health bit), drawn in the
// Compact and On the body styles.
class CNEOHud_Cyberbrain : public CNEOHud_ChildElement, public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE(CNEOHud_Cyberbrain, EditablePanel);

public:
	CNEOHud_Cyberbrain(const char *pElementName, vgui::Panel *parent = nullptr);

	virtual void ApplySchemeSettings(vgui::IScheme *pScheme) override;
	virtual void Paint() override;
	virtual bool ShouldDraw() override;
	virtual void LevelInit() override;

protected:
	virtual void UpdateStateForNeoHudElementDraw() override;
	virtual void DrawNeoHudElement() override;
	virtual ConVar *GetUpdateFrequencyConVar() const override;

private:
	void HomesOf(NeoCyberbrain::Home homes[NeoCyberbrain::GROUP__COUNT], NeoHudStyle style) const;

	NeoCyberbrain::Senses m_senses;
	NeoCyberbrain::Place m_places[NeoCyberbrain::GROUP__COUNT];
	int m_resX = 0, m_resY = 0;
	int m_lastFrame = -1;
	int m_lastClass = -1;
	int m_lastStyle = -1;
	float m_lastTime = 0.0f;

	CPanelAnimationVar(Color, m_color, "fgcolor", "228 238 240 255");
	// Each group's far home (deep in the periphery) and near one (closer to the focus), right-handed; the body's move
	// to the bottom centre in the On the body style, where the ring is its ground disc.
	CPanelAnimationVarAliasType(int, m_bodyFarX, "body_far_x", "249", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_bodyFarY, "body_far_y", "r48", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_bodyNearX, "body_near_x", "302", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_bodyNearY, "body_near_y", "r50", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_bodyRingFarX, "body_ring_far_x", "c-80", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_bodyRingFarY, "body_ring_far_y", "r54", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_bodyRingNearX, "body_ring_near_x", "c-72", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_bodyRingNearY, "body_ring_near_y", "r92", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_opticsFarX, "optics_far_x", "89", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_opticsFarY, "optics_far_y", "c0", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_opticsNearX, "optics_near_x", "222", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_opticsNearY, "optics_near_y", "c-9", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_weaponFarX, "weapon_far_x", "r209", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_weaponFarY, "weapon_far_y", "r129", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_weaponNearX, "weapon_near_x", "r250", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_weaponNearY, "weapon_near_y", "r140", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_linkFarX, "link_far_x", "r80", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_linkFarY, "link_far_y", "187", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_linkNearX, "link_near_x", "r142", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_linkNearY, "link_near_y", "204", "proportional_ypos");
	// The motion group, beside the body: left of it in Compact, right of it (toward the centre) On the body.
	CPanelAnimationVarAliasType(int, m_motionFarX, "motion_far_x", "138", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_motionFarY, "motion_far_y", "r48", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_motionNearX, "motion_near_x", "134", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_motionNearY, "motion_near_y", "r58", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_motionRingFarX, "motion_ring_far_x", "c36", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_motionRingFarY, "motion_ring_far_y", "r47", "proportional_ypos");
	CPanelAnimationVarAliasType(int, m_motionRingNearX, "motion_ring_near_x", "c18", "proportional_xpos");
	CPanelAnimationVarAliasType(int, m_motionRingNearY, "motion_ring_near_y", "r67", "proportional_ypos");
	// The compact ring: its centre's height and its radius.
	CPanelAnimationVarAliasType(int, m_ringY, "ring_y", "r42", "proportional_ypos");
	CPanelAnimationVarAliasType(float, m_ringRadius, "ring_radius", "84", "proportional_float");
	CPanelAnimationVarAliasType(float, m_bodyRingRadius, "body_ring_radius", "33", "proportional_float");
};

#endif // NEO_HUD_CYBERBRAIN_H
