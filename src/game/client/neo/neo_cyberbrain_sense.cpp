#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "neo_hud_model_view.h"
#include "c_neo_player.h"
#include "c_playerresource.h"
#include "neo_gamerules.h"
#include "weapon_neobasecombatweapon.h"
#include "weapon_supa7.h"
#include "view.h"
#include "filesystem.h"
#include "engine/IEngineSound.h"
#include "engine/SndInfo.h"
#include "materialsystem/imaterialsystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// What the cyberbrain senses, once a frame: vitals, posture and motion, the light you stand in, the weapon, the link,
// where things are round you, and sounds. Sounds come from the engine's own list of what's playing
// (enginesound->GetActiveSounds), so they don't depend on the game's sound code: yours by source entity (you, or
// something you own), others' only once they're audible at your ears (the engine's spatialised volume over a floor),
// so it never shows what you couldn't hear.

ConVar cl_neo_hud_hearing("cl_neo_hud_hearing", "1", FCVAR_ARCHIVE,
	"The cyberbrain's surround ring shows sounds other players make that you can already hear: direction and loudness,"
	" never position. 0 = off (the Comp preset).", true, 0, true, 1);

ConVar cl_neo_hud_light_range("cl_neo_hud_light_range", "0.25 0.75", FCVAR_ARCHIVE,
	"The light sensor's raw readings that count as fully dark and fully bright (the iris spreads between them).");

ConVar cl_neo_hud_hearing_debug("cl_neo_hud_hearing_debug", "0", FCVAR_NONE,
	"1 = print your own sounds as the cyberbrain takes them, and the weapon sounds it skips (and why).", true, 0, true, 1);

namespace NeoCyberbrain
{
// How far a shot carries: the game's own AI hearing radii (SOUNDENT_VOLUME_PISTOL / _NEO_SUPPRESSED, in units), so
// the stride waveform's shot marks are what bots hear, not a guess.
constexpr float SHOT_UNITS = 1500.0f, SHOT_SUPPRESSED_UNITS = 900.0f;
constexpr float SHOT_DEDUPE = 0.15f;	// seconds a shot's sound and its magazine drop count as one
constexpr float STEP_MIN_SPEED = 50.0f * METERS_PER_INCH;	// no steps under 50 units a second (NT;RE)
constexpr float AUDIBLE = 0.02f;		// spatialised volume under this: not heard
constexpr float HEARD_FOR = 2.0f, NOISE_FOR = 1.2f;
constexpr float LIGHT_EVERY = 0.1f;		// seconds between light samples

const wchar_t *SoundName(SoundKind kind)
{
	static const wchar_t *s_names[] = { L"STEPS", L"GUNFIRE", L"RELOAD", L"LAND", L"BLAST", L"SOUND" };
	return s_names[clamp(static_cast<int>(kind), 0, static_cast<int>(SOUND__COUNT) - 1)];
}

static struct
{
	float reloadFrom = 0.0f, shellNext = 0.0f, shellFrom = 0.0f;	// game time (curtime): the reload's clock
	float hpRaw = 1.0f, lightAt = -100.0f, lightRaw = 0.3f, lastYaw = 0.0f, lastPitch = 0.0f;
	bool bAir = false, bCloaked = false, bVision = false, bReloading = false;
	wchar_t ammoKey[96] = L"";
	int rounds = -1;
	int seen[64] = {};				// sounds already taken (guids), a ring
	int seenNext = 0;
	int worldSeen[64] = {};			// the world's sounds already passed over, their own ring (so they never push out a
	int worldNext = 0;				// player's sound still playing, which would then be taken twice)
	float lastShotMark = -100.0f;	// the last shot marked from the magazine (the sound path mustn't count it again)
} s_sense;

// The sound's kind, from its file name.
static SoundKind KindOf(const char *pFile)
{
	char lower[MAX_PATH];
	V_strcpy_safe(lower, pFile);
	V_strlower(lower);
	if (V_strstr(lower, "footstep") || V_strstr(lower, "/step") || V_strstr(lower, "ladder"))
		return SOUND_STEP;
	if (V_strstr(lower, "land") || V_strstr(lower, "jump"))
		return SOUND_LAND;
	if (V_strstr(lower, "explo") || V_strstr(lower, "grenade") || V_strstr(lower, "detpack"))
		return SOUND_BLAST;
	if (V_strstr(lower, "reload") || V_strstr(lower, "clip") || V_strstr(lower, "mag") || V_strstr(lower, "bolt") || V_strstr(lower, "pump"))
		return SOUND_RELOAD;
	if (V_strstr(lower, "weapons/"))
		return SOUND_GUNFIRE;
	return SOUND_OTHER;
}
// About how far your sound carries, metres (placeholders until the calibration pass: HUD-REDESIGN.md, "How far is heard").
static float MetresOf(SoundKind kind, float volume)
{
	static const float s_base[] = { 14.0f, 90.0f, 9.0f, 20.0f, 120.0f, 6.0f };
	return s_base[kind] * clamp(volume, 0.2f, 1.0f);
}
static bool InRing(const int (&ring)[64], int guid)
{
	for (const int id : ring)
	{
		if (id == guid)
		{
			return true;
		}
	}
	return false;
}
static bool Seen(int guid)
{
	return InRing(s_sense.seen, guid) || InRing(s_sense.worldSeen, guid);
}
static C_BasePlayer *OwnerPlayer(int entIndex)
{
	C_BaseEntity *pEnt = ClientEntityList().GetBaseEntity(entIndex);
	for (int depth = 0; pEnt && depth < 3; ++depth)
	{
		if (pEnt->IsPlayer())
		{
			return ToBasePlayer(pEnt);
		}
		pEnt = pEnt->GetOwnerEntity();
	}
	return nullptr;
}

// How far your shot carries, metres: the game's AI hearing radius, less for a suppressed weapon.
static float ShotMetres(C_NEO_Player *pPlayer)
{
	auto *pWeapon = static_cast<C_NEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon());
	const bool bSuppressed = pWeapon && (pWeapon->GetNeoWepBits() & NEO_WEP_SUPPRESSED);
	return (bSuppressed ? SHOT_SUPPRESSED_UNITS : SHOT_UNITS) * METERS_PER_INCH;
}

static bool NoisedSince(const Senses &out, SoundKind kind, float since)
{
	for (int i = 0; i < out.noiseCount; ++i)
	{
		if (out.noise[i].kind == kind && out.noise[i].time >= since)
			return true;
	}
	return false;
}

static void SenseSounds(C_NEO_Player *pPlayer, float now, Senses &out)
{
	// Drop what's done.
	int kept = 0;
	for (int i = 0; i < out.heardCount; ++i)
	{
		if (now - out.heard[i].time < HEARD_FOR)
		{
			out.heard[kept++] = out.heard[i];
		}
	}
	out.heardCount = kept;
	kept = 0;
	for (int i = 0; i < out.noiseCount; ++i)
	{
		if (now - out.noise[i].time < NOISE_FOR)
		{
			out.noise[kept++] = out.noise[i];
		}
	}
	out.noiseCount = kept;

	static CUtlVector<SndInfo_t> s_sounds;	// kept, not allocated every frame
	s_sounds.RemoveAll();
	enginesound->GetActiveSounds(s_sounds);
	for (const SndInfo_t &snd : s_sounds)
	{
		// Taken once, when it first reaches your ears (a sound starts before it's spatialised).
		if (snd.m_nSoundSource <= 0 || snd.m_flLastSpatializedVolume <= 0.0f || Seen(snd.m_nGuid))
		{
			if (cl_neo_hud_hearing_debug.GetBool() && !Seen(snd.m_nGuid))
			{
				char skipped[MAX_PATH] = "";
				g_pFullFileSystem->String(snd.m_filenameHandle, skipped, sizeof(skipped));
				if (V_stristr(skipped, "weapons"))
					Msg("[cyberbrain] skipped %s: source %d, spatialised %.3f\n", skipped, snd.m_nSoundSource, snd.m_flLastSpatializedVolume);
			}
			continue;
		}
		C_BasePlayer *pOwner = OwnerPlayer(snd.m_nSoundSource);
		if (!pOwner)
		{
			// The world's own sounds (ambience, doors, props): passed over once, not looked at again every frame.
			s_sense.worldSeen[s_sense.worldNext] = snd.m_nGuid;
			s_sense.worldNext = (s_sense.worldNext + 1) % ARRAYSIZE(s_sense.worldSeen);
			continue;
		}
		s_sense.seen[s_sense.seenNext] = snd.m_nGuid;
		s_sense.seenNext = (s_sense.seenNext + 1) % ARRAYSIZE(s_sense.seen);
		const bool bYours = pOwner == pPlayer;
		if (!bYours && (!cl_neo_hud_hearing.GetBool() || snd.m_flLastSpatializedVolume < AUDIBLE || out.heardCount == MAX_HEARD))
		{
			continue;
		}
		char file[MAX_PATH] = "";
		g_pFullFileSystem->String(snd.m_filenameHandle, file, sizeof(file));
		const SoundKind kind = KindOf(file);
		if (bYours)
		{
			if (cl_neo_hud_hearing_debug.GetBool())
				Msg("[cyberbrain] yours %s: kind %d\n", file, static_cast<int>(kind));
			// A shot the magazine already marked isn't counted again.
			const bool bMarked = kind == SOUND_GUNFIRE && now - s_sense.lastShotMark < SHOT_DEDUPE;
			if (out.noiseCount < MAX_NOISE && kind != SOUND_OTHER && !bMarked)
			{
				const float metres = kind == SOUND_GUNFIRE ? ShotMetres(pPlayer) : MetresOf(kind, snd.m_flVolume);
				out.noise[out.noiseCount++] = { now, metres, kind };
			}
			continue;
		}
		const Vector from = snd.m_pOrigin ? *snd.m_pOrigin : pOwner->GetAbsOrigin();
		const Vector delta = from - MainViewOrigin();
		out.heard[out.heardCount++] = { now, RAD2DEG(atan2f(delta.y, delta.x)), clamp(snd.m_flLastSpatializedVolume / 0.5f, 0.0f, 1.0f), kind };
	}
}

// The light you stand in: the world's light at your chest, times the view's auto exposure on HDR maps.
static float SenseLight(C_NEO_Player *pPlayer)
{
	const Vector light = engine->GetLightForPoint(pPlayer->EyePosition() - Vector(0, 0, 20), true);
	const float luminance = 0.3f * light.x + 0.59f * light.y + 0.11f * light.z;
	CMatRenderContextPtr pRenderContext(materials);
	const float exposure = pRenderContext->GetToneMappingScaleLinear().x;
	const float raw = sqrtf(clamp(0.5f * luminance * exposure, 0.0f, 1.0f));
	// Played, the raw reading sat between about a quarter and three quarters; spread that over the whole scale (a bit
	// past either end just pins it: it's a feel, not a meter).
	float dark = 0.25f, bright = 0.75f;
	sscanf(cl_neo_hud_light_range.GetString(), "%f %f", &dark, &bright);
	return clamp((raw - dark) / Max(bright - dark, 0.05f), 0.0f, 1.0f);
}

void Sense(C_NEO_Player *pPlayer, float dt, float now, bool bBoot, Senses &out)
{
	static ConVarRef cl_neo_hud_health_mode("cl_neo_hud_health_mode");
	const int neoClass = pPlayer->GetClass();
	out.neoClass = neoClass;
	out.bHasCloak = neoClass == NEO_CLASS_RECON || neoClass == NEO_CLASS_ASSAULT || neoClass == NEO_CLASS_VIP;
	out.bHasJumps = neoClass == NEO_CLASS_RECON;
	out.bHasSprint = neoClass == NEO_CLASS_ASSAULT || neoClass == NEO_CLASS_VIP || neoClass == NEO_CLASS_JUGGERNAUT;
	out.bArmour = neoClass == NEO_CLASS_SUPPORT;
	out.pVision = neoClass == NEO_CLASS_RECON ? L"NIGHT VISION" : neoClass == NEO_CLASS_ASSAULT ? L"MOTION VISION"
		: neoClass == NEO_CLASS_SUPPORT ? L"THERMAL VISION" : nullptr;
	const float ease = [&](float tau) { return bBoot ? 1.0f : Min(1.0f, dt / tau); }(0.08f);

	// Vitals.
	const float hpRaw = clamp(static_cast<float>(pPlayer->GetHealth()) / Max(1, pPlayer->GetMaxHealth()), 0.0f, 1.0f);
	if (!bBoot && hpRaw < s_sense.hpRaw - 0.001f)
	{
		out.hitTime = now;
	}
	s_sense.hpRaw = hpRaw;
	out.hp = bBoot ? hpRaw : out.hp + (hpRaw - out.hp) * ease;
	out.hpNumber = pPlayer->GetDisplayedHealth(cl_neo_hud_health_mode.GetInt());
	const float cloakRaw = clamp(pPlayer->CloakPower_CurrentVisualPercentage() / 100.0f, 0.0f, 1.0f);
	out.cloak = bBoot ? cloakRaw : out.cloak + (cloakRaw - out.cloak) * ease;
	const float auxRaw = clamp(pPlayer->m_HL2Local.m_flSuitPower, 0.0f, 100.0f);
	out.aux = bBoot ? auxRaw : out.aux + (auxRaw - out.aux) * ease;
	out.bCloaked = pPlayer->IsCloaked();
	out.bVision = pPlayer->IsInVision();
	out.bSprinting = pPlayer->IsSprinting();
	out.bInAim = pPlayer->IsInAim();
	if (!bBoot && out.bCloaked != s_sense.bCloaked)
		out.cloakChanged = now;
	if (!bBoot && out.bVision != s_sense.bVision)
		out.visionChanged = now;
	s_sense.bCloaked = out.bCloaked;
	s_sense.bVision = out.bVision;

	// Posture and motion.
	const float settle = bBoot ? 1.0f : Min(1.0f, dt / 0.1f);
	out.crouch += (((pPlayer->GetFlags() & FL_DUCKING) ? 1.0f : 0.0f) - out.crouch) * settle;
	const int lean = pPlayer->m_bInLean;
	out.lean += ((lean == NEO_LEAN_LEFT ? -1.0f : lean == NEO_LEAN_RIGHT ? 1.0f : 0.0f) - out.lean) * settle;
	const bool bAir = pPlayer->IsAirborne();
	out.air += ((bAir ? 1.0f : 0.0f) - out.air) * settle;
	if (!bBoot && s_sense.bAir && !bAir)
		out.landTime = now;
	s_sense.bAir = bAir;
	Vector velocity = pPlayer->GetAbsVelocity();
	velocity.z = 0.0f;
	const float unitsPerSecond = velocity.Length();
	out.speed = unitsPerSecond * METERS_PER_INCH;
	out.runSpeed = pPlayer->GetNormSpeed_WithActiveWepEncumberment() * METERS_PER_INCH;
	out.bMoving = out.speed > STEP_MIN_SPEED;
	if (out.bMoving)
	{
		out.moveYaw = AngleNormalize(MainViewAngles()[YAW] - RAD2DEG(atan2f(velocity.y, velocity.x)));
	}
	// Silent as NT;RE's steps are: aiming, or walking under 70% of your speed (crouched: of your crouch speed).
	const float quiet = 0.7f * ((pPlayer->GetFlags() & FL_DUCKING) ? pPlayer->GetCrouchSpeed_WithActiveWepEncumberment()
		: pPlayer->GetNormSpeed_WithActiveWepEncumberment());
	out.bSilent = out.bMoving && !bAir && (out.bInAim || unitsPerSecond < quiet);

	// Light, a few times a second.
	if (bBoot || now - s_sense.lightAt > LIGHT_EVERY)
	{
		s_sense.lightAt = now;
		const float light = SenseLight(pPlayer);
		if (!bBoot && fabsf(light - s_sense.lightRaw) > 0.25f)
			out.lightChanged = now;
		s_sense.lightRaw = light;
	}
	out.light = bBoot ? s_sense.lightRaw : out.light + (s_sense.lightRaw - out.light) * Min(1.0f, dt / 0.5f);
	out.bExposed = out.light > 0.6f && !out.bCloaked;

	// The weapon.
	NeoHud::ReadAmmo(pPlayer, out.ammo);
	out.bAmmoLow = !out.ammo.bHeat && out.ammo.maxRounds > 1 && out.ammo.rounds <= out.ammo.maxRounds / 5;
	out.heatLevel = !out.ammo.bHeat ? 0 : out.ammo.heat > 0.8f ? 2 : out.ammo.heat > 0.5f ? 1 : 0;
	auto *pWeapon = static_cast<C_NEOBaseCombatWeapon *>(pPlayer->GetActiveWeapon());
	out.bReloading = pWeapon && pWeapon->m_bInReload;
	if (out.bReloading && !s_sense.bReloading)
	{
		out.reloadStart = now;
		s_sense.reloadFrom = gpGlobals->curtime;
	}
	s_sense.bReloading = out.bReloading;
	// The reload's progress on the weapon's own clock. A magazine: DefaultReload sets the next attack to the reload
	// animation's end. Shells: the Supa 7 times each shell (and the start) with its next reload.
	out.bReloadShells = pWeapon && (pWeapon->GetNeoWepBits() & NEO_WEP_SUPA7);
	out.reloadProgress = 0.0f;
	if (out.bReloading && out.bReloadShells)
	{
		const float next = static_cast<CWeaponSupa7 *>(pWeapon)->GetNextReload();
		if (next != s_sense.shellNext)
		{
			s_sense.shellNext = next;
			s_sense.shellFrom = gpGlobals->curtime;
		}
		const float span = s_sense.shellNext - s_sense.shellFrom;
		out.reloadProgress = span > 0.01f ? clamp((gpGlobals->curtime - s_sense.shellFrom) / span, 0.0f, 1.0f) : 1.0f;
	}
	else if (out.bReloading)
	{
		const float span = pWeapon->m_flNextPrimaryAttack - s_sense.reloadFrom;
		out.reloadProgress = span > 0.01f ? clamp((gpGlobals->curtime - s_sense.reloadFrom) / span, 0.0f, 1.0f) : 1.0f;
	}
	out.sync = pWeapon ? 1.0f - clamp(pWeapon->GetAccuracyPenaltyFraction(), 0.0f, 1.0f) : 1.0f;
	wchar_t key[ARRAYSIZE(s_sense.ammoKey)];
	V_snwprintf(key, ARRAYSIZE(key), L"%ls|%ls|%ls", out.ammo.name, out.ammo.mags, out.ammo.pMode ? out.ammo.pMode : L"");
	if (!bBoot && (V_wcscmp(key, s_sense.ammoKey) != 0 || out.ammo.rounds > s_sense.rounds))
		out.ammoChanged = now;
	if (!bBoot && out.ammo.rounds < s_sense.rounds)
		out.shotTime = now;
	V_wcsncpy(s_sense.ammoKey, key, sizeof(s_sense.ammoKey));
	s_sense.rounds = out.ammo.rounds;

	// The link.
	const int self = pPlayer->entindex();
	out.ping = g_PR ? g_PR->GetPing(self) : 0;
	out.load = 1 + (out.bCloaked ? 1 : 0) + (out.bVision ? 1 : 0) + (out.bSprinting ? 1 : 0) + (out.bReloading ? 1 : 0)
		+ (now - out.shotTime < 1.0f ? 1 : 0);
	out.squadAlive = out.squadTotal = 0;
	out.mates = 0;
	const int team = pPlayer->GetTeamNumber();
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		if (i == self || !g_PR || !g_PR->IsConnected(i) || g_PR->GetTeam(i) != team)
			continue;
		++out.squadTotal;
		if (g_PR->IsAlive(i))
			++out.squadAlive;
		C_BasePlayer *pMate = UTIL_PlayerByIndex(i);
		if (pMate && !pMate->IsDormant() && pMate->IsAlive() && out.mates < MAX_MATES)
		{
			const Vector d = pMate->GetAbsOrigin() - MainViewOrigin();
			out.mateYaw[out.mates++] = RAD2DEG(atan2f(d.y, d.x));
		}
	}

	// The view, the objective (as the compass finds it), and the range while aiming (as the rangefinder does).
	out.yaw = MainViewAngles()[YAW];
	out.pitch = MainViewAngles()[PITCH];
	out.yawRate = (bBoot || dt <= 0.0f) ? 0.0f : AngleNormalize(out.yaw - s_sense.lastYaw) / dt;
	out.pitchRate = (bBoot || dt <= 0.0f) ? 0.0f : AngleNormalize(out.pitch - s_sense.lastPitch) / dt;
	s_sense.lastYaw = out.yaw;
	s_sense.lastPitch = out.pitch;
	NeoHud::Objective objective;
	out.bObjective = NeoHud::ReadObjective(pPlayer, objective);
	if (out.bObjective)
	{
		const Vector d = objective.pos - MainViewOrigin();
		out.objectiveYaw = RAD2DEG(atan2f(d.y, d.x));
		out.objectiveMetres = d.Length() * METERS_PER_INCH;
		// Coloured by who carries it, as the compass's arrow; hidden while you carry it yourself.
		const int ghoster = objective.carrierTeam;
		out.carrier = (ghoster != TEAM_JINRAI && ghoster != TEAM_NSF) ? CARRIER_NONE : ghoster == team ? CARRIER_OURS : CARRIER_THEIRS;
		out.bObjective = !objective.bYours;
	}
	SenseCallouts(now, out);
	out.bRange = out.bInAim;
	if (out.bRange)
	{
		float metres;
		out.rangeMetres = NeoHud::ReadRange(pPlayer, metres) ? metres : -1.0f;
	}

	SenseSounds(pPlayer, now, out);
	// Your shots from the magazine too: a first-person weapon sound can come without a source the engine names, or
	// unspatialised, so the sound path can miss it; the magazine dropping never does. Once per shot.
	if (out.shotTime == now && !NoisedSince(out, SOUND_GUNFIRE, now - SHOT_DEDUPE) && out.noiseCount < MAX_NOISE)
	{
		out.noise[out.noiseCount++] = { now, ShotMetres(pPlayer), SOUND_GUNFIRE };
		s_sense.lastShotMark = now;
	}
	SenseUplink(pPlayer, out);
	if (bBoot)
	{
		out.spawnTime = now;
	}
}
void SenseCallouts(float now, Senses &out)
{
	out.calloutCount = NeoHud::ReadCallouts(now, out.callout, out.calloutNewest);
}
} // namespace NeoCyberbrain
