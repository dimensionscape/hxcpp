class SortInts {
	public static function run():Int {
		final source = [for (i in 0...Main.SIZE) Main.nextRandom() % 1000000];
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
