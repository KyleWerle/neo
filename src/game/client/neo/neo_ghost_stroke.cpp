#include "cbase.h"
#include "neo_ghost_stroke.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_gunplay_outline("cl_neo_gunplay_outline", "0", FCVAR_ARCHIVE,
	"A thin dark outline behind the ghost linework (the crosshair layer, the sight ghost), so a light colour reads on"
	" a light background. Its opacity; 0 = none.", true, 0, true, 1);

static constexpr float OUTLINE_WIDTH = 1.0f;	// pixels either side of the stroke, at 1080p (at least one)

static int s_iWhiteTexture = -1;
static Color s_color;
static int s_iAlpha = 255;

// With an outline, strokes wait here and are drawn in two passes, every outline and then every stroke, so an
// outline never cuts into a stroke drawn before it where they meet.
struct QueuedStroke
{
	Vector2D start, end, along;
	float width, rim;
	Color color;
	int alpha;
};
static constexpr int MAX_QUEUED = 512;
static QueuedStroke s_queue[MAX_QUEUED];
static int s_iQueued = 0;

void NeoGhostBegin(const Color &color, int alpha)
{
	if (s_iWhiteTexture < 0)
	{
		s_iWhiteTexture = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile(s_iWhiteTexture, "vgui/white", true, false);
	}
	s_color = color;
	s_iAlpha = clamp(alpha, 0, 255);
	vgui::surface()->DrawSetTexture(s_iWhiteTexture);
	vgui::surface()->DrawSetColor(color.r(), color.g(), color.b(), s_iAlpha);
}

// A quad from start to end, width wide.
static void Quad(const Vector2D &start, const Vector2D &end, const Vector2D &along, float width)
{
	const Vector2D across(-along.y * width * 0.5f, along.x * width * 0.5f);
	vgui::Vertex_t quad[4];
	quad[0].Init(start - across, Vector2D(0, 0));
	quad[1].Init(end - across, Vector2D(1, 0));
	quad[2].Init(end + across, Vector2D(1, 1));
	quad[3].Init(start + across, Vector2D(0, 1));
	vgui::surface()->DrawTexturedPolygon(4, quad);
}

void NeoGhostStroke(const NeoGhostPen &pen, const Vector2D &a, const Vector2D &b, NeoGhostWeight weight)
{
	static const float s_widths[] = { 2.4f, 1.4f, 0.8f };
	const float width = Max(1.0f, s_widths[weight] * pen.scale);
	const Vector2D middle = (a + b) * 0.5f;
	const Vector2D from = middle + (a - middle) * pen.trace, to = middle + (b - middle) * pen.trace;
	Vector2D along = to - from;
	const float length = along.Length();
	if (length < 0.01f)
	{
		return;
	}
	along /= length;
	// Square caps: each end runs on by half the width, so strokes meeting at a corner join solid.
	const Vector2D start = from - along * (width * 0.5f), end = to + along * (width * 0.5f);
	if (cl_neo_gunplay_outline.GetFloat() > 0.0f)
	{
		if (s_iQueued == MAX_QUEUED)
		{
			NeoGhostFlush();
		}
		s_queue[s_iQueued++] = { start, end, along, width, Max(1.0f, OUTLINE_WIDTH * pen.scale), s_color, s_iAlpha };
		return;
	}
	Quad(start, end, along, width);
}

void NeoGhostFlush()
{
	if (s_iQueued == 0)
	{
		return;
	}
	const float outline = cl_neo_gunplay_outline.GetFloat();
	vgui::surface()->DrawSetTexture(s_iWhiteTexture);
	// Behind them all, dark and a little wider all round.
	for (int i = 0; i < s_iQueued; ++i)
	{
		const QueuedStroke &q = s_queue[i];
		vgui::surface()->DrawSetColor(0, 0, 0, RoundFloatToInt(q.alpha * outline));
		Quad(q.start - q.along * q.rim, q.end + q.along * q.rim, q.along, q.width + 2.0f * q.rim);
	}
	for (int i = 0; i < s_iQueued; ++i)
	{
		const QueuedStroke &q = s_queue[i];
		vgui::surface()->DrawSetColor(q.color.r(), q.color.g(), q.color.b(), q.alpha);
		Quad(q.start, q.end, q.along, q.width);
	}
	s_iQueued = 0;
	vgui::surface()->DrawSetColor(s_color.r(), s_color.g(), s_color.b(), s_iAlpha);
}
