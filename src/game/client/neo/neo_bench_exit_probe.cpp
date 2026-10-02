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
// with the caller still on the stack. A fault is caught too (FaultProbe below); no file after a crash: the process
// was killed outright (TerminateProcess) or faulted past the probe. Only kernel32 calls here: nothing that loads a
// library under the loader lock.
//-----------------------------------------------------------------------------
static char s_path[MAX_PATH] = "";
static char s_run[160] = "";	// the run going on, empty when none is
static volatile LONG s_faults = 0;	// faults written this process (the first few only)

// The calling thread's stack as "module+offset" lines, after a heading. Only kernel32 calls and no heap: it runs under
// the loader lock (exit) or on a faulting thread with little stack left (a fault).
static void WriteStack(const char *pszHeading, bool bAppend)
{
	void *frames[62];
	const USHORT count = RtlCaptureStackBackTrace(0, ARRAYSIZE(frames), frames, nullptr);
	const HANDLE file = CreateFileA(s_path, bAppend ? FILE_APPEND_DATA : GENERIC_WRITE, 0, nullptr,
		bAppend ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		return;
	}
	char line[MAX_PATH + 64];
	DWORD written;
	int length = _snprintf_s(line, sizeof(line), _TRUNCATE, "%s, thread %lu, %u frames\n", pszHeading,
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
	char heading[200];
	_snprintf_s(heading, sizeof(heading), _TRUNCATE, "exit during %s", s_run);
	WriteStack(heading, InterlockedCompareExchange(&s_faults, 0, 0) > 0);
}
}

//-----------------------------------------------------------------------------
// A fault while a run is going (an access violation, a stack overflow, a corrupt heap): the engine's own handler ends
// the process with TerminateProcess, past both the exit hook above and Windows' error reporting, so it is caught
// first, as it is raised. Written and passed on (a fault something handles still runs as before); the first few only,
// in case some code faults on purpose and recovers. The stack runs through the exception dispatcher to the faulting
// frame.
//-----------------------------------------------------------------------------
static constexpr LONG MAX_FAULTS_WRITTEN = 4;

static LONG CALLBACK FaultProbe(EXCEPTION_POINTERS *pInfo)
{
	const DWORD code = pInfo->ExceptionRecord->ExceptionCode;
	const bool bFatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW
		|| code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION
		|| code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_IN_PAGE_ERROR || code == 0xC0000374	// heap corruption
		|| code == 0xC0000409;	// stack buffer overrun (fast fail)
	if (!bFatal || !s_path[0] || !s_run[0])
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}
	const LONG fault = InterlockedIncrement(&s_faults);
	if (fault > MAX_FAULTS_WRITTEN)
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}
	char heading[320];
	HMODULE module = nullptr;
	char name[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCSTR>(pInfo->ExceptionRecord->ExceptionAddress), &module))
	{
		GetModuleFileNameA(module, name, sizeof(name));
	}
	const char *pszBase = strrchr(name, '\\');
	// An access violation's first two values: reading (0), writing (1) or executing (8), and the address.
	char access[64] = "";
	if (code == EXCEPTION_ACCESS_VIOLATION && pInfo->ExceptionRecord->NumberParameters >= 2)
	{
		const ULONG_PTR how = pInfo->ExceptionRecord->ExceptionInformation[0];
		_snprintf_s(access, sizeof(access), _TRUNCATE, ", %s 0x%llx",
			how == 1 ? "writing" : how == 8 ? "executing" : "reading",
			static_cast<unsigned long long>(pInfo->ExceptionRecord->ExceptionInformation[1]));
	}
	_snprintf_s(heading, sizeof(heading), _TRUNCATE, "fault %ld during %s: code 0x%08lx at %s+0x%llx%s", fault, s_run, code,
		pszBase ? pszBase + 1 : name,
		static_cast<unsigned long long>(static_cast<char *>(pInfo->ExceptionRecord->ExceptionAddress)
			- reinterpret_cast<char *>(module)),
		access);
	WriteStack(heading, fault > 1);
	return EXCEPTION_CONTINUE_SEARCH;
}

void NeoBenchExitProbeArm(const char *pszFullPath, const char *pszRun)
{
	static const PVOID s_pFaultProbe = AddVectoredExceptionHandler(1, FaultProbe);	// once, for the process
	(void)s_pFaultProbe;
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
