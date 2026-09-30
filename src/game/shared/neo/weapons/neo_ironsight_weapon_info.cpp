#include <KeyValues.h>
#include "neo_weapon_parse.h"

// The index of a name in a list, or the first entry if it isn't there.
static int NameIndex(const char *pszName, const char *const *ppszNames, int count)
{
	for (int i = 0; i < count; ++i)
	{
		if (V_stricmp(pszName, ppszNames[i]) == 0)
		{
			return i;
		}
	}
	return 0;
}

void CNEOIronsightWeaponInfo::ParseIronsights(KeyValues *pKeyValuesData)
{
	m_bHasIronsight = false;
	// "IronsightDisabled" keeps a weapon on the classic zoom pose (and its crosshair) with ironsights on.
	KeyValues* pIronOffset = pKeyValuesData->GetBool("IronsightDisabled") ? nullptr : pKeyValuesData->FindKey("AimOffset");
	if (pIronOffset)
	{
		m_bHasIronsight = true;
		m_flVMIronFov = pIronOffset->GetFloat("fov", 55);

		m_vecVMIronPosOffset.x = pIronOffset->GetFloat("forward", 0);
		m_vecVMIronPosOffset.y = pIronOffset->GetFloat("right", 0);
		m_vecVMIronPosOffset.z = pIronOffset->GetFloat("up", 0);

		m_angVMIronAngOffset[PITCH] = pIronOffset->GetFloat("pitch", 0);
		m_angVMIronAngOffset[YAW] = pIronOffset->GetFloat("yaw", 0);
		m_angVMIronAngOffset[ROLL] = pIronOffset->GetFloat("roll", 0);
	}

	// Optional per-weapon multipliers for the fire animation's kick while on the sights.
	KeyValues* pIronRecoil = pKeyValuesData->FindKey("IronsightRecoil");
	m_flIronRecoilVertical = pIronRecoil ? pIronRecoil->GetFloat("vertical", 1) : 1.f;
	m_flIronRecoilSide = pIronRecoil ? pIronRecoil->GetFloat("side", 1) : 1.f;
	m_flIronRecoilBack = pIronRecoil ? pIronRecoil->GetFloat("back", 1) : 1.f;
	m_flIronRecoilMaxDist = pIronRecoil ? pIronRecoil->GetFloat("max_dist", 0) : 0.f;
	m_flIronRecoilMaxAngle = pIronRecoil ? pIronRecoil->GetFloat("max_angle", 0) : 0.f;
	V_strncpy(m_szIronHideMaterials, pKeyValuesData->GetString("IronsightHideMaterials", ""), sizeof(m_szIronHideMaterials));

	KeyValues* pOptic = pKeyValuesData->FindKey("IronsightOptic");
	m_bHasIronOptic = pOptic != nullptr;
	V_strncpy(m_szIronOpticLens, pOptic ? pOptic->GetString("lens", "") : "", sizeof(m_szIronOpticLens));
	if (pOptic)
	{
		V_strncpy(m_szIronOpticLensBone, pOptic->GetString("lens_bone", ""), sizeof(m_szIronOpticLensBone));
		Vector &o = m_vecIronOpticLensOrigin, &u = m_vecIronOpticLensU, &v = m_vecIronOpticLensV;
		m_bHasIronOpticLensMap = m_szIronOpticLensBone[0] && sscanf(pOptic->GetString("lens_map", ""), "%f %f %f %f %f %f %f %f %f",
			&o.x, &o.y, &o.z, &u.x, &u.y, &u.z, &v.x, &v.y, &v.z) == 9;
	}
	m_bHasIronOpticLensMap2 = false;
	m_vecIronOpticLensCircle.Init(0.5f, 0.5f, 0.5f);
	m_flIronOpticLensRadiusV = 0.5f;
	m_flIronOpticLensShape = 2.0f;
	if (pOptic)
	{
		Vector &o = m_vecIronOpticLens2Origin, &u = m_vecIronOpticLens2U, &v = m_vecIronOpticLens2V;
		m_bHasIronOpticLensMap2 = m_bHasIronOpticLensMap && sscanf(pOptic->GetString("lens_map2", ""), "%f %f %f %f %f %f %f %f %f",
			&o.x, &o.y, &o.z, &u.x, &u.y, &u.z, &v.x, &v.y, &v.z) == 9;
		Vector &circle = m_vecIronOpticLensCircle;
		const int count = sscanf(pOptic->GetString("lens_circle", "0.5 0.5 0.5"), "%f %f %f %f",
			&circle.x, &circle.y, &circle.z, &m_flIronOpticLensRadiusV);
		if (count < 4)
		{
			m_flIronOpticLensRadiusV = circle.z;
		}
		m_flIronOpticLensShape = Max(1.0f, pOptic->GetFloat("lens_shape", 2.0f));
	}
	m_bIronOpticWindow = pOptic && pOptic->GetBool("window") && m_bHasIronOpticLensMap;
	m_flIronOpticWindowSkip = pOptic ? pOptic->GetFloat("window_skip", -1.0f) : -1.0f;
	ParseWindowGlass(pOptic ? pOptic->GetString("window_glass", "") : "");
	m_bIronOpticScope = m_bIronOpticWindow && pOptic->GetBool("scope");
	m_bIronOpticOnePane = pOptic && pOptic->GetBool("one_pane") && m_bHasIronOpticLensMap2 && m_szIronOpticLens[0];
	m_bIronOpticReticleInLens = pOptic && pOptic->GetBool("reticle_in_lens");
	// (The reticle name is read at the end, so look it up here.)
	m_bIronOpticCollimated = m_bIronOpticWindow && m_szIronOpticLens[0] && pOptic->GetString("reticle", "")[0]
		&& sscanf(pOptic->GetString("collimated_dot", ""), "%f %f %f", &m_vecIronOpticDot.x, &m_vecIronOpticDot.y,
			&m_vecIronOpticDot.z) == 3 && m_vecIronOpticDot.z > 0.0f;
	// Read by hand: KeyValues::GetColor leaves a three-number colour with alpha 0, which means unset here.
	int dotR = 0, dotG = 0, dotB = 0;
	m_clrIronOpticDot = (pOptic && sscanf(pOptic->GetString("dot_color", ""), "%d %d %d", &dotR, &dotG, &dotB) == 3)
		? Color(dotR, dotG, dotB, 255) : Color(0, 0, 0, 0);

	KeyValues* pDots = pKeyValuesData->FindKey("IronsightDots");
	m_bHasIronDots = pDots != nullptr;
	if (pDots)
	{
		sscanf(pDots->GetString("front", "0 0"), "%f %f", &m_vecIronDotFront.x, &m_vecIronDotFront.z);
		sscanf(pDots->GetString("rear", "0 0 0"), "%f %f %f", &m_vecIronDotRear.x, &m_vecIronDotRear.z, &m_vecIronDotRear.y);
		m_flIronDotSize = pDots->GetFloat("size", 0.12f);
		// Tritium-style: green front post, orange rear notch.
		m_clrIronDotFront = pDots->FindKey("front_color") ? pDots->GetColor("front_color") : Color(60, 255, 60, 255);
		m_clrIronDotRear = pDots->FindKey("rear_color") ? pDots->GetColor("rear_color") : Color(255, 110, 20, 255);
	}
	V_strncpy(m_szIronOpticReticle, pOptic ? pOptic->GetString("reticle", "") : "", sizeof(m_szIronOpticReticle));

	KeyValues* pGhost = pKeyValuesData->FindKey("IronsightGhost");
	if (pGhost)
	{
		static const char *const s_rear[] = { "brackets", "ticks", "gate", "corners" };
		static const char *const s_front[] = { "chevron", "post", "diamond", "split" };
		m_iIronGhostRear = NameIndex(pGhost->GetString("rear", ""), s_rear, ARRAYSIZE(s_rear));
		m_iIronGhostFront = NameIndex(pGhost->GetString("front", ""), s_front, ARRAYSIZE(s_front));
		m_flIronGhostScale = clamp(pGhost->GetFloat("scale", 1.0f), 0.25f, 4.0f);
	}
}

void CNEOIronsightWeaponInfo::ParseWindowGlass(const char *pszPoints)
{
	m_vecIronOpticWindowCircle = m_vecIronOpticLensCircle;
	m_flIronOpticWindowRadiusV = m_flIronOpticLensRadiusV;
	m_iIronOpticWindowGlassPoints = 0;

	// The points, sorted by u then v, for the hull (Andrew's monotone chain).
	Vector2D points[2 * IRON_WINDOW_GLASS_MAX];
	int count = 0;
	for (char *pszEnd = nullptr; count < ARRAYSIZE(points); pszPoints = pszEnd)
	{
		const float u = strtof(pszPoints, &pszEnd);
		if (pszEnd == pszPoints)
		{
			break;
		}
		pszPoints = pszEnd;
		const float v = strtof(pszPoints, &pszEnd);
		if (pszEnd == pszPoints)
		{
			break;
		}
		points[count++].Init(u, v);
	}
	if (count < 3)
	{
		return;
	}
	for (int i = 1; i < count; ++i)
	{
		const Vector2D point = points[i];
		int j = i;
		for (; j > 0 && (points[j - 1].x > point.x || (points[j - 1].x == point.x && points[j - 1].y > point.y)); --j)
		{
			points[j] = points[j - 1];
		}
		points[j] = point;
	}
	const auto cross = [](const Vector2D &o, const Vector2D &a, const Vector2D &b) {
		return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
	};
	Vector2D hull[2 * ARRAYSIZE(points)];
	int size = 0;
	for (int i = 0; i < count; ++i)	// lower hull
	{
		while (size >= 2 && cross(hull[size - 2], hull[size - 1], points[i]) <= 0.0f)
		{
			--size;
		}
		hull[size++] = points[i];
	}
	for (int i = count - 2, lower = size + 1; i >= 0; --i)	// upper hull
	{
		while (size >= lower && cross(hull[size - 2], hull[size - 1], points[i]) <= 0.0f)
		{
			--size;
		}
		hull[size++] = points[i];
	}
	--size;	// the last point repeats the first
	if (size < 3 || size > IRON_WINDOW_GLASS_MAX)
	{
		return;
	}

	Vector2D mins = hull[0], maxs = hull[0];
	for (int i = 1; i < size; ++i)
	{
		mins.Init(Min(mins.x, hull[i].x), Min(mins.y, hull[i].y));
		maxs.Init(Max(maxs.x, hull[i].x), Max(maxs.y, hull[i].y));
	}
	const Vector2D centre = (mins + maxs) * 0.5f;
	m_vecIronOpticWindowCircle.Init(centre.x, centre.y, (maxs.x - mins.x) * 0.5f);
	m_flIronOpticWindowRadiusV = (maxs.y - mins.y) * 0.5f;
	for (int i = 0; i < size; ++i)
	{
		m_vecIronOpticWindowGlass[i] = hull[i] - centre;
	}
	m_iIronOpticWindowGlassPoints = size;
}
