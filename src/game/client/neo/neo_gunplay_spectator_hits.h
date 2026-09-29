#pragma once

// A player watched in first person: where their shots hit. Their seed never reaches this client (NT sends no
// fire-bullets temp entity), but the server sends every impact to everyone who can hear it (the "Impact" effect,
// CBaseEntity::ImpactTrace), walls and players alike, with where the bullet left from and where it hit. The ones
// that left from the watched player's eyes along their aim are theirs: their impact marks, and their knock and
// pivot toward where the shot really went. Shots into the sky send no impact (the knock stays random for those).

class Vector;

// Every "Impact" effect received (fx_hl2_impacts.cpp): the bullet's start and where it hit.
void NeoGunplaySpectatorImpact(const Vector &start, const Vector &hit);

// The watched player's hits not yet taken, as directions from where each shot left, oldest first (at most max);
// how many. Hits older than a moment are dropped.
int NeoGunplayTakeSpectatorHits(Vector *pDirections, int max);
