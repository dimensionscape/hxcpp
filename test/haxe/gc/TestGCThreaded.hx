package gc;

import utest.Test;
import utest.Assert;
import haxe.io.Bytes;
import haxe.crypto.Md5;
import cpp.vm.Gc;
import sys.io.File;
#if haxe4
import sys.thread.Lock;
import sys.thread.Thread;
#else
import cpp.vm.Thread;
#end

class Wrapper
{
  public var a:Int;
  public function new(inA:Int) a = inA;
}


@:cppInclude("./ZoneTest.cpp")
@:depend("./ZoneTest.cpp")
class TestGCThreaded extends Test
{
  @:keep
  static var keepRunning = false;
  @:keep
  static var nativeRunning = false;
  var bigText:String;


   function setup()
   {
      var lines = [ for(i in 0...100000) "abc123\n" ];
      #if nme
      bigText = lines.join("");
      #else
      File.saveContent( "gc/big.txt", lines.join("") );
      #end
   }

   public function testThreadOnce():Void
   {
      startNative();
      doThreadedWork(4,100);
      stopNative();
      Assert.pass();
   }

   public function testThreadMany():Void
   {
      startNative();
      for(i in  0...10)
         #if nme
         doThreadedWork(4,100);
         #else
         doThreadedWork(100,100);
         #end
      stopNative();
      Assert.pass();
   }

   #if haxe4
   // A worker holds an array only in a local across safe points while this
   // thread forces collections and allocates. On Windows x64 the local sits in a
   // callee-saved register, which the pause did not capture: the array was
   // freed under the worker, and its rows refilled with this thread's garbage.
   public function testSafePointKeepsRegisterHeldObjects():Void
   {
      var expected = SAFE_POINT_ROUNDS * (SAFE_POINT_SIZE * (SAFE_POINT_SIZE + 1) >> 1);
      for(attempt in 0...4)
      {
         var done = new Lock();
         var result = 0;
         var finished = false;
         Thread.create( () -> {
            result = sumAcrossSafePoints();
            finished = true;
            done.release();
         });
         while(!finished)
         {
            var garbage = [ for(i in 0...2000) [i, i, i, i] ];
            Gc.run(true);
         }
         done.wait();
         Assert.equals(expected, result);
      }
   }

   static inline var SAFE_POINT_SIZE = 18;
   static inline var SAFE_POINT_ROUNDS = 200000;

   static function sumAcrossSafePoints():Int
   {
      var values = new Array<Int>();
      for(i in 0...SAFE_POINT_SIZE)
         values.push(i + 1);
      // A second array made the same way reuses the stack slot the first was
      // returned through, so the loop holds 'values' only in a register
      var other = new Array<Int>();
      other.push(SAFE_POINT_SIZE);
      var sum = other[0] - SAFE_POINT_SIZE;
      for(round in 0...SAFE_POINT_ROUNDS)
      {
         for(i in 0...SAFE_POINT_SIZE)
            sum += values[i];
         Gc.safePoint();
      }
      return sum;
   }
   #end

   @:native("nativeLoop")
   extern static function nativeLoop() : Void;

   function startNative()
   {
      Thread.create( () -> {
         nativeRunning = true;
         keepRunning = true;
         nativeLoop();
      });
   }
   function stopNative()
   {
      keepRunning = false;
      while(nativeRunning)
         Sys.sleep(0.1);
   }

   function doThreadedWork(numThreads, numWork):Void
   {
      var threads:Array<Thread> = makeThreads(numThreads);

      for (i in 0...numWork)
         threads[i % threads.length].sendMessage('doWork');

      for (i in 0...numThreads)
      {
         threads[i].sendMessage('exit'); 
         Thread.readMessage(true);
      }
   }

   function makeThreads(numThreads:Int):Array<Thread>
   {
      #if nme
      var text:String = bigText;
      #else
      var text:String = File.getContent("gc/big.txt");
      #end
      var main:Thread = Thread.current();
      var threads:Array<Thread> = [];
      for (i in 0...numThreads)
      {
         threads.push( Thread.create(function() {
            while(true) {
               var message:Dynamic = Thread.readMessage(true);
               if(message == 'exit')
                  break;
               else
                  Md5.encode(text);
                  var arrays = new Array<Array<Wrapper>>();
                  for(i in 0...100)
                  {
                     var wrappers = new Array<Wrapper>();
                     arrays.push(wrappers);
                     for(j in 0...1000)
                        wrappers.push( new Wrapper(1) );
                  }
                  var sum = 0;
                  for(a in arrays)
                     for(w in a)
                        sum += w.a;

                  Assert.equals(100000, sum);
            }
            main.sendMessage('done');
         }) );
      }
      return threads;
   }

}

