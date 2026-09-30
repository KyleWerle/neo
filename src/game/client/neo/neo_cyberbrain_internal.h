#pragma once

// Shared between the cyberbrain HUD's parts: sensing (neo_cyberbrain_sense.cpp), attention and placement
// (neo_cyberbrain_attention.cpp), the drawing helpers (neo_cyberbrain_paint.cpp), the body group
// (neo_cyberbrain_body.cpp), the surround ring (neo_cyberbrain_ring.cpp), the other groups (neo_cyberbrain_groups.cpp),
// and the HUD element that runs them (ui/neo_hud_cyberbrain.cpp).

#include "neo_cyberbrain.h"
#include "neo_ghost_stroke.h"
#include "neo_quickinfo_internal.h"
#include "mathlib/vector2d.h"
#include "Color.h"

class C_NEO_Player;
class IGameEvent;

namespace NeoCyberbrain
{
// The receptor groups. Motion (speed, stamina, jumps) is its own proprioceptive group, kept beside the body.
enum Group { GROUP_BODY, GROUP_OPTICS, GROUP_WEAPON, GROUP_LINK, GROUP_MOTION, GROUP__COUNT };

// NT's own shipped faces, from ClientScheme.res: NOCR for values, Zrnic for labels, Alpha Flight for plates; the
// kanji beside the plates in a Japanese face; NT's own killfeed icons (weapons, headshot, ghost, ranks) as glyphs;
// players' names in Zrnic too (NOCR is digits and capitals only: its lowercase slots hold NT's weapon glyphs).
// The weapons' own bullet glyphs (NOCR's lowercase slots), sized for the weapon group's row.
enum Font { FONT_VALUE, FONT_VALUE_LARGE, FONT_LABEL, FONT_PLATE, FONT_KANJI, FONT_INTEGRITY, FONT_ICONS, FONT_NAME, FONT_BULLETS,
	FONT__COUNT };
// What the fonts resolved to (the cl_neo_hud_fonts command).
void PrintFonts();

enum SoundKind { SOUND_STEP, SOUND_GUNFIRE, SOUND_RELOAD, SOUND_LAND, SOUND_BLAST, SOUND_OTHER, SOUND__COUNT };
const wchar_t *SoundName(SoundKind kind);

// A sound someone else made that you can hear: when it started, where from (world yaw, degrees), how loud at your
// ears (0 to 1).
struct Heard { float time; float bearing; float loud; SoundKind kind; };
// A sound you made: when, and about how far it carries (metres; placeholders until the in-game calibration).
struct Noise { float time; float metres; SoundKind kind; };
// An enemy the ghost called out: where (world yaw, degrees), how far (metres), how long ago, and how much of its time
// on the ring is left (1 new, 0 gone).
struct Callout { float yaw; float metres; float age; float life; };
// Who has the objective: nobody, your team, or theirs.
enum Carrier { CARRIER_NONE, CARRIER_OURS, CARRIER_THEIRS };
constexpr int MAX_HEARD = 16, MAX_NOISE = 16, MAX_MATES = 32, MAX_CALLOUTS = 32;

// Everything the groups show, read once a frame.
struct Senses
{
	int neoClass = -1;
	bool bHasCloak = false, bHasJumps = false, bHasSprint = false, bArmour = false;
	const wchar_t *pVision = nullptr;	// the class's vision mode's plate, or none
	float hp = 1.0f;					// 0 to 1, eased
	int hpNumber = 100;					// as cl_neo_hud_health_mode displays it
	float cloak = 1.0f;					// therm-optic power, 0 to 1, eased
	bool bCloaked = false;
	float aux = 100.0f;					// 0 to 100, eased
	bool bSprinting = false, bVision = false, bInAim = false;
	// Posture and motion.
	float crouch = 0.0f, lean = 0.0f, air = 0.0f;	// eased: 0 to 1, -1 (left) to 1, 0 (grounded) to 1
	float speed = 0.0f;					// metres a second, across the ground
	float runSpeed = 4.0f;				// your class's run with this weapon, metres a second
	float moveYaw = 0.0f;				// the way you're moving, degrees from where you look (right positive)
	bool bMoving = false, bSilent = false;
	float light = 0.3f;					// the light you stand in, 0 dark to 1 bright
	// The weapon, as the ammo panel counts it.
	NeoQuickInfo::Ammo ammo;
	// The rules the groups show, decided here once (attention and paint only read them): low on rounds, how hot the
	// BALC runs (0 fine, 1 warm, 2 critical), standing in bright light uncloaked.
	bool bAmmoLow = false;
	int heatLevel = 0;
	bool bExposed = false;
	bool bReloading = false;
	float reloadStart = -100.0f;
	// How far the reload has come, on the weapon's own clock: a magazine's whole reload (to its end time, the
	// animation's length), or with shells (the Supa 7) the next shell going in. 0 to 1.
	float reloadProgress = 0.0f;
	bool bReloadShells = false;
	float sync = 1.0f;					// aim settle: 1 settled, dips on each shot
	// Link.
	int ping = 0, load = 1, squadAlive = 0, squadTotal = 0;
	// The view, and where things are round you (world yaw, degrees).
	float yaw = 0.0f, pitch = 0.0f;
	float yawRate = 0.0f, pitchRate = 0.0f;	// degrees a second: turning left, looking down
	bool bObjective = false;			// shown: there is one, and you aren't carrying it
	float objectiveYaw = 0.0f, objectiveMetres = 0.0f;
	Carrier carrier = CARRIER_NONE;
	Callout callout[MAX_CALLOUTS];
	int calloutCount = 0, calloutNewest = -1;
	float mateYaw[MAX_MATES] = {};
	int mates = 0;
	bool bRange = false;
	float rangeMetres = 0.0f;
	// Sounds.
	Heard heard[MAX_HEARD];
	int heardCount = 0;
	Noise noise[MAX_NOISE];
	int noiseCount = 0;
	// When things happened (gpGlobals->realtime).
	float hitTime = -100.0f, landTime = -100.0f, cloakChanged = -100.0f, visionChanged = -100.0f, lightChanged = -100.0f,
		ammoChanged = -100.0f, shotTime = -100.0f, spawnTime = -100.0f;
};

// Where a group lives: a far home deep in the periphery and a near one closer to the focus, screen pixels,
// right-handed (mirrored for a left-handed gun), and its visual weight for the balance.
struct Home { Vector2D far, nearer; float weight; };

// One group's attention and placement.
// The deep layer (its registration crosses and etched rail) trails the group on a softer spring and drifts a few
// pixels as you turn: the depth.
struct Place { float att = 0.0f, sal = 0.0f, balance = 0.0f; Vector2D pos, vel, deep, deepVel; bool bPlaced = false; };

struct Frame
{
	NeoHudStyle style;
	Color color;
	float s;						// the screen's height over 1080
	int wide, tall;
	Vector2D centre;
	float now;
	float alpha;					// the reveal on spawn times the screen fade
	int hand;						// 1 the gun on the right, -1 on the left: sides mirror, text and the world don't
	NeoGhostPen pen;
	const Senses *pSenses;
	const Place *pPlaces;
	Vector2D ringCentre;			// the surround ring, where the attention step put it
	Vector2D ringRadii;
	float bright[GROUP__COUNT + 1];	// how bright the scene is behind each group and (last) the ring, 0 to 1
	float contrast;					// what's drawing now: 0 on a dark scene, 1 on a bright one (outlines, text edges)
};
constexpr int BRIGHT_RING = GROUP__COUNT;

// Readability on bright scenes (neo_cyberbrain_backing.cpp): the scene's brightness behind each group and the ring,
// one ray a frame round them; a feathered dark backing behind each, darker the brighter it is.
void MeasureBrightness(C_NEO_Player *pPlayer, Frame &f, float dt, bool bBoot);
// A group's (or the ring's) centre and half size on screen, pixels.
void GroupExtent(const Frame &f, int slot, Vector2D &centre, Vector2D &half);
// The etched chassis on the deep layer (neo_cyberbrain_chassis.cpp): registration crosses, rulers, channel codes.
void PaintChassis(const Frame &f);
void PaintBackings(const Frame &f);
// The frame for drawing one group (or the ring): its contrast set, the strokes' outline to match.
Frame ForGroup(const Frame &f, int slot);

// Sensing (neo_cyberbrain_sense.cpp): reads the player, the view, light and sounds into senses.
void Sense(C_NEO_Player *pPlayer, float dt, float now, bool bBoot, Senses &senses);
// The ghost's enemy callouts (neo_cyberbrain_callouts.cpp): the HUD element passes on the game events it listens for.
void CalloutEvent(IGameEvent *pEvent);
void SenseCallouts(float now, Senses &senses);
void ResetCallouts();
// The senses and colour the HUD drew with last (for the gun's overlay, drawn in the 3D pass before the HUD), or none
// if it didn't draw last frame.
const Senses *PublishedSenses(Color &color);

// Attention and placement (neo_cyberbrain_attention.cpp): salience per group, attention in fast and out slow,
// critically damped springs, the balance against the gun, the crosshair keep-out, motion comfort.
void Attend(const Senses &senses, const Home homes[GROUP__COUNT], const Frame &frame, float dt, bool bBoot, Place places[GROUP__COUNT]);
// The ring's deep layer offset (it doesn't move, but its chassis drifts as you turn), pixels.
Vector2D RingDeepOffset();
// How far to move something with this centre and half size (pixels) to keep it inside the screen's edges.
Vector2D Inside(const Frame &frame, const Vector2D &centre, const Vector2D &half);
// Whether a screen point is in the keep-out round the crosshair (nothing of the HUD's goes there).
bool InKeepout(const Frame &frame, const Vector2D &p);
// A group's look at its attention: scale, strength, and how far its numbers and labels have come in.
struct Look { float scale, alpha, numbers, labels; };
Look LookOf(const Frame &frame, Group group);

// Drawing helpers (neo_cyberbrain_paint.cpp). A Local draws round a point at a scale; its coordinates are pixels at
// 1080p before that scale, and m mirrors its sides.
struct Local
{
	Vector2D origin;
	float k;
	int m;
	Vector2D At(float x, float y) const { return origin + Vector2D(x, y) * k; }
};
extern const Color WARN, CRIT, TEAM_OURS;
int Alpha(const Frame &f, float a);
void Line(const Frame &f, const Vector2D &a, const Vector2D &b, NeoGhostWeight weight, const Color &c, float alpha);
void Rect(const Frame &f, const Vector2D &a, const Vector2D &b, const Color &c, float alpha);
void RectOutline(const Frame &f, const Vector2D &a, const Vector2D &b, NeoGhostWeight weight, const Color &c, float alpha);
// An ellipse's arc, from and to in degrees (0 up, clockwise), as short strokes.
void Arc(const Frame &f, const Vector2D &centre, const Vector2D &radii, float from, float to, NeoGhostWeight weight, const Color &c, float alpha);
// Text with its vertical middle at y: align -1 ending at x, 0 centred, 1 starting at x. Returns its width in pixels.
float Text(const Frame &f, const wchar_t *pText, float x, float y, int align, Font font, const Color &c, float alpha);
// How wide text would draw, pixels.
float TextWidth(const wchar_t *pText, Font font);
// NT's plate: a light grey label with dark text, and its kanji beside it (away from the align side) if given.
void Plate(const Frame &f, const wchar_t *pText, float x, float y, int align, float alpha, const wchar_t *pKanji = nullptr);
void Cross(const Frame &f, const Vector2D &at, float size, float alpha);
// How loud you are now, 0 (nothing) to 3 (60 m and more): the noise waveform's colour, the ring's ticks.
int NoiseArcs(const Senses &s);
// Loose cells (Kyle's pick for stamina and jumps, in place of the tanks): `count` separate cells stacked bottom up in
// the box from a (top left) to b (bottom right), each with its top corner cut on the `chamfer` side (1 right, -1
// left); full ones solid, the one charging outlined and filling from its bottom (its top edge flaring as it fills),
// empty ones a faint outline. No vessel, no cap.
struct CellStyle { int count = 1; int chamfer = 1; bool bCharging = false; Color fill; };
void Cells(const Frame &f, const Vector2D &a, const Vector2D &b, float fill, const CellStyle &style, float alpha);

// The groups and the ring.
void PaintBody(const Frame &f);
void PaintRing(const Frame &f);
void PaintOptics(const Frame &f);
void PaintWeapon(const Frame &f);
void PaintLink(const Frame &f);
void PaintMotion(const Frame &f);
// The stride waveform over the speed trace (neo_cyberbrain_stride.cpp): each step, and each other sound you make,
// landing live as a burst; from left to right at height y, in the motion group's local frame.
void PaintStrideStrip(const Frame &f, const Local &L, float left, float right, float y, float alpha);
} // namespace NeoCyberbrain
