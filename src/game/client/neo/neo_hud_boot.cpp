#include "cbase.h"
#include "neo_hud_boot.h"
#include "neo_ghost_stroke.h"
#include "neo_ironsights.h"
#include "c_neo_player.h"
#include "neo_gunplay_shots.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/Panel.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar cl_neo_hud_boot("cl_neo_hud_boot", "0", FCVAR_ARCHIVE,
	"HUD panels boot when they come up (a scan reveals them, a loading ring, a tech line, a"
	" cellular automaton), then clear.", true, 0, true, 1);
ConVar cl_neo_hud_boot_time("cl_neo_hud_boot_time", "0.8", FCVAR_ARCHIVE,
	"Seconds a HUD panel's boot takes.", true, 0.2f, true, 3);

// Fractions of the boot's time.
static constexpr float SCAN_END = 0.45f;		// the scan line reaches the bottom
static constexpr float RING_END = 0.7f;			// the loading ring fades out by here
static constexpr float TEXT_IN = 0.3f;			// the tech line has slid in
static constexpr float TEXT_OUT = 0.75f;		// and starts fading
static constexpr float HIDDEN_REBOOT = 0.5f;	// seconds unpainted after which a panel boots again
// Sizes in pixels at 1080p.
static constexpr float RING_RADIUS = 4.0f;
static constexpr int RING_DOTS = 8;
static constexpr float TEXT_SLIDE = 220.0f;		// how far off the tech line starts
static constexpr int CELLS = 12;				// the automaton's width, and its cell size
static constexpr float CELL = 2.0f;
static constexpr int CELL_ROWS = 3;
static constexpr float CELL_STEP = 0.05f;		// seconds a generation
static constexpr int MASK_ALPHA = 190;			// the unrevealed part below the scan

// In the NT boot sequence's voice (neo_hud_startup_sequence.cpp).
static const wchar_t *const s_lines[] = {
	L".Linking to local systems",
	L".Loading runtime combat libraries",
	L".Testing neural weapon link sequencing",
	L".Booting combat readiness systems",
	L".Analyzing signal cross talk",
	L".Staging interrupts",
	L".Configuring firewalls",
	L".Compensating for combat network latency",
};

static float s_flSpawnEpoch = -100.0f;	// when the local player last came alive
static bool s_bWasAlive = false;
static int s_iLastEpochFrame = -1;

static void WatchSpawn(float now)
{
	if (s_iLastEpochFrame == gpGlobals->framecount)
	{
		return;
	}
	s_iLastEpochFrame = gpGlobals->framecount;
	C_NEO_Player *pPlayer = C_NEO_Player::GetLocalNEOPlayer();
	const bool bAlive = pPlayer && pPlayer->IsAlive();
	if (bAlive && !s_bWasAlive)
	{
		s_flSpawnEpoch = now;
	}
	s_bWasAlive = bAlive;
	// Spectating: a new player watched in first person boots the HUD again.
	const float viewChanged = NeoGunplayWatchShots().viewChanged;
	if (viewChanged > s_flSpawnEpoch && now - viewChanged < 0.1f)
	{
		s_flSpawnEpoch = viewChanged;
	}
}

static vgui::HFont BootFont()
{
	static vgui::HFont s_font = vgui::INVALID_FONT;
	if (s_font == vgui::INVALID_FONT)
	{
		vgui::IScheme *pScheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));
		s_font = pScheme ? pScheme->GetFont("NHudOCRSmallerNoAdditive", true) : vgui::INVALID_FONT;
	}
	return s_font;
}

// Rule 30 from a seeded row, generations - 1 steps on; the last CELL_ROWS rows, newest last.
static void Automaton(int seed, int generations, bool rows[CELL_ROWS][CELLS])
{
	bool row[CELLS] = {};
	for (int i = 0; i < CELLS; ++i)
	{
		row[i] = ((seed * 2654435761u) >> (i + 3)) & 1;
	}
	for (int g = 0; g < generations; ++g)
	{
		for (int r = 0; r < CELL_ROWS - 1; ++r)
		{
			V_memcpy(rows[r], rows[r + 1], sizeof(rows[r]));
		}
		V_memcpy(rows[CELL_ROWS - 1], row, sizeof(row));
		bool next[CELLS];
		for (int i = 0; i < CELLS; ++i)
		{
			const bool left = row[(i + CELLS - 1) % CELLS], middle = row[i], right = row[(i + 1) % CELLS];
			next[i] = left != (middle || right);	// rule 30
		}
		V_memcpy(row, next, sizeof(row));
	}
}

void NeoHudBootPaint(vgui::Panel *pPanel, NeoHudBootState &state)
{
	const float now = gpGlobals->realtime;
	WatchSpawn(now);
	// Its own opt-in, apart from Enable Gunplay (Kyle: a separate update path).
	if (!pPanel || !cl_neo_hud_boot.GetBool())
	{
		state.lastPaint = now;
		return;
	}
	// Due: shown again after a while hidden, or the player came alive since this panel last booted.
	if (now - state.lastPaint > HIDDEN_REBOOT || state.bootStart < s_flSpawnEpoch)
	{
		state.bootStart = now;
		state.line = (state.line + 1 + (reinterpret_cast<uintptr_t>(pPanel) >> 4)) % ARRAYSIZE(s_lines);
	}
	state.lastPaint = now;
	const float t = (now - state.bootStart) / cl_neo_hud_boot_time.GetFloat();
	if (t >= 1.0f)
	{
		return;
	}

	int wide, tall;
	pPanel->GetSize(wide, tall);
	int screenWide, screenTall;
	vgui::surface()->GetScreenSize(screenWide, screenTall);
	const float s = screenTall / 1080.0f;
	const Color white(255, 255, 255, 255);
	NeoGhostPen pen;
	pen.scale = s;

	// The scan: everything below it still hidden, the line itself bright with a fading band above it.
	const float scan = NeoSmoothStep(t / SCAN_END);
	if (scan < 1.0f)
	{
		const int y = RoundFloatToInt(tall * scan);
		vgui::surface()->DrawSetColor(0, 0, 0, MASK_ALPHA);
		vgui::surface()->DrawFilledRect(0, y, wide, tall);
		for (int band = 0; band < 4; ++band)
		{
			const int by = y - band * RoundFloatToInt(2.0f * s);
			vgui::surface()->DrawSetColor(255, 255, 255, 140 >> band);
			vgui::surface()->DrawFilledRect(0, by, wide, by + Max(1, RoundFloatToInt(s)));
		}
	}

	// The loading ring in the top right corner: a bright dot running around it.
	const float ringFade = 1.0f - NeoSmoothStep((t - RING_END * 0.6f) / (RING_END * 0.4f));
	if (ringFade > 0.0f)
	{
		const Vector2D centre(wide - (RING_RADIUS + 3.0f) * s, (RING_RADIUS + 3.0f) * s);
		const float lead = now * 10.0f;
		for (int i = 0; i < RING_DOTS; ++i)
		{
			float behind = lead - i;
			behind = fmodf(fmodf(behind, RING_DOTS) + RING_DOTS, RING_DOTS);
			const float angle = 2.0f * M_PI_F * i / RING_DOTS;
			const Vector2D at = centre + Vector2D(cosf(angle), sinf(angle)) * (RING_RADIUS * s);
			NeoGhostBegin(white, RoundFloatToInt(255.0f * ringFade * (1.0f - behind / RING_DOTS)));
			NeoGhostStroke(pen, at - Vector2D(0.6f * s, 0.0f), at + Vector2D(0.6f * s, 0.0f), NEO_GHOST_MEDIUM);
		}
		NeoGhostFlush();
	}

	// The tech line, sliding in from far to the right along the panel's bottom, then fading.
	const vgui::HFont font = BootFont();
	const float textFade = 1.0f - NeoSmoothStep((t - TEXT_OUT) / (1.0f - TEXT_OUT));
	if (font != vgui::INVALID_FONT && textFade > 0.0f)
	{
		const float slide = 1.0f - NeoSmoothStep(t / TEXT_IN);
		const int fontTall = vgui::surface()->GetFontTall(font);
		const wchar_t *pLine = s_lines[state.line];
		vgui::surface()->DrawSetTextFont(font);
		vgui::surface()->DrawSetTextColor(255, 255, 255, RoundFloatToInt(200.0f * textFade));
		vgui::surface()->DrawSetTextPos(RoundFloatToInt(3.0f * s + slide * TEXT_SLIDE * s), tall - fontTall - RoundFloatToInt(2.0f * s));
		vgui::surface()->DrawPrintText(pLine, V_wcslen(pLine));
	}

	// The automaton in the top left corner, a generation every CELL_STEP.
	bool rows[CELL_ROWS][CELLS] = {};
	Automaton(state.line + 7, 1 + static_cast<int>((now - state.bootStart) / CELL_STEP), rows);
	const int cell = Max(1, RoundFloatToInt(CELL * s));
	const int origin = RoundFloatToInt(3.0f * s);
	for (int r = 0; r < CELL_ROWS; ++r)
	{
		vgui::surface()->DrawSetColor(255, 255, 255, RoundFloatToInt(textFade * (90 + 55 * r)));
		for (int i = 0; i < CELLS; ++i)
		{
			if (rows[r][i])
			{
				const int x0 = origin + i * (cell + 1), y0 = origin + r * (cell + 1);
				vgui::surface()->DrawFilledRect(x0, y0, x0 + cell, y0 + cell);
			}
		}
	}
}
