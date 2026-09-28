#include "cbase.h"
#include "neo_ironsight_optic_glitch.h"
#include "weapon_neobasecombatweapon.h"
#include "materialsystem/imesh.h"
#include "view.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_ironsight_optic_glitch("cl_neo_ironsight_optic_glitch", "1", FCVAR_ARCHIVE,
	"Disc optics (e.g. the Jitte's) break up into glitchy pixels off the sights: 0 = off, 1 = full.", true, 0, true, 1);

static constexpr float GLITCH_FPS = 15.0f;	// how often the glitch pattern changes: choppy on purpose
static constexpr int GLITCH_CELLS_AT_HIP = 8;		// pixels across the lens at the hip
static constexpr int GLITCH_CELLS_MAX = 40;		// just before it snaps to the clean view
static constexpr float GLITCH_SPILL = 0.4f;		// how far cells spill past the rim, in lens radii

float NeoOpticClarity(float ironsightBlend)
{
	const float clarity = clamp((ironsightBlend - 0.3f) / 0.7f, 0.0f, 1.0f);
	return 1.0f - (1.0f - clarity) * cl_neo_ironsight_optic_glitch.GetFloat();
}

// A repeatable random value in [0, 1) for a cell, row or frame.
static float GlitchHash(int a, int b, int c)
{
	unsigned int h = static_cast<unsigned int>(a) * 73856093u ^ static_cast<unsigned int>(b) * 19349663u ^ static_cast<unsigned int>(c) * 83492791u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return (h & 0xFFFFFF) / static_cast<float>(0x1000000);
}

struct GlitchCell
{
	float x, y;			// centre, in lens radii from the lens centre
	float sampleX, sampleY;	// where in the live view it takes its colour, in [0, 1]
	unsigned char r, g, b, a;
};

void NeoDrawOpticGlitch(IMaterial *pLiveView, IMaterial *pSpill, const matrix3x4_t &lensToWorld,
	const CNEOWeaponInfo &data, float clarity, float centreAlpha, float fadeStart)
{
	const float glitch = 1.0f - clarity;
	const int cells = Min(GLITCH_CELLS_MAX, GLITCH_CELLS_AT_HIP + static_cast<int>((GLITCH_CELLS_MAX - GLITCH_CELLS_AT_HIP) * clarity * clarity * clarity));
	const float size = 2.0f / cells;
	const float extent = 1.0f + GLITCH_SPILL * glitch;
	const int reach = static_cast<int>(ceilf(extent / size));
	const int frame = static_cast<int>(gpGlobals->realtime * GLITCH_FPS);

	CUtlVector<GlitchCell> inside, spill;
	for (int row = -reach; row < reach; ++row)
	{
		// Some rows tear sideways this frame.
		const float tear = GlitchHash(row, frame, 1) < 0.25f * glitch ? (GlitchHash(row, frame, 2) - 0.5f) * 0.4f * glitch : 0.0f;
		for (int col = -reach; col < reach; ++col)
		{
			GlitchCell cell;
			cell.x = (col + 0.5f) * size;
			cell.y = (row + 0.5f) * size;
			const float radius = sqrtf(cell.x * cell.x + cell.y * cell.y);
			const int id = (row + 1000) * 2001 + col;
			const bool bSpill = radius > 1.0f;
			if (bSpill && (radius > extent || GlitchHash(id, frame, 3) >= 0.35f * glitch * (1.0f - (radius - 1.0f) / (extent - 1.0f))))
			{
				continue;
			}
			// Each cell samples its own spot, shimmering by up to half a cell.
			const float jitterX = (GlitchHash(id, frame, 4) - 0.5f) * size * glitch;
			const float jitterY = (GlitchHash(id, frame, 5) - 0.5f) * size * glitch;
			cell.sampleX = clamp(0.5f + 0.5f * (cell.x + tear + jitterX), 0.0f, 1.0f);
			cell.sampleY = clamp(0.5f + 0.5f * (cell.y + jitterY), 0.0f, 1.0f);

			const float t = clamp((radius - fadeStart) / Max(1.0f - fadeStart, 0.001f), 0.0f, 1.0f);
			float alpha = centreAlpha * (1.0f - t * t * (3.0f - 2.0f * t));
			if (bSpill)
			{
				alpha = centreAlpha * (0.3f + 0.6f * GlitchHash(id, frame, 6));
			}
			const float roll = GlitchHash(id, frame, 7);
			const float bright = 1.0f - 0.35f * glitch * GlitchHash(id, frame, 8);
			if (roll < 0.08f * glitch)
			{
				cell.r = cell.g = cell.b = 0;	// dropped out
			}
			else if (roll < 0.14f * glitch)
			{
				// NT green
				cell.r = 120;
				cell.g = 255;
				cell.b = 140;
			}
			else
			{
				cell.r = cell.g = cell.b = static_cast<unsigned char>(255.0f * bright);
			}
			cell.a = static_cast<unsigned char>(255.0f * alpha);
			(bSpill ? spill : inside).AddToTail(cell);
		}
	}

	// Lens-circle coordinates -> world, lifted a hair toward the eye so it sits on the lens.
	const Vector &circle = data.m_vecIronOpticLensCircle;
	const Vector toEye = CurrentViewOrigin();
	const auto point = [&](float x, float y) {
		const Vector local = data.m_vecIronOpticLensOrigin + data.m_vecIronOpticLensU * (circle.x + circle.z * x)
			+ data.m_vecIronOpticLensV * (circle.y + circle.z * y);
		Vector out;
		VectorTransform(local, lensToWorld, out);
		Vector lift = toEye - out;
		VectorNormalize(lift);
		return out + lift * 0.01f;
	};

	CMatRenderContextPtr pRenderContext(materials);
	const auto draw = [&](IMaterial *pMaterial, const CUtlVector<GlitchCell> &list) {
		if (!pMaterial || list.IsEmpty())
		{
			return;
		}
		pRenderContext->Bind(pMaterial);
		IMesh *pMesh = pRenderContext->GetDynamicMesh();
		CMeshBuilder meshBuilder;
		meshBuilder.Begin(pMesh, MATERIAL_QUADS, list.Count());
		const float half = size * 0.5f;
		for (const GlitchCell &cell : list)
		{
			// One colour per cell: all four corners sample the same spot.
			const float corners[4][2] = { { -half, -half }, { half, -half }, { half, half }, { -half, half } };
			for (const auto &corner : corners)
			{
				const Vector position = point(cell.x + corner[0], cell.y + corner[1]);
				meshBuilder.Color4ub(cell.r, cell.g, cell.b, cell.a);
				meshBuilder.TexCoord2f(0, cell.sampleX, cell.sampleY);
				meshBuilder.Position3fv(position.Base());
				meshBuilder.AdvanceVertex();
			}
		}
		meshBuilder.End();
		pMesh->Draw();
	};
	draw(pLiveView, inside);
	draw(pSpill, spill);
}
