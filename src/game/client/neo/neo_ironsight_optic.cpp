#include "cbase.h"
#include "neo_ironsight_optic.h"
#include "c_neo_player.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Whose view this is: the local player, or the player they spectate in first person (whose gun, aim
// and viewmodel are on screen then).
C_NEO_Player *NeoIronsightOpticViewPlayer()
{
	C_NEO_Player *pLocal = C_NEO_Player::GetLocalNEOPlayer();
	if (pLocal && pLocal->IsObserver() && pLocal->GetObserverMode() == OBS_MODE_IN_EYE)
	{
		return dynamic_cast<C_NEO_Player *>(pLocal->GetObserverTarget());
	}
	return pLocal;
}

bool NeoIronsightInThermals(const C_NEO_Player *pPlayer)
{
	return pPlayer && pPlayer->GetClass() == NEO_CLASS_SUPPORT && pPlayer->IsInVision();
}
