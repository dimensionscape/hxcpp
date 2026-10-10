import utest.Test;
import utest.Assert;

class SortData
{
   public var value:Int;
   public var id:Int;
   static var ids = 0;

   public function new()
   {
      value = Std.int(Math.random()*500);
      id = ids++;
   }
}

class TestSort extends Test
{
   public function testObjects()
   {
      var tests = new Array<SortData>();
      for(i in 0...100000)
         tests.push( new SortData() );

      var sorted = tests.copy();
      sorted.sort( function(a,b) return a.value - b.value );

      for(i in 1...sorted.length)
      {
         if (sorted[i].value < sorted[i-1].value)
            throw "Index out of order";
         if (sorted[i].value == sorted[i-1].value &&
               sorted[i].id <= sorted[i-1].id )
            throw "Not stable sort";
      }

      var sorted = tests.copy();
      var compares = 0;
      sorted.sort( function(a,b) {
          // Churn some GC
          var array = new Array<Int>();
          compares++;
          array.push(a.value);
          array.push(b.value);
          return array[0] < array[1] ? 1 : array[0] > array[1] ? -1 : 0;
       });

      //Sys.println("\nCompares per log elements:" + (compares/(tests.length*Math.log(tests.length))));

      for(i in 1...sorted.length)
      {
         if (sorted[i].value > sorted[i-1].value)
            throw "Index out of order";
         if (sorted[i].value == sorted[i-1].value &&
               sorted[i].id <= sorted[i-1].id )
            throw "Not stable sort";
      }

      Assert.pass();
   }

   // A comparator that changes the array's length leaves the sort's index
   // describing values that are gone, so the sort leaves the array as the
   // comparator left it.  It once applied the index anyway, over the new
   // length: past the end of the index when the array grew, which hung or
   // crashed, and over the moved values when it shrank, as here.  Plain
   // values compare through boxes made before the sort, so the comparator
   // still sees consistent values after the change.
   public function testComparatorChangesLength()
   {
      var ints = [ for(i in 0...100) (i * 37) % 100 ];
      var left:Array<Int> = null;
      ints.sort( function(a, b) {
         if (left==null)
         {
            ints.splice(0, 50);
            left = ints.copy();
         }
         return a - b;
      });
      Assert.same(left, ints);
   }

   public function testInconsistentComparator()
   {
      var ints = [ for(i in 0...1000) i ];
      var strings = [ for(i in 0...1000) "s" + i ];
      var objects = [ for(i in 0...1000) new SortData() ];
      for(i in 0...500)
      {
         ints.sort( function(a,b) return Std.random(3) - 1 );
         strings.sort( function(a,b) return Std.random(3) - 1 );
         objects.sort( function(a,b) return Std.random(3) - 1 );
      }

      // The order is unspecified, but no element may be lost
      ints.sort( function(a,b) return a - b );
      for(i in 0...ints.length)
         if (ints[i] != i)
            throw "Int lost";

      var expected = [ for(i in 0...1000) "s" + i ];
      expected.sort( Reflect.compare );
      strings.sort( Reflect.compare );
      for(i in 0...strings.length)
         if (strings[i] != expected[i])
            throw "String lost";

      objects.sort( function(a,b) return a.id - b.id );
      for(i in 1...objects.length)
         if (objects[i].id != objects[i-1].id + 1)
            throw "Object lost";

      Assert.pass();
   }

   public function testSizes()
   {
      // Sizes around the insertion sort runs, with random keys and with
      // ascending and descending keys in runs of equal ones
      var sizes = [ for(i in 0...70) i ].concat([127, 128, 129, 1000, 4097]);
      for(size in sizes)
      {
         for(order in 0...3)
         {
            var keys = [ for(i in 0...size) order==0 ? Std.random(5) : order==1 ? i>>2 : (size-i)>>2 ];
            var tests = [ for(key in keys) new SortData() ];
            for(i in 0...size)
               tests[i].value = keys[i];

            keys.sort( function(a,b) return a - b );
            tests.sort( function(a,b) return a.value - b.value );
            for(i in 0...size)
            {
               if (tests[i].value != keys[i] || (i>0 && keys[i] < keys[i-1]))
                  throw "Index out of order";
               if (i>0 && tests[i].value == tests[i-1].value && tests[i].id <= tests[i-1].id)
                  throw "Not stable sort";
            }
         }
      }

      Assert.pass();
   }

   // An object sort compares the elements as they are at the time, so a
   // comparator that releases the array's buffer must not take it out of range
   public function testComparatorReleasesObjectArray()
   {
      var strings = [ for(i in 0...1000) "s" + i ];
      var released = false;
      strings.sort( function(a,b) {
         if (!released)
         {
            released = true;
            cpp.NativeArray.setSize(strings, 0);
         }
         return Reflect.compare(a,b);
      });
      Assert.equals(0, strings.length);
   }
}

