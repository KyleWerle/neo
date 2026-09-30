#pragma once

// The HUD's damped springs (HUD-SYSTEM.md, the core layer), substepped so a long frame (an alt-tab, a hitch) can't
// run them away: `at` and its velocity move toward `goal` at omega (radians a second) and damping (1 = critical).
// Works for a float or a Vector2D.

constexpr float NEO_HUD_SPRING_STEP = 1.0f / 240.0f;

template <typename T>
inline void NeoHudSpring(T &at, T &vel, const T &goal, float omega, float damping, float dt)
{
	for (float left = dt; left > 0.0f; left -= NEO_HUD_SPRING_STEP)
	{
		const float h = Min(left, NEO_HUD_SPRING_STEP);
		vel += ((goal - at) * (omega * omega) - vel * (2.0f * damping * omega)) * h;
		at += vel * h;
	}
}
