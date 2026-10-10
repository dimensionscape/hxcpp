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
}

