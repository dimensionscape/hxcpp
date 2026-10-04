import sys.thread.Thread;

// Threads that end while the process exits. After main returns, the C runtime
// runs the static destructors and then ends the process, and a thread that
// ends in between removes its root from the collector's root set as it goes.
// That set was a static itself, destroyed by then: the first thread to end
// faulted in the erase while holding the root lock, and the second waited on
// that lock for ever -- on Windows inside the loader lock, so the process's
// exit waited for it too and never finished.
//
// Rather than wait for the timing to line up, the threads are let go from a
// function the C runtime calls after the static destructors (a .CRT$XPU entry
// with MSVC, a destructor attribute elsewhere), one at a time. On Windows it
// waits for each thread to be gone, and ends the process with code 3 if one
// is still there after five seconds. Elsewhere it gives each a moment.
@:cppFileCode('
#include <stdio.h>
#ifdef HX_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
	const int WORKERS = 2;
	volatile int sRegistered = 0;
	volatile int sGo[WORKERS] = { 0, 0 };
	#ifdef HX_WINDOWS
	HANDLE sThreads[WORKERS] = { 0, 0 };
	#endif

	void registerWorker(int inIndex)
	{
		#ifdef HX_WINDOWS
		DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
			&sThreads[inIndex], SYNCHRONIZE, FALSE, 0);
		InterlockedIncrement((volatile LONG *)&sRegistered);
		#else
		__sync_fetch_and_add(&sRegistered, 1);
		#endif
	}

	int registeredWorkers()
	{
		#ifdef HX_WINDOWS
		return InterlockedCompareExchange((volatile LONG *)&sRegistered, 0, 0);
		#else
		return __sync_fetch_and_add(&sRegistered, 0);
		#endif
	}

	void waitToEnd(int inIndex)
	{
		while (!sGo[inIndex])
		{
			#ifdef HX_WINDOWS
			Sleep(1);
			#else
			usleep(1000);
			#endif
		}
	}

	void endThreadsLate()
	{
		int ended = 0;
		for (int i = 0; i < WORKERS; i++)
		{
			sGo[i] = 1;
			#ifdef HX_WINDOWS
			if (WaitForSingleObject(sThreads[i], 5000) == WAIT_OBJECT_0)
				ended++;
			#else
			usleep(200000);
			ended++;
			#endif
		}
		printf("%d of %d threads ended after the static destructors\\n", ended, WORKERS);
		fflush(stdout);
		#ifdef HX_WINDOWS
		// Not ExitProcess, which waits for the loader lock a stuck thread holds
		if (ended < WORKERS)
			TerminateProcess(GetCurrentProcess(), 3);
		#endif
	}

	#ifdef _MSC_VER
	typedef void (__cdecl *LateExitFunc)(void);
	#pragma section(".CRT$XPU", long, read)
	__declspec(allocate(".CRT$XPU")) LateExitFunc sLateExit = endThreadsLate;
	// Referred to from main, so nothing discards the entry
	bool lateExitInstalled() { return sLateExit != 0; }
	#else
	__attribute__((destructor)) void lateExit() { endThreadsLate(); }
	bool lateExitInstalled() { return true; }
	#endif
}
')
class Main {
	static final WORKERS = 2;

	static function main() {
		for (i in 0...WORKERS) {
			Thread.create(() -> {
				untyped __cpp__("registerWorker({0})", i);
				cpp.vm.Gc.enterGCFreeZone();
				untyped __cpp__("waitToEnd({0})", i);
				cpp.vm.Gc.exitGCFreeZone();
			});
		}
		var registered:Int = 0;
		while (registered < WORKERS) {
			Sys.sleep(0.001);
			registered = untyped __cpp__("registeredWorkers()");
		}
		var installed:Bool = untyped __cpp__("lateExitInstalled()");
		Sys.println(installed ? "main returns" : "no late exit");
	}
}
