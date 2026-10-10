import cpp.vm.WeakRef;

private class Item {
	public final value:Int;

	public function new(value:Int) {
		this.value = value;
	}
}

// Nothing but the array holds these items, and nothing uses the array after its
// sort, so the comparator watches them through weak references
class SortObjects {
	public static function run():Int {
		var lost = 0;
		for (s in 0...Main.SORTS) {
			final items = [for (i in 0...Main.SIZE) new Item(Main.nextRandom() % 1000000)];
			final watched = [for (item in items) new WeakRef(item)];
			var compares = 0;
			items.sort((a, b) -> {
				if (++compares % Main.COLLECT_EVERY == 0) {
					cpp.vm.Gc.run(true);
					for (item in watched) {
						if (item.get() == null) {
							lost++;
						}
					}
				}
				a.value - b.value;
			});
		}
		return lost;
	}
}
