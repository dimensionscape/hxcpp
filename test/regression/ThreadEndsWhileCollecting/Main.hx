import haxe.Timer;
import sys.thread.Lock;
import sys.thread.Thread;

// Threads that end while another thread collects in a loop. A thread removes
// its root as it ends, after it has unregistered from the collector, so it
// does not wait for a collection: the collector has to hold the root lock
// while it walks the roots, or it walks into the node being erased. On
// Windows the root is removed by the thread's thread_local destructor, inside
// the loader lock, so a walk that needed the loader would hang here instead.
//
// build.hxml builds it against the debug C runtime on Windows, whose checked
// iterators report an iterator whose node was erased under it. Without the
// lock it failed in every run there, in the collector's walk.
@:cppFileCode('
#include <stdio.h>
#ifdef HX_WINDOWS
#include <windows.h>
#include <stdlib.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#endif

namespace
{
	#ifdef HX_WINDOWS
	LONG WINAPI reportCrash(EXCEPTION_POINTERS *inInfo)
	{
		fprintf(stderr, "exception %08lx\\n", (unsigned long)inInfo->ExceptionRecord->ExceptionCode);
		fflush(stderr);
		TerminateProcess(GetCurrentProcess(), 3);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	#ifdef _DEBUG
	void reportInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
	{
		fprintf(stderr, "the C runtime refused an invalid parameter or iterator\\n");
		fflush(stderr);
		TerminateProcess(GetCurrentProcess(), 4);
	}
	#endif
	#endif

	// Failures end the process with a code and a line on stderr: never a
	// dialog, which would wait on the desktop for someone to click it
	void reportFailures()
	{
		#ifdef HX_WINDOWS
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		SetUnhandledExceptionFilter(reportCrash);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		#ifdef _DEBUG
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
		_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
		_set_invalid_parameter_handler(reportInvalidParameter);
		#endif
		#endif
	}
}
')
class Main {
	static final SECONDS = 3.0;
	static final BATCH = 16;

	static function main() {
		untyped __cpp__("reportFailures()");

		var stop:Bool = false;
		var collections:Int = 0;
		var collectorDone = new Lock();
		Thread.create(() -> {
			while (!stop) {
				// Garbage to fill whatever rows a collection frees
				var garbage:Array<Array<Int>> = [for (j in 0...200) [j, j]];
				cpp.vm.Gc.run(true);
				collections++;
			}
			collectorDone.release();
		});

		var ended:Int = 0;
		var deadline:Float = Timer.stamp() + SECONDS;
		while (Timer.stamp() < deadline) {
			var done = new Lock();
			for (_ in 0...BATCH) {
				Thread.create(() -> done.release());
			}
			for (_ in 0...BATCH) {
				done.wait();
			}
			ended += BATCH;
		}
		stop = true;
		collectorDone.wait();

		Sys.println("threads ended while the roots were walked");
		Sys.stderr().writeString('$ended threads ended across $collections collections\n');
	}
}
