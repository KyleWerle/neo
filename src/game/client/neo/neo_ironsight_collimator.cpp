#include "cbase.h"
#include "neo_ironsight_collimator.h"
#include "neo_ironsight_lens.h"
#include "neo_ironsights.h"
#include "neo_predicted_viewmodel.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"
#include "view.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static constexpr int COLLIMATOR_SEGMENTS = 32;	// around the dot, for the art's hole and its outline
static constexpr int COLLIMATOR_RINGS = 4;		// from the hole out to the area's outline
static constexpr int DOT_RINGS = 3;			// the dot patch and the hole's fill
static constexpr float DOT_SOFT_EDGE = 0.6f;		// the dot patch fades out from this fraction of its radius
static constexpr float DOT_HARD_EDGE = 0.97f;		// the same for additive art (no tint to hide; the M41's chevron
												// fills its patch right to the edge)

// The glass's own frame (right along its u, the normal away from the eye, up completing them), turned from the
// gun's drawn angles to others: the unswayed ones (no bob, sway lag or view shake, which the base viewmodel
// adds after our own offsets and the bullets share none of), or the unkicked ones (no spread pivot or recoil
// knock either).
struct GlassFrame
{
	Vector right, up, normal;
};

static GlassFrame FrameOf(C_BaseAnimating *pViewModel, const NeoLensPane &pane, const Vector &eye, bool bUnkicked)
{
	GlassFrame frame;
	frame.right = pane.u;
	VectorNormalize(frame.right);
	frame.normal = CrossProduct(pane.u, pane.v);
	VectorNormalize(frame.normal);
	if (DotProduct(frame.normal, pane.origin - eye) < 0.0f)
	{
		frame.normal = -frame.normal;
	}
	frame.up = CrossProduct(frame.normal, frame.right);
	if (auto *pNeoViewModel = dynamic_cast<C_NEOPredictedViewModel *>(pViewModel))
	{
		// Turn back from the drawn angles to the unswayed ones.
		matrix3x4_t unswayed, drawn, drawnInverse, unsway;
		AngleMatrix(bUnkicked ? pNeoViewModel->GetUnkickedAngles() : pNeoViewModel->GetUnswayedAngles(), unswayed);
		AngleMatrix(pNeoViewModel->GetAbsAngles(), drawn);
		MatrixInvert(drawn, drawnInverse);
		ConcatTransforms(unswayed, drawnInverse, unsway);
		for (Vector *pAxis : { &frame.right, &frame.up, &frame.normal })
		{
			const Vector turned = *pAxis;
			VectorRotate(turned, unsway, *pAxis);
		}
	}
	return frame;
}

// The sight's axis in the glass's frame (right, up, normal): the eye's forward, measured against the unkicked
// glass every frame the gun sits on the sights between shots (aimed, idle), so the dot rests exactly on the aim
// (the idle's own sway included) and only the shots' kick moves it; kept from the last such frame through the
// fire animation and at the hip; until the first, the glass's normal. Measured once, it could keep a small
// offset from whatever the pivot or knock were doing then (the dots sat a little up and right).
static struct
{
	int player = 0;
	const CNEOWeaponInfo *pData = nullptr;
	Vector axis = Vector(0.0f, 0.0f, 1.0f);
} s_axis;

static Vector SightAxis(C_BaseAnimating *pViewModel, const NeoLensPane &pane, const Vector &eye,
	const CNEOWeaponInfo &data, const GlassFrame &frame)
{
	auto *pNeoViewModel = dynamic_cast<C_NEOPredictedViewModel *>(pViewModel);
	auto *pPlayer = pNeoViewModel ? dynamic_cast<C_NEO_Player *>(pNeoViewModel->GetOwner()) : nullptr;
	const int player = pPlayer ? pPlayer->entindex() : 0;
	if (player != s_axis.player || &data != s_axis.pData)
	{
		s_axis.player = player;
		s_axis.pData = &data;
		s_axis.axis.Init(0.0f, 0.0f, 1.0f);
	}
	if (pPlayer && pPlayer->IsInAim() && pNeoViewModel->GetIronsightBlend() >= 0.999f)
	{
		const int activity = pNeoViewModel->GetSequenceActivity(pNeoViewModel->GetSequence());
		if (activity == ACT_VM_IDLE || activity == ACT_VM_IDLE_EMPTY)
		{
			const GlassFrame rest = FrameOf(pViewModel, pane, eye, true);
			const Vector &forward = CurrentViewForward();
			s_axis.axis.Init(DotProduct(forward, rest.right), DotProduct(forward, rest.up), DotProduct(forward, rest.normal));
		}
	}
	return frame.right * s_axis.axis.x + frame.up * s_axis.axis.y + frame.normal * s_axis.axis.z;
}

// A point of the pane in its UV, from a point on its plane.
static Vector2D PaneUV(const NeoLensPane &pane, const Vector &point)
{
	const Vector d = point - pane.origin;
	const float uu = DotProduct(pane.u, pane.u), uv = DotProduct(pane.u, pane.v), vv = DotProduct(pane.v, pane.v);
	const float du = DotProduct(d, pane.u), dv = DotProduct(d, pane.v);
	const float det = uu * vv - uv * uv;
	return (fabsf(det) > 1e-8f) ? Vector2D((du * vv - dv * uv) / det, (dv * uu - du * uv) / det) : Vector2D(0.0f, 0.0f);
}

// Where along the ray from start in UV direction dir the outline (a convex polygon around start) is crossed,
// as a multiple of dir; 0 if it isn't.
static float OutlineCrossing(const Vector2D &start, const Vector2D &dir, const Vector2D *pOutline, int points)
{
	float nearest = 0.0f;
	for (int i = 0; i < points; ++i)
	{
		const Vector2D &p = pOutline[i], &q = pOutline[(i + 1) % points];
		const Vector2D edge = q - p;
		const float denom = dir.x * edge.y - dir.y * edge.x;
		if (fabsf(denom) < 1e-9f)
		{
			continue;
		}
		const Vector2D rel = p - start;
		const float t = (rel.x * edge.y - rel.y * edge.x) / denom;
		const float s = (rel.x * dir.y - rel.y * dir.x) / denom;
		if (t > 0.0f && s >= -1e-4f && s <= 1.0f + 1e-4f && (nearest <= 0.0f || t < nearest))
		{
			nearest = t;
		}
	}
	return nearest;
}

// How far a UV point is inside the outline, in viewmodel units (negative outside).
static float InsideBy(const Vector2D &point, const Vector2D *pOutline, int points, float lengthU, float lengthV)
{
	float nearest = FLT_MAX;
	int sign = 0;
	bool bInside = true;
	for (int i = 0; i < points; ++i)
	{
		const Vector2D p(pOutline[i].x * lengthU, pOutline[i].y * lengthV);
		const Vector2D q(pOutline[(i + 1) % points].x * lengthU, pOutline[(i + 1) % points].y * lengthV);
		const Vector2D x(point.x * lengthU, point.y * lengthV);
		const Vector2D edge = q - p, rel = x - p;
		const float cross = edge.x * rel.y - edge.y * rel.x;
		const int side = (cross > 0.0f) ? 1 : -1;
		if (sign == 0)
		{
			sign = side;
		}
		else if (side != sign)
		{
			bInside = false;
		}
		const float along = clamp(DotProduct2D(rel, edge) / Max(DotProduct2D(edge, edge), 1e-9f), 0.0f, 1.0f);
		nearest = Min(nearest, (rel - edge * along).Length());
	}
	return bInside ? nearest : -nearest;
}

bool NeoIronsightDrawCollimatedArt(C_BaseAnimating *pViewModel, IMaterial *pArt, const NeoLensPane &pane,
	const CNEOWeaponInfo &data, const Vector2D *pOutline, int points, float alpha, float fadeStart)
{
	const Vector eye = CurrentViewOrigin();
	const float lengthU = pane.u.Length(), lengthV = pane.v.Length();
	if (!pArt || points < 3 || lengthU <= 0.0f || lengthV <= 0.0f)
	{
		return false;
	}
	// The dot in the art, and its radius as UV offsets (round on the glass).
	const Vector2D dot(data.m_vecIronOpticDot.x, data.m_vecIronOpticDot.y);
	const float radius = data.m_vecIronOpticDot.z * lengthU;	// in viewmodel units
	const auto offset = [&](float angle, float distance) {
		return Vector2D(cosf(angle) * distance / lengthU, sinf(angle) * distance / lengthV);
	};

	// The area's outline as seen from the dot: where each of the dot's directions leaves it.
	Vector2D outline[COLLIMATOR_SEGMENTS];
	for (int i = 0; i < COLLIMATOR_SEGMENTS; ++i)
	{
		const Vector2D dir = offset(2.0f * M_PI_F * i / COLLIMATOR_SEGMENTS, 1.0f);
		const float crossing = OutlineCrossing(dot, dir, pOutline, points);
		if (crossing <= 1.5f * radius)
		{
			return false;	// the dot isn't well inside the area
		}
		outline[i] = dot + dir * crossing;
	}

	// Where the dot floats: the sight's axis from the eye, through the glass. The axis turns with the gun as its
	// shots turn it (fire animation, spread pivot, recoil knock), not with its bob, sway lag or view shake.
	const GlassFrame frame = FrameOf(pViewModel, pane, eye, false);
	const Vector axis = SightAxis(pViewModel, pane, eye, data, frame);
	const Vector &normal = frame.normal;
	const float facing = DotProduct(normal, axis);
	float dotAlpha = 0.0f;
	Vector2D floating = dot;
	if (facing > 0.01f)
	{
		const Vector crossing = eye + axis * (DotProduct(normal, pane.origin - eye) / facing);
		floating = PaneUV(pane, crossing);
		// Fading out as it reaches the edge of the area, gone past it.
		dotAlpha = alpha * NeoSmoothStep(InsideBy(floating, pOutline, points, lengthU, lengthV) / radius);
	}

	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->Bind(pArt);
	IMesh *pMesh = pRenderContext->GetDynamicMesh();
	CMeshBuilder meshBuilder;
	const int artVertices = COLLIMATOR_SEGMENTS * (COLLIMATOR_RINGS + 1);
	const int discVertices = COLLIMATOR_SEGMENTS * (DOT_RINGS + 1);
	const int artIndices = COLLIMATOR_SEGMENTS * COLLIMATOR_RINGS * 6;
	const int discIndices = COLLIMATOR_SEGMENTS * DOT_RINGS * 6;
	const bool bDot = dotAlpha > 0.0f;
	// Additive art (the M41's) adds light where it has any and nothing elsewhere: its hole needs no filling (it is
	// clear glass), and it fades by darkening rather than by alpha, which its material doesn't use.
	const bool bAdditive = pArt->GetMaterialVarFlag(MATERIAL_VAR_ADDITIVE);
	const int discs = (bDot ? 1 : 0) + (bAdditive ? 0 : 1);
	meshBuilder.Begin(pMesh, MATERIAL_TRIANGLES, artVertices + discVertices * discs, artIndices + discIndices * discs);
	int base = 0;
	const auto vertex = [&](const Vector2D &at, const Vector2D &tex, float a) {
		const Vector world = pane.At(at.x, at.y);
		// Lifted a hair toward the eye so it sits on the glass rather than in it.
		Vector lift = eye - world;
		VectorNormalize(lift);
		const Vector position = world + lift * 0.01f;
		const unsigned char level = static_cast<unsigned char>(255.0f * clamp(a, 0.0f, 1.0f));
		if (bAdditive)
		{
			meshBuilder.Color4ub(level, level, level, 255);
		}
		else
		{
			meshBuilder.Color4ub(255, 255, 255, level);
		}
		meshBuilder.TexCoord2f(0, tex.x, tex.y);
		meshBuilder.Position3fv(position.Base());
		meshBuilder.AdvanceVertex();
	};
	// Quads between rings of COLLIMATOR_SEGMENTS vertices, rings consecutive from base.
	const auto rings = [&](int count) {
		for (int ring = 0; ring < count; ++ring)
		{
			for (int seg = 0; seg < COLLIMATOR_SEGMENTS; ++seg)
			{
				const int next = (seg + 1) % COLLIMATOR_SEGMENTS;
				const int a = base + ring * COLLIMATOR_SEGMENTS + seg, b = base + ring * COLLIMATOR_SEGMENTS + next;
				const int c = a + COLLIMATOR_SEGMENTS, d = b + COLLIMATOR_SEGMENTS;
				meshBuilder.FastIndex(a); meshBuilder.FastIndex(c); meshBuilder.FastIndex(d);
				meshBuilder.FastIndex(a); meshBuilder.FastIndex(d); meshBuilder.FastIndex(b);
			}
		}
		base += (count + 1) * COLLIMATOR_SEGMENTS;
	};
	// The art, from the rim of the dot's hole out to the area's outline, fading to the rim from fadeStart of
	// the way out (the hole is small, so from the hole is close enough to from the centre).
	for (int ring = 0; ring <= COLLIMATOR_RINGS; ++ring)
	{
		const float t = static_cast<float>(ring) / COLLIMATOR_RINGS;
		const float a = alpha * (1.0f - NeoSmoothStep((t - fadeStart) / Max(1.0f - fadeStart, 0.001f)));
		for (int seg = 0; seg < COLLIMATOR_SEGMENTS; ++seg)
		{
			const Vector2D inner = dot + offset(2.0f * M_PI_F * seg / COLLIMATOR_SEGMENTS, radius);
			vertex(inner + (outline[seg] - inner) * t, inner + (outline[seg] - inner) * t, a);
		}
	}
	rings(COLLIMATOR_RINGS);

	// The hole, filled with the art just around it, mirrored in its rim (the tint and any lines carry on).
	if (!bAdditive)
	{
		for (int ring = 0; ring <= DOT_RINGS; ++ring)
		{
			const float r = Max(static_cast<float>(ring) / DOT_RINGS, 0.02f);
			for (int seg = 0; seg < COLLIMATOR_SEGMENTS; ++seg)
			{
				const float angle = 2.0f * M_PI_F * seg / COLLIMATOR_SEGMENTS;
				vertex(dot + offset(angle, radius * r), dot + offset(angle, radius * (2.0f - r)), alpha);
			}
		}
		rings(DOT_RINGS);
	}

	// The dot, at its floating place, softened at its edge so its patch of tint doesn't show.
	if (bDot)
	{
		for (int ring = 0; ring <= DOT_RINGS; ++ring)
		{
			const float r = Max(static_cast<float>(ring) / DOT_RINGS, 0.02f);
			const float edge = bAdditive ? DOT_HARD_EDGE : DOT_SOFT_EDGE;
			const float soft = 1.0f - NeoSmoothStep((r - edge) / (1.0f - edge));
			for (int seg = 0; seg < COLLIMATOR_SEGMENTS; ++seg)
			{
				const Vector2D step = offset(2.0f * M_PI_F * seg / COLLIMATOR_SEGMENTS, radius * r);
				vertex(floating + step, dot + step, dotAlpha * soft);
			}
		}
		rings(DOT_RINGS);
	}
	meshBuilder.End();
	pMesh->Draw();
	return true;
}
