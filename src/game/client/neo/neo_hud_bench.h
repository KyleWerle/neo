#pragma once

// The HUD bench (neo_hud_bench.cpp), for the automatic runner (neo_hud_bench_auto.cpp).
bool NeoHudBenchRunning();
void NeoHudBenchStart(float measureSeconds, float settleSeconds);
// How many runs have finished and been reported (a stopped run isn't).
int NeoHudBenchReports();
