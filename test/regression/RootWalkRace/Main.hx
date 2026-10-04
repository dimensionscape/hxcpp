import haxe.Timer;

// Roots added and removed while collections walk them. A thread that is
// starting adds its root before it registers with the collector, one that is
// ending removes it after it has unregistered, and native code can add or
// remove roots from a thread hxcpp never knew: none of them waits for a
// collection, so the collector has to hold the root lock while it walks the
// roots, or it follows nodes that are erased and relinked under it.
//
// Native threads that never attach to hxcpp add and remove roots in a loop
// while the main thread collects, compacting, in a loop. Twenty thousand
// arrays are reachable only through roots of their own; a walk that loses its
// place skips some, and they are freed and their rows reused.
//
// build.hxml builds it against the debug C runtime on Windows, whose checked
// iterators report an iterator whose node was erased under it. Without the
// lock that build failed within a quarter of a second in every run, mostly on
// that check, otherwise reading a freed node; a release build crashed in the
// walk in most runs.
@:cppFileCode('
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <thread>
#include <vector>
#ifdef HX_WINDOWS
#include <windows.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#endif

namespace
{
	std::atomic<bool> sStop(false);
	std::atomic<long long> sChurned(0);
	std::vector<std::thread> sChurners;
	std::vector<hx::Object **> sVictims;

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

	// Never attached to hxcpp, so a collection neither waits for these threads
	// nor stops them. Their roots hold null: the walk passes over the nodes.
	void churn(int inSlots)
	{
		std::vector<hx::Object *> slots(inSlots, nullptr);
		long long churned = 0;
		while (!sStop.load(std::memory_order_relaxed))
		{
			for (int i = 0; i < inSlots; i++)
				hx::GCAddRoot(&slots[i]);
			for (int i = 0; i < inSlots; i++)
				hx::GCRemoveRoot(&slots[i]);
			churned += inSlots;
		}
		sChurned += churned;
	}

	void startChurners(int inThreads, int inSlots)
	{
		for (int i = 0; i < inThreads; i++)
			sChurners.emplace_back(churn, inSlots);
	}

	double stopChurners()
	{
		sStop = true;
		for (size_t i = 0; i < sChurners.size(); i++)
			sChurners[i].join();
		return (double)sChurned.load();
	}

	void rootVictim(hx::Object *inObject)
	{
		hx::Object **root = new hx::Object *(inObject);
		hx::GCAddRoot(root);
		sVictims.push_back(root);
	}

	hx::Object *victim(int inIndex)
	{
		return *sVictims[inIndex];
	}
}
')
class Main {
	static final VICTIMS = 20000;
	static final LENGTH = 8;
	static final THREADS = 4;
	static final SLOTS = 64;
	static final SECONDS = 4.0;

	static function makeVictims():Void {
		for (i in 0...VICTIMS) {
			var values:Array<Int> = [for (j in 0...LENGTH) i + j];
			untyped __cpp__("rootVictim({0}.mPtr)", values);
		}
	}

	static function lostVictims():Int {
		var lost:Int = 0;
		for (i in 0...VICTIMS) {
			var values:Array<Int> = untyped __cpp__("Dynamic(victim({0}))", i);
			if (values == null || values.length != LENGTH) {
				lost++;
				continue;
			}
			for (j in 0...LENGTH) {
				if (values[j] != i + j) {
					lost++;
					break;
				}
			}
		}
		return lost;
	}

	static function main() {
		untyped __cpp__("reportFailures()");
		makeVictims();
		untyped __cpp__("startChurners({0}, {1})", THREADS, SLOTS);

		var collections:Int = 0;
		var deadline:Float = Timer.stamp() + SECONDS;
		while (Timer.stamp() < deadline) {
			// Garbage to fill whatever rows a collection frees
			var garbage:Array<Array<Int>> = [for (j in 0...500) [j, j, j, j]];
			cpp.vm.Gc.run(true);
			collections++;
		}

		var churned:Float = untyped __cpp__("stopChurners()");
		var lost:Int = lostVictims();
		Sys.println(lost == 0 ? "every rooted array survived" : '$lost of $VICTIMS rooted arrays were lost');
		Sys.stderr().writeString('$collections collections while ${churned} roots were added and removed\n');
	}
}
