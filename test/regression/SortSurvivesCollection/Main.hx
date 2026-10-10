// Sorting an array of plain values boxes each element once, for the
// comparator's Dynamic arguments, and sorts an index through the boxes, so the
// boxes must survive any collection the comparator causes. In release builds
// they did not: the sort kept only a pointer into the boxes' buffer, which keeps
// the buffer alive but not the boxes in it, so a collection freed them and the
// comparator was handed freed boxes. On gcc x64 an Int sort crashed in its
// second pass.
//
// Object arrays sort an index too, through a pointer into the array's buffer,
// so an array nothing else holds must be kept alive by the sort itself.
//
// Whether the dropped array is still lying in a callee-saved register depends
// on how the compiler built that copy of Array_obj<T>::sort, which depends on
// the file around it: in the shared test suite, and with all of these in one
// file, it was, and nothing failed. So each sort is a small class of its own,
// in a file of its own, like the program the crash was found in.

final SORTS = 10;
final SIZE = 1024;
final COLLECT_EVERY = 500;

var seed = 12345;
var compares = 0;

function nextRandom():Int {
	seed = (seed * 1103515245 + 12345) & 0x7fffffff;
	return seed;
}

// The comparators also allocate, since the differences they return lie
// outside the cached small ints, but forced collections do not depend on when
// the heap fills
function collectSometimes() {
	if (++compares % COLLECT_EVERY == 0) {
		cpp.vm.Gc.run(true);
	}
}

function main() {
	Sys.println('Int: ${SortInts.run()} of $SORTS sorts wrong');
	Sys.println('Float: ${SortFloats.run()} of $SORTS sorts wrong');
	Sys.println('UInt16: ${SortShorts.run()} of $SORTS sorts wrong');
	Sys.println('UInt8: ${SortBytes.run()} of $SORTS sorts wrong');
	Sys.println('Objects: ${SortObjects.run()} lost');
}
