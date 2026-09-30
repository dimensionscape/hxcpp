/**
	A timed sys.thread.Lock.wait comes due about when it was asked to.

	On Windows the lock took its deadline from GetTickCount64, which moves
	only on the ~15.6ms system tick, so a wait ended at the first tick
	after its deadline: with the timer resolution at 1ms, as a program that
	paces itself on these waits sets it, a 2ms wait took 15ms. Exits
	non-zero when the median overshoot of a short wait passes 4ms.
**/
@:cppFileCode('
#ifdef HX_WINDOWS
#include <windows.h>
// winmm looked up rather than linked, so the build needs no extra library
static void lockwaitTimerResolution(bool inFine)
{
   typedef unsigned int (WINAPI *PeriodFunc)(unsigned int);
   HMODULE winmm = LoadLibraryA("winmm.dll");
   PeriodFunc period = winmm ? (PeriodFunc)GetProcAddress(winmm, inFine ? "timeBeginPeriod" : "timeEndPeriod") : 0;
   if (period)
      period(1);
}
#else
static void lockwaitTimerResolution(bool) { }
#endif
')
class Test {
	static function main() {
		untyped __cpp__("lockwaitTimerResolution(true)");
		var lock = new sys.thread.Lock();
		var overshoots:Array<Float> = [];
		for (_ in 0...25) {
			var start = haxe.Timer.stamp();
			lock.wait(0.002);
			overshoots.push((haxe.Timer.stamp() - start - 0.002) * 1000);
		}
		untyped __cpp__("lockwaitTimerResolution(false)");
		overshoots.sort(Reflect.compare);
		var median = overshoots[overshoots.length >> 1];
		Sys.println('median overshoot of a 2ms wait: ${Math.round(median * 10) / 10}ms');
		if (median > 4) {
			Sys.println("FAILED: timed waits come due late");
			Sys.exit(1);
		}
		Sys.println("OK: timed waits come due on time");
	}
}
