#pragma once

// Shared between the cyberbrain HUD's parts: sensing (neo_cyberbrain_sense.cpp), attention and placement
// (neo_cyberbrain_attention.cpp), the drawing helpers (neo_cyberbrain_paint.cpp), the body group
// (neo_cyberbrain_body.cpp), the surround ring (neo_cyberbrain_ring.cpp), the other groups (neo_cyberbrain_groups.cpp),
// and the HUD element that runs them (ui/neo_hud_cyberbrain.cpp).

#include "neo_cyberbrain.h"
#include "neo_ghost_stroke.h"
#include "neo_hud_model_ammo.h"
#include "neo_hud_model_callouts.h"
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
// bFriendly: a teammate made it (drawn faint, never leading; Kyle: team sounds crowded the ring).
struct Heard { float time; float bearing; float loud; SoundKind kind; bool bFriendly; };
// A sound you made: when, and about how far it carries (metres; placeholders until the in-game calibration).
struct Noise { float time; float metres; SoundKind kind; };
// An enemy the ghost called out: where (world yaw, degrees), how far (metres), how long ago, and how much of its time
// on the ring is left (1 new, 0 gone).
using NeoHud::Callout;
// Who has the objective: nobody, your team, or theirs.
enum Carrier { CARRIER_NONE, CARRIER_OURS, CARRIER_THEIRS };
constexpr int MAX_HEARD = 16, MAX_NOISE = 16, MAX_MATES = 32;
using NeoHud::MAX_CALLOUTS;

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
	NeoHud::Ammo ammo;
	// The rules the groups show, decided here once (attention and paint only read them): low on rounds, how hot the
	// BALC runs (0 fine, 1 warm, 2 critical), standing in bright light uncloaked.
	bool bAmmoLow = false;
	int heatLevel = 0;
	bool bExposed = false;
	bool bReloading = false;
	float reloadStart = -100.0f;
	// The ghost, carried (neo_cyberbrain_uplink.cpp): its uplink working (the active weapon, or holstered when the
	// server allows), its bootup 0 to 1, the enemies its beacons show on your screen now and the nearest of them
	// (metres; below 0 none). Only what the stock beacons already show.
	bool bGhost = false, bGhostWorking = false;
	float ghostBoot = 0.0f;
	int ghostContacts = 0;
	float ghostNearest = -1.0f;
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
		ammoChanged = -100.0f, shotTime = -100.0f, spawnTime = -100.0f, mateDiedTime = -100.0f;
};

// Where a group lives: a far home deep in the periphery and a near one closer to the focus, screen pixels,
// right-handed (mirrored for a left-handed gun), and its visual weight for the balance.
// bLeft: fixed to the screen's left whichever hand holds the gun (the link, beside the squad list), not mirrored.
struct Home { Vector2D far, nearer; float weight; bool bLeft = false; };

// One group's attention and placement.
// The deep layer (its registration crosses and etched rail) trails the group on a softer spring and drifts a few
// pixels as you turn: the depth.
// The perception layers (neo_cyberbrain_perceive.cpp) and the priority each starts at; from critical's a group comes
// into the focus zone by the crosshair.
enum Layer { LAYER_AMBIENT, LAYER_NOTABLE, LAYER_URGENT, LAYER_CRITICAL, LAYER__COUNT };
constexpr float LAYER_FLOOR[LAYER__COUNT] = { 0.0f, 0.25f, 0.55f, 0.85f };
constexpr float FOCUS_FROM = LAYER_FLOOR[LAYER_CRITICAL];

// focus: how far into the focus zone by the crosshair (0 out, 1 all the way; neo_cyberbrain_attention.cpp); kick: the
// gun's knock it rides there, as it was last frame. layer: the perception layer its attention is in now, lastLayer
// the one before, layerChanged when (the marks' shift, neo_cyberbrain_layers.cpp).
struct Place
{
	float att = 0.0f, attVel = 0.0f, sal = 0.0f, balance = 0.0f, focus = 0.0f;
	Vector2D pos, vel, deep, deepVel, kick = Vector2D(0.0f, 0.0f);
	int layer = LAYER_AMBIENT, lastLayer = LAYER_AMBIENT;
	float layerChanged = -100.0f;
	// What it drew last frame, round its point (pos): the box's centre offset and half size, eased (Kyle: the
	// backings should fit each class, their parts differ and change). bDrawn: measured at all.
	Vector2D drawnCentre = Vector2D(0.0f, 0.0f), drawnHalf = Vector2D(0.0f, 0.0f);
	float drawnTime = -100.0f;
	bool bDrawn = false;
	bool bPlaced = false;
};
// A group's priority now, 0 to 1, from its signals in layers (neo_cyberbrain_perceive.cpp).
float Perceive(const Senses &senses, Group group, float now);

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
	float listen;					// how closely you listen, 0 in a fight to 1 all quiet (Listening())
};
constexpr int BRIGHT_RING = GROUP__COUNT;

// Readability on bright scenes (neo_cyberbrain_backing.cpp): the scene's brightness behind each group and the ring,
// one ray a frame round them; a feathered dark backing behind each, darker the brighter it is.
void MeasureBrightness(C_NEO_Player *pPlayer, Frame &f, float dt, bool bBoot);
// A group's (or the ring's) centre and half size on screen, pixels (neo_cyberbrain_attention.cpp, with the layout).
void GroupExtent(const Frame &f, int slot, Vector2D &centre, Vector2D &half);
// The etched chassis on the deep layer (neo_cyberbrain_chassis.cpp): registration crosses, rulers, channel codes.
void PaintChassis(const Frame &f);
void PaintBackings(const Frame &f);
// The frame for drawing one group (or the ring): its contrast set, the strokes' outline to match.
Frame ForGroup(const Frame &f, int slot);

// Sensing (neo_cyberbrain_sense.cpp): reads the player, the view, light and sounds into senses.
void Sense(C_NEO_Player *pPlayer, float dt, float now, bool bBoot, Senses &senses);
// Sounds, yours and those you can hear, and your shots from the magazine (neo_cyberbrain_hearing.cpp), into senses.
void SenseHearing(C_NEO_Player *pPlayer, float now, Senses &senses);
// The ghost's enemy callouts (neo_hud_model_callouts.h) into senses.
void SenseCallouts(float now, Senses &senses);
using NeoHud::ResetCallouts;
// The senses and colour the HUD drew with last (for the gun's overlay, drawn in the 3D pass before the HUD), or none
// if it didn't draw last frame.
const Senses *PublishedSenses(Color &color);

// Attention and placement (neo_cyberbrain_attention.cpp): salience per group, attention in fast and out slow,
// critically damped springs, the balance against the gun, the crosshair keep-out, motion comfort.
void Attend(const Senses &senses, const Home homes[GROUP__COUNT], const Frame &frame, float dt, bool bBoot, Place places[GROUP__COUNT]);
// The ring's deep layer offset (it doesn't move, but its chassis drifts as you turn), pixels.
Vector2D RingDeepOffset();
// The action round you now, 0 calm to 1 a fight (neo_cyberbrain_perceive.cpp).
float Action(const Senses &s, float now);
// How closely you listen, eased from the action (neo_cyberbrain_attention.cpp): quiet, the ring, the sounds round you
// and your own noise come up; in a fight they draw back. 0 to 1.
float Listening();
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
// A plate in other colours (the ammo calls: OUT in red).
void PlateIn(const Frame &f, const wchar_t *pText, float x, float y, int align, float alpha, const Color &bg, const Color &fg);
void Cross(const Frame &f, const Vector2D &at, float size, float alpha);
// Measuring what a group draws (neo_cyberbrain_paint.cpp): between Begin and End every stroke, fill and text it draws
// with any strength goes into one box, kept on its place for GroupExtent next frame. Paused: drawn far from the group
// (the rounds at the muzzle), not counted.
void MeasureBegin();
void MeasurePause(bool bPaused);
void MeasureEnd(Place &place, float now);
// How loud you are now, 0 (nothing) to 3 (60 m and more): the noise waveform's colour, the ring's ticks.
int NoiseArcs(const Senses &s);
// Loose cells (Kyle's pick for stamina and jumps, in place of the tanks): `count` separate cells stacked bottom up in
// the box from a (top left) to b (bottom right), each with its top corner cut on the `chamfer` side (1 right, -1
// left); full ones solid, the one charging outlined and filling from its bottom (its top edge flaring as it fills),
// empty ones a faint outline. No vessel, no cap. bRow lays them left to right, filling from the left.
struct CellStyle { int count = 1; int chamfer = 1; bool bCharging = false; bool bRow = false; Color fill; };
void Cells(const Frame &f, const Vector2D &a, const Vector2D &b, float fill, const CellStyle &style, float alpha);

// The groups and the ring.
// The marks of a group's perception layer round its extent, and their shift as it changes layer.
void PaintLayer(const Frame &f, Group group);
void PaintBody(const Frame &f);
void PaintRing(const Frame &f);
void PaintOptics(const Frame &f);
void PaintWeapon(const Frame &f);
void PaintLink(const Frame &f);
void PaintMotion(const Frame &f);
// The ghost's uplink (neo_cyberbrain_uplink.cpp): its sensing, and its readout in the weapon group's place.
void SenseUplink(C_NEO_Player *pPlayer, Senses &senses);
void PaintUplink(const Frame &f, const Local &L, float alpha, float labels);
// The stride waveform over the speed trace (neo_cyberbrain_stride.cpp): each step, and each other sound you make,
// landing live as a burst; from left to right at height y, in the motion group's local frame.
void PaintStrideStrip(const Frame &f, const Local &L, float left, float right, float y, float alpha);
// Takes your new noises into the stride waveform's level (neo_cyberbrain_stride.cpp): returns it now, 0 to 1.5, and
// whether a sound in it carried 16 m or more.
float StrideListen(const Senses &s, float now, bool &bLoud);
// The motion group's samples (neo_cyberbrain_motion.cpp), the speed trace's and the stride waveform's: how many, and
// the waveform's level at sample i (0 the oldest).
int MotionSamples();
float MotionNoiseAt(int i, bool &bLoud);
} // namespace NeoCyberbrain
