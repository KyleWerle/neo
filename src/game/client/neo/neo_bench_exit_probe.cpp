// Built on its own, with no precompiled header and no unity group (game/client/CMakeLists.txt): windows.h's macros
// would break the other files of a unity chunk.
#include "neo_bench_exit_probe.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

//-----------------------------------------------------------------------------
// If the process exits while a bench run is going (exit() or ExitProcess with no error, as the optics set did at the
// M41S in 2026-10), this writes the exiting thread's stack as "module+offset" lines (resolve client.dll and
// server.dll's against their PDBs). ExitProcess detaches every DLL on the exiting thread, so the destructor below runs
// with the caller still on the stack. No file after such an exit: the process was killed outright (TerminateProcess)
// or faulted. Only kernel32 calls here: nothing that loads a library under the loader lock.
//-----------------------------------------------------------------------------
static char s_path[MAX_PATH] = "";
static char s_run[160] = "";	// the run going on, empty when none is

namespace
{
struct ExitProbe
{
	~ExitProbe();
} s_probe;

ExitProbe::~ExitProbe()
{
	if (!s_path[0] || !s_run[0])
	{
		return;
	}
	void *frames[62];
	const USHORT count = RtlCaptureStackBackTrace(0, ARRAYSIZE(frames), frames, nullptr);
	const HANDLE file = CreateFileA(s_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		return;
	}
	char line[MAX_PATH + 64];
	DWORD written;
	int length = _snprintf_s(line, sizeof(line), _TRUNCATE, "exit during %s, thread %lu, %u frames\n", s_run,
		GetCurrentThreadId(), count);
	WriteFile(file, line, length > 0 ? length : 0, &written, nullptr);
	for (USHORT i = 0; i < count; ++i)
	{
		HMODULE module = nullptr;
		char name[MAX_PATH] = "?";
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCSTR>(frames[i]), &module))
		{
			GetModuleFileNameA(module, name, sizeof(name));
		}
		const char *pszBase = strrchr(name, '\\');
		length = _snprintf_s(line, sizeof(line), _TRUNCATE, "%s+0x%llx\n", pszBase ? pszBase + 1 : name,
			static_cast<unsigned long long>(static_cast<char *>(frames[i]) - reinterpret_cast<char *>(module)));
		WriteFile(file, line, length > 0 ? length : 0, &written, nullptr);
	}
	CloseHandle(file);
}
}

void NeoBenchExitProbeArm(const char *pszFullPath, const char *pszRun)
{
	strncpy_s(s_path, pszFullPath, _TRUNCATE);
	strncpy_s(s_run, pszRun, _TRUNCATE);
}

void NeoBenchExitProbeDisarm()
{
	s_run[0] = '\0';
}
#else
void NeoBenchExitProbeArm(const char *, const char *) {}
void NeoBenchExitProbeDisarm() {}
#endif
