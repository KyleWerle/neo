#pragma once

#include "studio.h"

// Crossfades the viewmodel into a new sequence (the empty idle after the last shot, a reload starting) instead of
// cutting to its first frame. The same sequence restarting (the next shot) is not blended, nor a different model.
class NeoViewmodelAnimBlend
{
public:
	// After this frame's pose is set up (the bones in boneMask), in the bones' local space.
	void Apply(CStudioHdr *hdr, int sequence, Vector pos[], Quaternion q[], int boneMask);

private:
	const studiohdr_t *m_pModel = nullptr;
	int m_iSequence = -1;
	float m_flBlendStart = -1.0f;
	bool m_bHasLast = false;
	bool m_bBlending = false;
	Vector m_lastPos[MAXSTUDIOBONES];
	Quaternion m_lastQ[MAXSTUDIOBONES];
	Vector m_fromPos[MAXSTUDIOBONES];
	Quaternion m_fromQ[MAXSTUDIOBONES];
};
