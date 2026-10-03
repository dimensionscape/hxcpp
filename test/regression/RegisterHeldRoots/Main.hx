import sys.thread.Lock;
import sys.thread.Thread;

// A worker keeps an array only in a local while it loops through safe points,
// or through GC-free zones, and the main thread forces collections and
// allocates. On Windows x64 the local sat in a callee-saved register that the
// pause did not capture, so the array was freed under the worker, its rows were
// refilled with the main thread's garbage, and the worker's sums went wrong or
// it faulted.
//
// Whether the compiler keeps the array only in a register depends on the code
// around it, so each worker is a function of its own, called directly.

final SIZE = 18;
final ROUNDS = 400000;
final ATTEMPTS = 10;

function sumAcrossSafePoints():Int {
	var values:Array<Int> = [];
	for (i in 0...SIZE) {
		values.push(i + 1);
	}
	// A second array made the same way reuses the stack slot the first was
	// returned through, so the loop holds 'values' only in a register
	var other:Array<Int> = [];
	other.push(SIZE);
	var sum:Int = other[0] - SIZE;
	for (r in 0...ROUNDS) {
		for (i in 0...SIZE) {
			sum += values[i];
		}
		cpp.vm.Gc.safePoint();
	}
	return sum;
}

function sumAcrossFreeZones():Int {
	var values:Array<Int> = [];
	for (i in 0...SIZE) {
		values.push(i + 1);
	}
	var other:Array<Int> = [];
	other.push(SIZE);
	var sum:Int = other[0] - SIZE;
	for (r in 0...ROUNDS) {
		for (i in 0...SIZE) {
			sum += values[i];
		}
		cpp.vm.Gc.enterGCFreeZone();
		cpp.vm.Gc.exitGCFreeZone();
	}
	return sum;
}

function collectUntil(finished:Void->Bool):Void {
	while (!finished()) {
		// Garbage to fill whatever rows the collection frees
		var garbage:Array<Array<Int>> = [for (j in 0...2000) [j, j, j, j]];
		cpp.vm.Gc.run(true);
	}
}

function main() {
	var expected:Int = ROUNDS * (SIZE * (SIZE + 1) >> 1);

	var wrong:Int = 0;
	for (attempt in 0...ATTEMPTS) {
		var done:Lock = new Lock();
		var result:Int = 0;
		var finished:Bool = false;
		Thread.create(() -> {
			result = sumAcrossSafePoints();
			finished = true;
			done.release();
		});
		collectUntil(() -> finished);
		done.wait();
		if (result != expected) {
			wrong++;
		}
	}
	Sys.println('safe points: $wrong of $ATTEMPTS sums wrong');

	wrong = 0;
	for (attempt in 0...ATTEMPTS) {
		var done:Lock = new Lock();
		var result:Int = 0;
		var finished:Bool = false;
		Thread.create(() -> {
			result = sumAcrossFreeZones();
			finished = true;
			done.release();
		});
		collectUntil(() -> finished);
		done.wait();
		if (result != expected) {
			wrong++;
		}
	}
	Sys.println('free zones: $wrong of $ATTEMPTS sums wrong');
}
