#pragma once

#include "studio.h"

// Crossfades the viewmodel between animations: Source cuts a viewmodel straight to a new sequence's first
// frame (the last shot into the empty idle, the reload starting), so the gun jumps. When the sequence changes,
// the last pose drawn is blended into the new one over cl_neo_viewmodel_anim_blend seconds (0.4). The same
// sequence restarting (the next shot) is not blended, nor a different model (a weapon switch).
class NeoViewmodelAnimBlend
{
public:
	// After the pose for this frame is set up (bones in boneMask), in the bones' local space.
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
