#pragma once

// The benches' exit probe (neo_bench_exit_probe.cpp): armed while a run is going, it writes the exiting thread's
// stack to a file if the process exits then. Windows only; elsewhere these do nothing.
void NeoBenchExitProbeArm(const char *pszFullPath, const char *pszRun);
void NeoBenchExitProbeDisarm();
