#pragma once

// Where a player watched in first person is hitting: their seeds never reach this client, but every bullet impact
// does, with where the bullet started, and the ones that left from their eyes along their aim are theirs.

class Vector;

void NeoSpectatorImpact(const Vector &start, const Vector &hit);

// The hits not yet taken, oldest first, as directions from where each shot left. Returns how many.
int NeoTakeSpectatorHits(Vector *pDirections, int max);
