#include "cbase.h"
#include "neo_ghost_stroke.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static int s_iWhiteTexture = -1;

void NeoGhostBegin(const Color &color, int alpha)
{
	if (s_iWhiteTexture < 0)
	{
		s_iWhiteTexture = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile(s_iWhiteTexture, "vgui/white", true, false);
	}
	vgui::surface()->DrawSetTexture(s_iWhiteTexture);
	vgui::surface()->DrawSetColor(color.r(), color.g(), color.b(), clamp(alpha, 0, 255));
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
	const Vector2D across(-along.y * width * 0.5f, along.x * width * 0.5f);
	const Vector2D start = from - along * (width * 0.5f), end = to + along * (width * 0.5f);
	vgui::Vertex_t quad[4];
	quad[0].Init(start - across, Vector2D(0, 0));
	quad[1].Init(end - across, Vector2D(1, 0));
	quad[2].Init(end + across, Vector2D(1, 1));
	quad[3].Init(start + across, Vector2D(0, 1));
	vgui::surface()->DrawTexturedPolygon(4, quad);
}
