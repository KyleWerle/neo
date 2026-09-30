#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "c_neo_player.h"
#include "c_neo_npc_dummy.h"
#include "c_team.h"
#include "neo_gamerules.h"
#include "neo_player_shared.h"
#include "weapon_ghost.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The ghost's uplink, in the weapon group's place while you carry it (Kyle's pick, A): NT's own g_gscan art, set up
// properly: the stock boot sequence played over the ghost's real bootup (sv_neo_ghost_delay_secs) rather than its own
// clock, then held on its last frame, as stock. Under it, chamfered cells: four for the bootup, then one red pip per
// enemy the beacons are showing on your screen, and the nearest of them. Only what the stock beacons show: nothing
// behind you, no count past what you can see. Holstered (the server not letting it work so), it dims and says so.

extern ConVar sv_neo_ctg_ghost_beacons_when_inactive;

namespace NeoCyberbrain
{
constexpr int UPLINK_FRAMES = 4, UPLINK_STEPS = 11, UPLINK_MAX_PIPS = 8;
constexpr float UPLINK_H = 40.0f;		// the art's height, pixels at 1080p

// The ghost you carry, or none.
static const C_WeaponGhost *CarriedGhost(C_NEO_Player *pPlayer)
{
	if (!pPlayer || !pPlayer->m_bCarryingGhost)
		return nullptr;
	return static_cast<const C_WeaponGhost *>(GetNeoWepWithBits(pPlayer, NEO_WEP_GHOST));
}

void SenseUplink(C_NEO_Player *pPlayer, Senses &out)
{
	out.bGhost = out.bGhostWorking = false;
	out.ghostContacts = 0;
	out.ghostNearest = -1.0f;
	const C_WeaponGhost *pGhost = CarriedGhost(pPlayer);
	if (!pGhost || !NEORules() || NEORules()->IsRoundOver() || (NEORules()->GetHiddenHudElements() & NEO_HUD_ELEMENT_GHOST_UPLINK_STATE))
		return;
	out.bGhost = true;
	auto *pActive = static_cast<C_NEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon());
	out.bGhostWorking = (pActive && pActive->IsGhost()) || sv_neo_ctg_ghost_beacons_when_inactive.GetBool();
	out.ghostBoot = pGhost->GetBootupProgress();
	if (!out.bGhostWorking || !pGhost->IsBootupCompleted())
		return;
	// The contacts the stock beacons draw on your screen now (the same test: in the ghost's range, then on screen).
	const int team = pPlayer->GetTeamNumber();
	C_Team *pEnemies = GetGlobalTeam(team == TEAM_JINRAI ? TEAM_NSF : TEAM_JINRAI);
	int wide, tall;
	vgui::surface()->GetScreenSize(wide, tall);
	const auto count = [&](CBaseEntity *pEnemy)
	{
		float distance;
		if (!pEnemy || !pGhost->BeaconRange(pEnemy, distance))
			return;
		int x, y;
		const Vector offset(0.0f, 0.0f, 48.0f);
		if (!GetVectorInScreenSpace(pEnemy->GetAbsOrigin(), x, y, const_cast<Vector *>(&offset)) || x < 0 || y < 0 || x >= wide || y >= tall)
			return;
		++out.ghostContacts;
		const float metres = distance * METERS_PER_INCH;
		out.ghostNearest = out.ghostNearest < 0.0f ? metres : Min(out.ghostNearest, metres);
	};
	for (int i = 0; pEnemies && i < pEnemies->GetNumPlayers(); ++i)
		count(pEnemies->GetPlayer(i));
	for (auto *pDummy = C_NEO_NPCDummy::GetList(); pDummy; pDummy = pDummy->GetNext())
		count(pDummy);
}

// The stock boot sequence: which frame, and from when (seconds of its own 3 s).
static int BootFrame(float progress)
{
	static const float s_starts[UPLINK_STEPS] = { 0.0f, 0.2f, 0.5f, 0.6f, 1.0f, 1.1f, 1.4f, 1.8f, 2.5f, 2.7f, 3.0f };
	static const int s_order[UPLINK_STEPS] = { 0, 1, 2, 3, 2, 0, 1, 2, 3, 0, 1 };
	const float t = progress * s_starts[UPLINK_STEPS - 1];
	int step = 0;
	while (step + 1 < UPLINK_STEPS && t >= s_starts[step + 1])
		++step;
	return s_order[step];
}

void PaintUplink(const Frame &f, const Local &L, float alpha, float labels)
{
	const Senses &s = *f.pSenses;
	static int s_frames[UPLINK_FRAMES] = { -1, -1, -1, -1 };
	static int s_texW = 1, s_texH = 1;
	if (s_frames[0] < 0)
	{
		for (int i = 0; i < UPLINK_FRAMES; ++i)
		{
			char path[32];
			V_snprintf(path, sizeof(path), "vgui/hud/ctg/g_gscan_0%d", i);
			s_frames[i] = vgui::surface()->CreateNewTextureID();
			vgui::surface()->DrawSetTextureFile(s_frames[i], path, true, false);
		}
		vgui::surface()->DrawGetTextureSize(s_frames[0], s_texW, s_texH);
	}
	const bool bWorking = s.bGhostWorking, bOnline = bWorking && s.ghostBoot >= 1.0f;
	const float a = alpha * (bWorking ? 1.0f : 0.4f);
	const float h = UPLINK_H, w = h * s_texW / Max(s_texH, 1);
	const Vector2D art0 = L.At(-w * 0.5f, -h * 0.5f - 6.0f), art1 = L.At(w * 0.5f, h * 0.5f - 6.0f);

	// The art, NT's own, red as stock; its brackets.
	NeoGhostFlush();
	const int frame = bOnline || !bWorking ? 1 : BootFrame(s.ghostBoot);
	const Color red = bWorking ? CRIT : f.color;
	vgui::surface()->DrawSetTexture(s_frames[frame]);
	vgui::surface()->DrawSetColor(red.r(), red.g(), red.b(), Alpha(f, 0.9f * a));
	vgui::surface()->DrawTexturedRect(RoundFloatToInt(art0.x), RoundFloatToInt(art0.y), RoundFloatToInt(art1.x), RoundFloatToInt(art1.y));
	const float bx = w * 0.5f + 6.0f, by0 = -h * 0.5f - 10.0f, by1 = h * 0.5f - 2.0f;
	for (int side = -1; side <= 1; side += 2)
	{
		Line(f, L.At(side * bx, by0), L.At(side * bx, by1), NEO_GHOST_MEDIUM, f.color, 0.7f * a);
		Line(f, L.At(side * bx, by0), L.At(side * (bx - 6.0f), by0), NEO_GHOST_MEDIUM, f.color, 0.7f * a);
		Line(f, L.At(side * bx, by1), L.At(side * (bx - 6.0f), by1), NEO_GHOST_MEDIUM, f.color, 0.7f * a);
	}

	// Under it: the bootup's cells, then the contacts' pips and the nearest.
	const float row = h * 0.5f + 6.0f;
	if (!bWorking)
	{
		Plate(f, L"HOLSTERED", L.At(0.0f, row + 6.0f).x, L.At(0.0f, row + 6.0f).y, 0, 0.8f * alpha);
	}
	else if (!bOnline)
	{
		CellStyle style;
		style.count = 4;
		style.bRow = true;
		style.chamfer = L.m > 0 ? 1 : -1;
		style.fill = CRIT;
		Cells(f, L.At(-bx, row), L.At(bx, row + 8.0f), s.ghostBoot, style, a);
	}
	else
	{
		const int pips = Min(s.ghostContacts, UPLINK_MAX_PIPS);
		for (int i = 0; i < pips; ++i)
		{
			const float x0 = -bx + i * 11.0f;
			CellStyle style;
			style.chamfer = L.m > 0 ? 1 : -1;
			style.fill = CRIT;
			const Vector2D p0 = L.At(x0, row), p1 = L.At(x0 + 8.0f, row + 9.0f);
			Cells(f, Vector2D(Min(p0.x, p1.x), p0.y), Vector2D(Max(p0.x, p1.x), p1.y), 1.0f, style, a);
		}
		if (s.ghostNearest >= 0.0f)
		{
			wchar_t nearest[16];
			V_snwprintf(nearest, ARRAYSIZE(nearest), L"%.0f M", s.ghostNearest);
			const Vector2D at = L.At(bx * static_cast<float>(L.m), row + 4.5f);
			Text(f, nearest, at.x, at.y, L.m > 0 ? -1 : 1, FONT_VALUE, f.color, a);
		}
	}
	if (labels > 0.02f)
	{
		const Vector2D pa = L.At(0.0f, by0 - 10.0f);
		Plate(f, L"UPLINK", pa.x, pa.y, 0, labels);
	}
}
} // namespace NeoCyberbrain
