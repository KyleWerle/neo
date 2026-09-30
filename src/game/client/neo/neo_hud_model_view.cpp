#include "cbase.h"
#include "neo_hud_model_view.h"
#include "c_neo_player.h"
#include "neo_gamerules.h"
#include "view.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace NeoHud
{
bool ReadObjective(C_NEO_Player *pPlayer, Objective &out)
{
	if (!NEORules() || !(NEORules()->GhostExists() || NEORules()->GetJuggernautMarkerPos() != vec3_origin))
		return false;
	out.pos = NEORules()->GetGameType() == NEO_GAME_TYPE_JGR ? NEORules()->GetJuggernautMarkerPos() : NEORules()->GetGhostPos();
	out.carrierTeam = NEORules()->GetGhosterTeam();
	out.bYours = pPlayer->IsObjective();
	return true;
}

bool ReadRange(C_NEO_Player *pPlayer, float &metres)
{
	Vector forward;
	AngleVectors(MainViewAngles(), &forward);
	trace_t tr;
	UTIL_TraceLine(MainViewOrigin(), MainViewOrigin() + forward * MAX_TRACE_LENGTH, MASK_SHOT, pPlayer, COLLISION_GROUP_NONE, &tr);
	metres = tr.startpos.DistTo(tr.endpos) * METERS_PER_INCH;
	return (tr.surface.flags & (SURF_SKY | SURF_SKY2D)) == 0;
}
} // namespace NeoHud
