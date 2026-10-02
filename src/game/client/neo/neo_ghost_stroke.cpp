#include "cbase.h"
#include "neo_ghost_stroke.h"
#include "neo_hud_profile.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "materialsystem/MaterialSystemUtil.h"
#include <xmmintrin.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gunplay_outline("cl_neo_gunplay_outline", "0", FCVAR_ARCHIVE,
	"A thin dark outline behind the ghost linework (the crosshair layer, the sight ghost), so a light colour reads on"
	" a light background. Its opacity; 0 = none.", true, 0, true, 1);
ConVar cl_neo_hud_batch("cl_neo_hud_batch", "1", FCVAR_ARCHIVE,
	"Draws the HUD linework (the crosshair layer, the sight ghost, the quick info) as one batch per pass instead of a"
	" draw call a line, much cheaper on old computers. 0 = a draw call each (the old way), if anything looks wrong.",
	true, 0, true, 1);
ConVar cl_neo_hud_batch_fast("cl_neo_hud_batch_fast", "1", 0,
	"Writes the HUD batch's vertices whole, with streaming stores (0 = through the mesh builder, field by field: the old"
	" way, for comparing).", true, 0, true, 1);

static constexpr float OUTLINE_WIDTH = 1.0f;	// pixels either side of the stroke, at 1080p (at least one)

static int s_iWhiteTexture = -1;
static Color s_color;
static int s_iAlpha = 255;
static bool s_bPanelLocal = false;
static float s_flOutline = -1.0f;	// NeoGhostOutline's, or negative for the setting

// Everything waits here and is drawn at the flush: outlines first (every one, so an outline never cuts into a stroke
// drawn before it where they meet), then the strokes and fills in the order they came. Batched, that's one draw
// call a flush rather than one a line.
struct QueuedQuad
{
	Vector2D corners[4];
	Color color;
	int alpha;
	unsigned char alphas[4];	// each corner's (a feathered fill); all alpha otherwise
};
static constexpr int MAX_QUEUED = 1024;
static QueuedQuad s_quads[MAX_QUEUED];
static QueuedQuad s_outlines[MAX_QUEUED];
static int s_iQuads = 0, s_iOutlines = 0;

void NeoGhostBegin(const Color &color, int alpha)
{
	s_color = color;
	s_iAlpha = clamp(alpha, 0, 255);
}

void NeoGhostOutline(float strength)
{
	s_flOutline = strength;
}

void NeoGhostPanelLocal(bool bPanelLocal)
{
	NeoGhostFlush();
	s_bPanelLocal = bPanelLocal;
}

static void SetQuad(QueuedQuad &q, const Vector2D &start, const Vector2D &end, const Vector2D &along, float width,
	const Color &color, int alpha)
{
	const Vector2D across(-along.y * width * 0.5f, along.x * width * 0.5f);
	q.corners[0] = start - across;
	q.corners[1] = end - across;
	q.corners[2] = end + across;
	q.corners[3] = start + across;
	q.color = color;
	q.alpha = alpha;
	for (unsigned char &corner : q.alphas)
	{
		corner = static_cast<unsigned char>(alpha);
	}
}

void NeoGhostStroke(const NeoGhostPen &pen, const Vector2D &a, const Vector2D &b, NeoGhostWeight weight)
{
	static const float s_widths[] = { 2.4f, 1.4f, 0.8f };
	const float width = Max(1.0f, s_widths[weight] * pen.scale);
	const Vector2D middle = (a + b) * 0.5f;
	const Vector2D from = middle + (a - middle) * pen.trace, to = middle + (b - middle) * pen.trace;
	Vector2D along = to - from;
	const float length = along.Length();
	if (length < 0.01f || s_iAlpha <= 0)
	{
		return;
	}
	along /= length;
	if (s_iQuads == MAX_QUEUED || s_iOutlines == MAX_QUEUED)
	{
		NeoGhostFlush();
	}
	// Square caps: each end runs on by half the width, so strokes meeting at a corner join solid.
	const Vector2D start = from - along * (width * 0.5f), end = to + along * (width * 0.5f);
	SetQuad(s_quads[s_iQuads++], start, end, along, width, s_color, s_iAlpha);
	const float outline = (s_flOutline >= 0.0f) ? s_flOutline : cl_neo_gunplay_outline.GetFloat();
	if (outline > 0.0f)
	{
		// Behind it, dark and a little wider all round.
		const float rim = Max(1.0f, OUTLINE_WIDTH * pen.scale);
		SetQuad(s_outlines[s_iOutlines++], start - along * rim, end + along * rim, along, width + 2.0f * rim, Color(0, 0, 0, 255),
			RoundFloatToInt(s_iAlpha * outline));
	}
}

void NeoGhostFill(const Vector2D corners[4])
{
	if (s_iAlpha <= 0)
	{
		return;
	}
	if (s_iQuads == MAX_QUEUED)
	{
		NeoGhostFlush();
	}
	QueuedQuad &q = s_quads[s_iQuads++];
	for (int i = 0; i < 4; ++i)
	{
		q.corners[i] = corners[i];
		q.alphas[i] = static_cast<unsigned char>(s_iAlpha);
	}
	q.color = s_color;
	q.alpha = s_iAlpha;
}

void NeoGhostFillShaded(const Vector2D corners[4], const float shade[4])
{
	if (s_iAlpha <= 0)
	{
		return;
	}
	if (s_iQuads == MAX_QUEUED)
	{
		NeoGhostFlush();
	}
	QueuedQuad &q = s_quads[s_iQuads++];
	int most = 0;
	for (int i = 0; i < 4; ++i)
	{
		q.corners[i] = corners[i];
		q.alphas[i] = static_cast<unsigned char>(clamp(RoundFloatToInt(s_iAlpha * shade[i]), 0, 255));
		most = Max(most, static_cast<int>(q.alphas[i]));
	}
	q.color = s_color;
	q.alpha = most / 2;	// drawn a call each (no per-corner alpha there), about its average
}

void NeoGhostFillRect(float x0, float y0, float x1, float y1)
{
	const Vector2D corners[4] = { Vector2D(x0, y0), Vector2D(x1, y0), Vector2D(x1, y1), Vector2D(x0, y1) };
	NeoGhostFill(corners);
}

// One draw call for the lot, in screen pixels (the HUD's 2D pass takes them as they are, as the dot trail's ribbon).
// The vertices go straight into the mesh, each written whole (normal and padding too) with streaming stores: the
// dynamic buffer is write-combined memory, where the builder's field-at-a-time writes, skipping the normal and the
// padding, cost about 25 ns a vertex (0.13 ms a frame for the cyberbrain's 1400 quads; OPTIMIZATION.md).
static void DrawBatched(const QueuedQuad *pQuads, int count, IMesh *pMesh)
{
	CMeshBuilder meshBuilder;
	meshBuilder.Begin(pMesh, MATERIAL_QUADS, count);
	CNeoHudProfileScope slice(NEO_HUD_PROFILE_BATCH_BUILD);
	// The layout UnlitGeneric's vertex colour gives: position, normal, colour, one 2D texcoord, padded to 48 bytes.
	// Anything else (another material, compression) goes through the builder.
	char *pBase = static_cast<char *>(meshBuilder.BaseVertexData());
	const bool bFast = cl_neo_hud_batch_fast.GetBool() && meshBuilder.VertexSize() == 48
		&& CompressionType(pMesh->GetVertexFormat()) == VERTEX_COMPRESSION_NONE
		&& reinterpret_cast<const char *>(meshBuilder.Normal()) - pBase == 12
		&& reinterpret_cast<const char *>(meshBuilder.TexCoord(0)) - pBase == 28
		&& (reinterpret_cast<uintptr_t>(pBase) & 15) == 0;
	static int s_said = -1;	// once a session, and on a change: whether the fast path is taken (a silent fallback would mislead a bench)
	if (s_said != static_cast<int>(bFast))
	{
		s_said = static_cast<int>(bFast);
		Msg("[hud] batch vertices: %s (vertex %d bytes)\n", bFast ? "written whole, streamed" : "through the builder", meshBuilder.VertexSize());
	}
	if (bFast)
	{
		__m128 *pDst = reinterpret_cast<__m128 *>(pBase);
		for (int i = 0; i < count; ++i)
		{
			const QueuedQuad &q = pQuads[i];
			const unsigned int rgb = q.color.b() | (q.color.g() << 8) | (q.color.r() << 16);
			for (int k = 0; k < 4; ++k)
			{
				union { float f[12]; unsigned int u[12]; __m128 m[3]; } v;
				v.f[0] = q.corners[k].x; v.f[1] = q.corners[k].y; v.f[2] = 0.0f;	// position
				v.f[3] = 0.0f; v.f[4] = 0.0f; v.f[5] = 1.0f;						// normal
				v.u[6] = rgb | (static_cast<unsigned int>(q.alphas[k]) << 24);		// colour, as Color4ub packs it
				v.f[7] = 0.5f; v.f[8] = 0.5f;										// texcoord
				v.u[9] = v.u[10] = v.u[11] = 0;										// padding
				_mm_stream_ps(reinterpret_cast<float *>(pDst++), v.m[0]);
				_mm_stream_ps(reinterpret_cast<float *>(pDst++), v.m[1]);
				_mm_stream_ps(reinterpret_cast<float *>(pDst++), v.m[2]);
			}
		}
		_mm_sfence();
		meshBuilder.FastAdvanceNVertices(count * 4);
	}
	else
	{
		for (int i = 0; i < count; ++i)
		{
			const QueuedQuad &q = pQuads[i];
			for (int k = 0; k < 4; ++k)
			{
				meshBuilder.Position3f(q.corners[k].x, q.corners[k].y, 0.0f);
				meshBuilder.Color4ub(q.color.r(), q.color.g(), q.color.b(), q.alphas[k]);
				meshBuilder.TexCoord2f(0, 0.5f, 0.5f);
				meshBuilder.AdvanceVertex();
			}
		}
	}
	slice.Switch(NEO_HUD_PROFILE_BATCH_DRAW);
	meshBuilder.End();
	pMesh->Draw();
	NeoHudCount(NEO_HUD_COUNT_MESHES);
	NeoHudCount(NEO_HUD_COUNT_QUADS, count);
}

// A draw call each, through vgui (which moves them by the panel's position: the HUD boot draws in its panel's own
// coordinates).
static void DrawEach(const QueuedQuad *pQuads, int count)
{
	if (s_iWhiteTexture < 0)
	{
		s_iWhiteTexture = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile(s_iWhiteTexture, "vgui/white", true, false);
	}
	vgui::surface()->DrawSetTexture(s_iWhiteTexture);
	for (int i = 0; i < count; ++i)
	{
		const QueuedQuad &q = pQuads[i];
		vgui::surface()->DrawSetColor(q.color.r(), q.color.g(), q.color.b(), q.alpha);
		vgui::Vertex_t quad[4];
		quad[0].Init(q.corners[0], Vector2D(0, 0));
		quad[1].Init(q.corners[1], Vector2D(1, 0));
		quad[2].Init(q.corners[2], Vector2D(1, 1));
		quad[3].Init(q.corners[3], Vector2D(0, 1));
		vgui::surface()->DrawTexturedPolygon(4, quad);
	}
	NeoHudCount(NEO_HUD_COUNT_MESHES, count);
	NeoHudCount(NEO_HUD_COUNT_QUADS, count);
}

void NeoGhostFlush()
{
	NeoHudCount(NEO_HUD_COUNT_FLUSHES);
	if (s_iQuads == 0 && s_iOutlines == 0)
	{
		return;
	}
	CNeoHudProfileScope slice(NEO_HUD_PROFILE_BATCH_FLUSH);
	if (s_bPanelLocal || !cl_neo_hud_batch.GetBool())
	{
		DrawEach(s_outlines, s_iOutlines);
		DrawEach(s_quads, s_iQuads);
	}
	else
	{
		static CMaterialReference s_material;
		if (!s_material.IsValid())
		{
			KeyValues *pVMT = new KeyValues("UnlitGeneric");
			pVMT->SetString("$basetexture", "vgui/white");
			pVMT->SetInt("$vertexcolor", 1);
			pVMT->SetInt("$vertexalpha", 1);
			pVMT->SetInt("$translucent", 1);
			pVMT->SetInt("$ignorez", 1);
			pVMT->SetInt("$nocull", 1);
			s_material.Init("__neo_ghost_batch", pVMT);
		}
		CMatRenderContextPtr pRenderContext(materials);
		if (s_iOutlines > 0)
		{
			DrawBatched(s_outlines, s_iOutlines, pRenderContext->GetDynamicMesh(true, nullptr, nullptr, s_material));
		}
		if (s_iQuads > 0)
		{
			DrawBatched(s_quads, s_iQuads, pRenderContext->GetDynamicMesh(true, nullptr, nullptr, s_material));
		}
	}
	s_iQuads = s_iOutlines = 0;
}
