// Regression test for hx::Anon_obj fixed-field deletion.
//
// Background: anonymous-object literals store their fields in an inline
// "fixed" array sized at construction. Reflect.deleteField removes a fixed
// field by shifting the following elements down. A previous off-by-one in the
// shift loop read one VariantKey past the fixed array (out-of-bounds / UB).
//
// Note: because the stray read landed in a slot that is immediately discarded,
// the *visible* behaviour was already correct, so this test is a behavioural
// regression guard rather than a sanitizer trip-wire. It exercises deletion at
// the front/middle/back and down to empty, across object sizes that span the
// fixed-array fast paths and the hash-map fallback.

class Test {
    static var failures = 0;

    static function check(cond:Bool, msg:String) {
        if (!cond) {
            Sys.println('FAIL: $msg');
            failures++;
        }
    }

    static function fieldsOf(o:Dynamic):Array<String> {
        var f = Reflect.fields(o);
        f.sort(Reflect.compare);
        return f;
    }

    static function expectFields(o:Dynamic, expected:Array<String>, ctx:String) {
        var got = fieldsOf(o);
        var exp = expected.copy();
        exp.sort(Reflect.compare);
        check(got.join(",") == exp.join(","), '$ctx: fields [${got.join(",")}] != [${exp.join(",")}]');
        for (name in expected)
            check(Reflect.hasField(o, name), '$ctx: missing field "$name"');
    }

    // Delete each field one at a time, in the given order, verifying integrity
    // after every removal. Field values are name -> index so we can re-check.
    static function runDeletionOrder(size:Int, order:Array<Int>) {
        var o:Dynamic = {};
        var names = [for (i in 0...size) "f" + i];
        for (i in 0...size) Reflect.setField(o, names[i], i);
        expectFields(o, names, 'size=$size init');

        var remaining = names.copy();
        for (idx in order) {
            var name = names[idx];
            var removed = Reflect.deleteField(o, name);
            check(removed, 'size=$size delete "$name" returned false');
            remaining.remove(name);

            check(!Reflect.hasField(o, name), 'size=$size "$name" still present after delete');
            expectFields(o, remaining, 'size=$size after deleting "$name"');
            // surviving values must be intact
            for (r in remaining) {
                var v:Int = Reflect.field(o, r);
                var expectedVal = Std.parseInt(r.substr(1));
                check(v == expectedVal, 'size=$size field "$r" value $v != $expectedVal');
            }
        }
        check(fieldsOf(o).length == 0, 'size=$size not empty after deleting all');

        // deleting a non-existent / already-removed field returns false
        check(!Reflect.deleteField(o, names[0]), 'size=$size delete of absent field returned true');
    }

    static function main() {
        // Cross the fixed-array fast paths (<5) and the binary-search path (>=5),
        // and the hash-map fallback boundary.
        for (size in 1...8) {
            runDeletionOrder(size, [for (i in 0...size) i]);          // front-to-back
            runDeletionOrder(size, [for (i in 0...size) size - 1 - i]); // back-to-front (hits the old OOB case)
            // middle-out rotation: a valid permutation starting from the middle
            runDeletionOrder(size, [for (i in 0...size) (Std.int(size / 2) + i) % size]);
        }

        // Add fields beyond the literal shape (forces hash-map fallback) then delete.
        var o:Dynamic = {a: 1, b: 2};
        Reflect.setField(o, "c", 3);
        Reflect.setField(o, "d", 4);
        expectFields(o, ["a", "b", "c", "d"], "mixed init");
        check(Reflect.deleteField(o, "b"), "mixed delete fixed 'b'");   // fixed field
        check(Reflect.deleteField(o, "c"), "mixed delete hashed 'c'");  // hash-map field
        expectFields(o, ["a", "d"], "mixed after deletes");
        check((Reflect.field(o, "a") : Int) == 1 && (Reflect.field(o, "d") : Int) == 4, "mixed values intact");

        if (failures == 0) {
            Sys.println("OK: all Anon deleteField regression checks passed");
            Sys.exit(0);
        } else {
            Sys.println('FAILED: $failures check(s)');
            Sys.exit(1);
        }
    }
}
