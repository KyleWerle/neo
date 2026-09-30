#include "cbase.h"
#include "neo_cyberbrain_internal.h"
#include "c_neo_player.h"
#include "weapon_neobasecombatweapon.h"
#include "view.h"
#include "filesystem.h"
#include "engine/IEngineSound.h"
#include "engine/SndInfo.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// What the cyberbrain hears, once a frame. Sounds come from the engine's own list of what's playing
// (enginesound->GetActiveSounds), so they don't depend on the game's sound code: yours by source entity (you, or
// something you own), others' only once they're audible at your ears (the engine's spatialised volume over a floor),
// so it never shows what you couldn't hear.

ConVar cl_neo_hud_hearing("cl_neo_hud_hearing", "1", FCVAR_ARCHIVE,
	"The cyberbrain's surround ring shows sounds other players make that you can already hear: direction and loudness,"
	" never position. 0 = off (the Comp preset).", true, 0, true, 1);

ConVar cl_neo_hud_hearing_debug("cl_neo_hud_hearing_debug", "0", FCVAR_NONE,
	"1 = print your own sounds as the cyberbrain takes them, and the weapon sounds it skips (and why).", true, 0, true, 1);

namespace NeoCyberbrain
{
// How far a shot carries: the game's own AI hearing radii (SOUNDENT_VOLUME_PISTOL / _NEO_SUPPRESSED, in units), so
// the stride waveform's shot marks are what bots hear, not a guess.
constexpr float SHOT_UNITS = 1500.0f, SHOT_SUPPRESSED_UNITS = 900.0f;
constexpr float SHOT_DEDUPE = 0.15f;	// seconds a shot's sound and its magazine drop count as one
constexpr float AUDIBLE = 0.02f;		// spatialised volume under this: not heard
constexpr float HEARD_FOR = 2.0f, NOISE_FOR = 1.2f;

const wchar_t *SoundName(SoundKind kind)
{
	static const wchar_t *s_names[] = { L"STEPS", L"GUNFIRE", L"RELOAD", L"LAND", L"BLAST", L"SOUND" };
	return s_names[clamp(static_cast<int>(kind), 0, static_cast<int>(SOUND__COUNT) - 1)];
}

static struct
{
	int seen[64] = {};				// sounds already taken (guids), a ring
	int seenNext = 0;
	int worldSeen[64] = {};			// the world's sounds already passed over, their own ring (so they never push out a
	int worldNext = 0;				// player's sound still playing, which would then be taken twice)
	float lastShotMark = -100.0f;	// the last shot marked from the magazine (the sound path mustn't count it again)
} s_hearing;

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
	return InRing(s_hearing.seen, guid) || InRing(s_hearing.worldSeen, guid);
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
			s_hearing.worldSeen[s_hearing.worldNext] = snd.m_nGuid;
			s_hearing.worldNext = (s_hearing.worldNext + 1) % ARRAYSIZE(s_hearing.worldSeen);
			continue;
		}
		s_hearing.seen[s_hearing.seenNext] = snd.m_nGuid;
		s_hearing.seenNext = (s_hearing.seenNext + 1) % ARRAYSIZE(s_hearing.seen);
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
			const bool bMarked = kind == SOUND_GUNFIRE && now - s_hearing.lastShotMark < SHOT_DEDUPE;
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

void SenseHearing(C_NEO_Player *pPlayer, float now, Senses &out)
{
	SenseSounds(pPlayer, now, out);
	// Your shots from the magazine too: a first-person weapon sound can come without a source the engine names, or
	// unspatialised, so the sound path can miss it; the magazine dropping never does. Once per shot.
	if (out.shotTime == now && !NoisedSince(out, SOUND_GUNFIRE, now - SHOT_DEDUPE) && out.noiseCount < MAX_NOISE)
	{
		out.noise[out.noiseCount++] = { now, ShotMetres(pPlayer), SOUND_GUNFIRE };
		s_hearing.lastShotMark = now;
	}
}
} // namespace NeoCyberbrain
