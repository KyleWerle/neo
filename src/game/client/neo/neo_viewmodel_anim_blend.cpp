#include "cbase.h"
#include "neo_viewmodel_anim_blend.h"
#include "neo_ironsights.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_viewmodel_anim_blend("cl_neo_viewmodel_anim_blend", "0.4", FCVAR_ARCHIVE,
	"Seconds the gun blends from one animation into the next (the empty idle after the last shot, a reload"
	" starting) instead of jumping; 0 = off.", true, 0, true, 1);

void NeoViewmodelAnimBlend::Apply(CStudioHdr *hdr, int sequence, Vector pos[], Quaternion q[], int boneMask)
{
	if (!hdr)
	{
		return;
	}
	const int bones = Min(hdr->numbones(), MAXSTUDIOBONES);
	const studiohdr_t *pModel = hdr->GetRenderHdr();
	if (pModel != m_pModel)
	{
		// A different gun: nothing of the last one's pose carries over.
		m_pModel = pModel;
		m_iSequence = sequence;
		m_bHasLast = false;
		m_bBlending = false;
	}

	const float duration = NeoGunplayEnabled() ? cl_neo_viewmodel_anim_blend.GetFloat() : 0.0f;
	if (sequence != m_iSequence)
	{
		m_iSequence = sequence;
		if (m_bHasLast && duration > 0.0f)
		{
			// From the pose last drawn (itself mid-blend, if one was running), so the change is continuous.
			V_memcpy(m_fromPos, m_lastPos, sizeof(Vector) * bones);
			V_memcpy(m_fromQ, m_lastQ, sizeof(Quaternion) * bones);
			m_flBlendStart = gpGlobals->curtime;
			m_bBlending = true;
		}
	}

	if (m_bBlending)
	{
		const float t = (duration > 0.0f) ? (gpGlobals->curtime - m_flBlendStart) / duration : 1.0f;
		if (t >= 1.0f || t < 0.0f)
		{
			m_bBlending = false;
		}
		else
		{
			const float weight = NeoSmoothStep(t);
			for (int i = 0; i < bones; ++i)
			{
				if (!(hdr->boneFlags(i) & boneMask))
				{
					continue;
				}
				pos[i] = Lerp(weight, m_fromPos[i], pos[i]);
				Quaternion blended;
				QuaternionSlerp(m_fromQ[i], q[i], weight, blended);
				q[i] = blended;
			}
		}
	}

	for (int i = 0; i < bones; ++i)
	{
		if (hdr->boneFlags(i) & boneMask)
		{
			m_lastPos[i] = pos[i];
			m_lastQ[i] = q[i];
		}
	}
	m_bHasLast = true;
}
