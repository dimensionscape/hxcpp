// Every byte value boxes to a cached constant, so no box can be lost here, but
// the small types take the same path
class SortBytes {
	public static function run():Int {
		final source = new Array<cpp.UInt8>();
		for (i in 0...Main.SIZE) {
			source.push(Main.nextRandom() & 0xff);
		}
		final expected = source.copy();
		haxe.ds.ArraySort.sort(expected, (a, b) -> a - b);
		final array = source.copy();
		var wrong = 0;
		for (s in 0...Main.SORTS) {
			for (i in 0...array.length) {
				array[i] = source[i];
			}
			array.sort((a, b) -> {
				Main.collectSometimes();
				a - b;
			});
			for (i in 0...array.length) {
				if (array[i] != expected[i]) {
					wrong++;
					break;
				}
			}
		}
		return wrong;
	}
}
