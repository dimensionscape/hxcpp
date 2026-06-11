
4.3.xx
------------------------------------------------------------

* Added support for killing processes on OS' other than Windows, Apple TV, and Apple Watch
* Added register capturing on Linux
* Added HXCPP_CPP17 define
* Added ASM and NASM support to build xml
* Added support for name and value entries to build xml
* Added tracy profiler
* Added support for Android NDKs higher than 21
* Added x86_64 support to older Android NDKs
* Added optional detaching of main thread

* Improved Map/StringMap/IntMap lookup performance by ~16-26% by lowering the default hash table load factor
* Fixed catastrophic IntMap collisions for keys with low-bit structure (pointers, aligned/strided ids) by mixing integer hashes; up to ~280x faster lookups for such keys with no regression for dense keys
* Applied the same hash mixing to Int64 and object map keys (fixes ~168x slower lookups for strided Int64 keys; modest gain for object keys)
* Reduced large-map build time ~24-36% by switching hash table growth from 2x to 4x once a map is large (small maps keep 2x, so their memory is unchanged)
* Improved array slice/splice by skipping the generational GC write barrier for arrays of primitive (non-pointer) types
* Increased Socket.read() chunk buffer from 256 bytes to 16KB, cutting recv() syscalls ~64x when reading a stream to EOF
* Sped up String.indexOf on byte strings using memchr: ~58x faster for sparse/absent single-char search (the common contains check) and ~2.5x faster for multi-char search
* Sped up String.toUpperCase/toLowerCase ~2.3x for ASCII strings with a branchless transform instead of per-char locale-dependent toupper/tolower
* Sped up Date field access ~3.7x by caching the last localtime/gmtime conversion per thread (getHours/getMinutes/getFullYear/... on one date no longer each call localtime)
* Sped up Int/Int64/UInt64 to String conversion ~2.5x by writing digits directly instead of snprintf (affects Std.string of integers, string interpolation, etc.)
* Replaced the C rand() backing of Math.random/Std.random with a per-thread xoshiro256++ generator: ~10x faster, no libc lock contention, 53-bit doubles, and worker threads no longer produce identical sequences on Windows (also fixes Std.random(0) crashing with division by zero)
* Sped up String.split with a memchr candidate scan instead of a libc compare per byte position (~15-20% on byte strings including the per-part allocation, much more on the raw scan), added a first-unit skip to the wide-string path, and fixed delimiters containing NUL falsely matching any NUL in the subject
* Sped up haxe.io.Bytes.ofString ~25% for non-ASCII strings by sizing the output once and encoding directly into the buffer instead of pushing per byte
* Reduced GC pause time for programs holding many large (4KB+) allocations: conservative stack marking now rejects out-of-range candidates against cached bounds instead of scanning the whole large-object list per stack word, and the last-value dedupe in the conservative marker actually works now
* Enabled the PCRE2 JIT for EReg: 3.7-6.2x faster matching/search/replace in benchmarks. The JIT was compiled in as a disabled stub and never invoked. Falls back to the interpreter automatically where executable memory is unavailable; disabled at build time on iOS/tvOS/watchOS/emscripten/WinRT, opt out anywhere with -D HXCPP_PCRE_NO_JIT
* Sped up indexOf/lastIndexOf on UTF-16 (non-ASCII) strings 2-3.5x with first-unit candidate skipping, and made searching a byte string for an unmatchable wide needle O(1) instead of a full scan
* Fixed sys.thread.Lock timed waits breaking after ~24.8 days of process uptime on Windows (was built on the 32-bit clock(); now uses the monotonic 64-bit tick count)
* Reduced marker-thread cache-line contention by skipping redundant row-mark stores during the GC mark phase
* Fixed a data race in the large-allocation recycle list: the lock-free probe now reads a dedicated counter instead of scanning the vector concurrently with mutation (also fixed a skipped-entry bug when the locked recheck failed)
* Sped up freeing large allocations on the array/Bytes growth path ~36% in realloc-heavy benchmarks by searching the large-object list from the end, where the just-allocated buffer lives
* Reduced Dynamic function call overhead at API level 500 (Haxe 5): arguments are written into a pre-sized array instead of pushed one at a time with per-element capacity checks
* Made the GC-safe-zone handshake lock-free when no collection is pending: entering skips the kernel event signal and exiting skips the process-global mutex unless a collect is actually starting or running. Combined with uncontended fast paths in sys.thread.Mutex/ConditionVariable/Semaphore (try-lock before entering the zone), uncontended Mutex.acquire/release is ~18x faster and Semaphore ~2x, and independent threads no longer serialize on a global lock for every blocking call
* Fixed Dynamic + with boxed Int64 operands producing a string concatenation of the two numbers; Int64 addition now stays in 64-bit math (no precision loss via double), and Int + Int keeps an Int-typed result consistent with - and * (also avoids boxing a double per addition)
* Made Std.parseFloat and Float-to-String formatting independent of the process locale: host frameworks (GUI toolkits, audio libraries, plugin containers) that call setlocale after startup no longer flip the decimal separator and break float parsing, printing, and serialization (uses explicit "C" numeric locale on Windows, macOS, glibc and Android 21+)
* Fixed equal strings hashing differently and missing each other in string maps in two cases: strings created from bytes (haxe.io.Bytes.toString, file/socket reads of non-ASCII text) stored their pre-computed hash at an address the hash reader did not use for UTF-16 strings, and strings containing astral-plane characters (emoji) hashed surrogate halves individually while compile-time literals hashed real UTF-8 - so a literal key and an equal runtime-built key could not find each other
* Sped up string maps with runtime-built keys 2.2-2.4x: strings of 8+ chars now memoize their hash on first use in a reserved slot after the terminator instead of re-hashing the whole string on every map operation
* Sped up Array.sort with scalar elements ~23-33% by boxing comparator arguments once per element instead of twice per comparison
* Fixed Sys.sleep with a negative duration hanging ~49.7 days on Windows (schedulers computing sleep(deadline-now) could dip below zero)
* Fixed FileSystem.kind/isDirectory misclassifying special files (the S_IF* constants were tested as bit flags: sockets reported as "file", block devices as "dir")
* Fixed File.getContent/getBytes silently returning truncated data for huge files (32-bit length math), leaking the file handle on the length-error path, and now reporting "file too large" instead of wrapping
* Fixed FileSystem.fullPath on Windows returning uninitialized memory for paths longer than MAX_PATH, and a one-character stack overflow in readDirectory at exactly MAX_PATH
* Fixed an operator-precedence bug destroying every mbedtls error code reported from SSL (eleven call sites reported "UNKNOWN ERROR CODE (0001)" instead of the real failure)
* Fixed sys.ssl writeByte silently dropping the byte on would-block/error and readByte turning a non-blocking retry into a spurious end-of-file
* Sped up sys.ssl Socket.read ~64x fewer native calls per TLS record (256-byte drain buffer -> 16KB)
* Sped up print/println on Windows by caching the stdout console probe (was a kernel call per print), and only force-flushing per line when stdout is interactive (a redirected stream no longer pays a write syscall per println)
* Fixed cppia array access through host-class getters returning garbage (the linked getter was validated but never invoked - the argument frame was read back as the result)
* Fixed cppia float constants parsing locale-dependently (comma-decimal hosts)
* Sped up Std.isOfType(x, Int) by replacing RTTI dynamic_casts with cheap type probes
* Fixed comparing a statically-typed number with a Dynamic skipping the runtime type check: 5 == ("5":Dynamic) was true (the string was parsed as a number) and 0 compared equal to non-numeric objects; mixed string/number comparisons also answered != incorrectly
* Fixed SQLite 64-bit INTEGER columns (timestamps, large ids, SUM aggregates) being silently truncated to 32 bits - values that fit stay Int, larger ones widen to Float; last_insert_id saturates instead of wrapping
* Fixed ++/-- on Dynamic values and untyped fields rewriting Int values as Float (disabling downstream Int fast paths) - Int values now stay Int
* Improved Sys.time() resolution on Windows from ~15.6ms to sub-microsecond (GetSystemTimePreciseAsFileTime with a Win7 fallback)
* Fixed a race between Sys.getEnv and Sys.putEnv (putenv can free the storage a concurrent getenv result points into) - environment access is now serialized
* Sped up File.getContent by reading directly into the string buffer (was staged through a std::vector, doubling peak memory with an extra full copy)
* Fixed converting a null function value across compatible Callable signatures producing a non-null callable (the universal `if (callback != null) callback()` idiom then threw instead of skipping)
* Fixed truncated/corrupt .cppia files driving the loader past the end of the buffer (the byte reader now reports EOF like the other stream primitives)
* Added a script stack overflow guard to cppia: deep recursion now throws a catchable "Stack Overflow" instead of silently corrupting the heap past the fixed script stack (checked in the interpreter entries and in jitted function prologues)
* Fixed the cppia JIT writing before the array buffer for stores with a negative index (heap corruption; the write is now dropped, matching compiled code)
* Fixed jitted calls that omit two or more optional arguments corrupting the callee frame (each omitted argument advanced the frame twice)
* Fixed swapped register-class tags in jitted int/float conversions, which miscounted scratch registers
* Sped up jitted Int % Int ~7.6x: a real integer division (with a bailout for divisors 0 and -1) instead of two int-to-double conversions and a native fmod call per operation
* Fixed the jitted stack-overflow guard raising a C++ exception, which cannot unwind through jitted frames (crashed instead of throwing catchably) - it now follows the runtime's stored-exception convention
* Sped up Type.resolveClass ~3.7x by replacing the class registry's ordered map (full string comparisons per tree level) with a hash map keyed on the precomputed permanent-string hashes, and fixed runtime cppia class registration racing unsynchronized against concurrent Type.resolveClass
* Sped up Type.getInstanceFields ~5.7x by caching the computed field list per class (metadata is immutable after registration; callers receive copies)
* CFFI val_id field lookups no longer allocate a std::string per call (transparent comparator)
* Sped up cppia dynamic field access ~1.7-2.3x (get/set/Reflect.field on script classes): each class now builds a hash map of its members at link time instead of scanning functions, dynamic functions and variables linearly with a string compare per entry on every access
* Sped up interpreted cppia switch statements with constant integer cases (including over int expressions the runtime types as float, like %): the body is found with one hash probe instead of re-running every case condition per execution (~20% on a 12-case switch including loop overhead; the win grows with case count)
* Sped up interpreted Int % Int: both operands now stay in integer math (with a bailout for divisors 0 and -1) instead of two double conversions and a native fmod per operation
* Calling a Dynamic value that is not a function now throws "Cannot call ..." at API level 500 (Haxe 5) instead of silently returning null, matching the other targets; API level 430 and below keep the old behavior
* TLS connections now require TLS 1.2 or newer - the bundled mbedtls preset still negotiated the deprecated TLS 1.0/1.1 with downgrade-capable peers
* Fixed Socket.select smashing the stack on Linux/macOS when a descriptor number reaches FD_SETSIZE (1024) - the guard only counted sockets, valid on Windows where fd_set is a counted array, while posix fd_set is a fixed bitmap; selecting on a closed socket (fd -1) had the same effect and both now throw a descriptive error
* Fixed sys.net/sys.ssl reads and writes that hold a raw pointer into a GC array across a blocking call: TLS reads/writes now stage through a stack buffer (mbedtls re-enters the GC-visible world mid-call) and plain socket send/recv pin the buffer in the scanned frame, closing a heap-corruption window in moving-GC builds
* Fixed Socket.connect doing a dynamic field lookup (and potentially throwing) inside the GC free zone - a caught "Invalid socket handle" left the thread permanently marked as parked, letting the collector run concurrently with live code
* Fixed Socket.setTimeout: a negative value configured a random timeout from uninitialized stack memory on posix (now throws), a sub-millisecond value on Windows rounded to 0 = block forever (now 1ms minimum), and huge values overflowed; setting the timeout now reports failure instead of silently doing nothing
* A UDP datagram larger than the receive buffer on Windows now returns the truncated data like posix instead of throwing a spurious "EOF" and discarding it (WSAEMSGSIZE)
* Added the missing EINTR retry to socket send and accept - any signal (profilers, child-process reaping) made them throw a bogus "EOF" and the connection got closed; an interrupted blocking connect now reports "Blocking" (the connect continues asynchronously per posix) instead of "EOF"
* Socket listen and setBlocking failures now throw instead of failing silently (a failed listen left the app believing it was accepting connections); shutdown throws except for the defensive not-connected case
* Fixed Socket.select on Windows reporting errno (always garbage) instead of WSAGetLastError, and both selects now sample the error code before leaving the GC free zone, which can clobber it
* Fixed socket poll reporting sockets from a previous poll as ready after a poll error (stale index lists are now terminated)
* Accepted sockets now inherit close-on-exec (posix) and SO_NOSIGPIPE (macOS) like freshly created ones - accepted connections leaked into child processes, and a client reset could SIGPIPE-kill a macOS server
* Fixed resolving "255.255.255.255" (UDP limited broadcast) throwing "Unknown host" - inet_addr's error value collides with the broadcast address
* Socket close no longer retries on EINTR (posix releases the descriptor regardless, so the retry could close a descriptor just handed to another thread) and runs in the GC free zone so a lingering close cannot stall collection
* Hardened the socket/TLS buffer bounds checks against integer overflow in position+length
* Fixed sys.ssl Certificate loading with a null CA chain crashing (the null check tested the wrong handle), and message digests over-allocating their result 4x
* Fixed GC finalizer-map corruption when a finalizer unregisters itself or another finalizer: dropping an unclosed haxe.zip Compress/Uncompress (whose finalizer calls the same close() as manual cleanup) invalidated the iterator the GC was holding - undefined behavior on every such collection. Dead entries are now unregistered before their callbacks run
* Fixed Array<Dynamic>.concat/blit with an empty untyped operand silently corrupting the receiver: it installed byte storage, so a later push(300) stored 44 and push(-1) stored 255
* Fixed Array<Dynamic>.resize on a fresh untyped array: the result reported length 0, pop/shift/copy/slice/concat ignored the elements, and the first typed push rewrote the null elements as 0/false
* Fixed untyped-array equality operators: comparing a wrapped array with null was inverted, == against a typed array permanently froze and retyped the array as a side effect of comparing, and == / != could both be true for the same operands (now identity comparison, like all other array comparisons)
* Fixed memcmp on an empty untyped array returning inverted results (reported two empty arrays as different and an empty vs non-empty as equal)
* Fixed bool-element untyped arrays mislabeling their storage after copy/slice/concat/splice, which made a later push(5) silently store true
* Out-of-range reads on an untyped array now return null instead of a boxed 0/false from the typed backing store
* Mixing Int64 and Float values in an untyped array now promotes to object storage instead of silently rounding Int64 values beyond 2^53 through a double
* haxe.zip.Uncompress.run no longer reallocates and copies the whole accumulated output per 64KB chunk (quadratic - decompressing 100MB copied ~80GB), and guards against 2GB overflow instead of writing past the output
* A zlib stream requesting a preset dictionary (FDICT) now throws instead of looping forever (denial of service on hostile input); zlib failures now include the error code and zlib's message text instead of a bare "ZLib Error"
* Fixed haxe.zip streaming buffers held as raw pointers across the GC free zone while zlib runs (moving-GC corruption window), an uninitialized flush mode for unknown mode strings, Compress.run copying its whole result an extra time to shrink it, and using a closed Uncompress reporting "Compress closed"
* Writing to a dead child process's stdin no longer kills the whole host process via SIGPIPE on Linux/macOS - it throws like other closed-stream writes (SIGPIPE is now ignored process-wide once sys.io.Process is used)
* Process.kill/exitCode/getPid after close() now throw instead of operating on a recycled OS handle - kill() could terminate an unrelated process and exitCode() hang forever
* A child process killed by a signal (crash, kill()) no longer reports exit code 0: exitCode() returns 128+signal (shell convention); a command that cannot be executed reports 127 instead of 1
* Fixed Process.exitCode(false) on Windows conflating exit code 259 with "still running" (could poll forever) and reading an uninitialized exit code when the status query failed; on Linux a stale EINTR turned the non-blocking poll into a 100% CPU busy-wait
* Process pipe reads/writes now retry on EINTR instead of reporting a bogus EOF mid-stream (silent output truncation under signals), and a failed stdin write throws instead of making writeFullBytes spin forever
* Process pipes are now close-on-exec: concurrently spawned children no longer inherit each other's descriptors, which held stdout/stderr open so reads hung past child exit; closing an unconsumed process also reaps the zombie if it already exited
* Fixed sys.io.Process resource leaks: a failed CreateProcess (e.g. executable not found) leaked six pipe handles per attempt on Windows, partial pipe/fork failures leaked descriptors on posix, and an unchecked CreatePipe could close arbitrary process handles via uninitialized stack values
* The forked child no longer allocates GC memory in the exec-failure path (deadlock risk in multithreaded apps) and uses _exit instead of running the parent's atexit handlers; a quote embedded in the Windows command name is rejected instead of smuggling extra arguments
* Fixed every Thread.create on Windows leaking a kernel thread handle for the process lifetime
* An uncaught exception in a sys.thread.Thread now prints the exception instead of silently terminating the whole process with no diagnostic; the thread's closure is released when it finishes instead of being pinned by the thread handle
* Fixed native threads that touch a Haxe thread API leaking a semaphore (a kernel event handle on Windows) per thread, and Lock.wait/timed waits overflowing for very large timeout values
* Fixed a latent use-after-free in the ndll primitive cache: cache keys were made permanent only after insertion (which changes a local copy, not the stored key), so any cpp.Lib.load after a GC cycle compared against freed string data
* Fixed == on loaded ndll primitives returning true exactly when they were different functions (inverted comparison contract)
* The plugin loader (module registry, primitive cache, kind registry, search paths) is now thread-safe; failed lookups no longer permanently grow the registries; __hxcpp_unload_all_libraries clears the caches so later loads cannot use freed module handles or call into unmapped libraries
* "Could not load module" errors now include the OS loader detail (dlerror/Windows error code) - a missing dependent library or wrong bitness was indistinguishable from file-not-found
* CFFI fixes: buffer_to_string returned an unterminated string aliasing the live buffer (later appends mutated the result, C consumers overread); val_fun_nargs reported arbitrary objects as varargs functions instead of faNotFunction; val_to_buffer/val_array_push silently failed for dynamically typed arrays; an abstract allocated with a size but no finalizer leaked its payload on collection; loaded primitives report their real arity
* Haxelib resolution fixes: non-ASCII Windows home directories no longer break ndll lookup (wide environment reads, copied instead of aliasing CRT storage), trailing whitespace in .current/.dev files is trimmed, and pushing an empty dll path no longer inserts the filesystem root into the search list
* Debugger fixes: an inverted condition silently disabled all breakpoints after toggling execution trace (and ran the per-line handler forever with none set); thread-terminated events always reported thread -1; querying a thread that exited during a break-all crashed the debugged process (NULL map entry); reading stack variables could deadlock the whole process (GC allocation under the debugger lock); break-all no longer stalls every thread for the 2s timeout; replacing breakpoints freed the old set while unlocked readers could still dereference it; stepping state is atomic (lost step counts with multiple threads); a detach racing a stop no longer calls a null handler; execution trace logged every line twice
* Math.round/fround no longer double-round (the largest double below 0.5 rounded to 1 where every other target gives 0), Math.floor/ceil/round of NaN is a defined 0 on every platform (was undefined per-platform behavior), and Type.getClassFields(Math) reports the full field list
* cpp.encoding hardening (new marshal API surface): Ascii.decode no longer truncates at the first NUL byte; malformed UTF-8 (bad continuation bytes, overlong forms, codepoints beyond U+10FFFF) throws instead of producing mojibake, desynchronized decodes, or leaking uninitialized memory into the decoded String; decoded ASCII/char strings are properly NUL-terminated; a lone trailing surrogate reports "Invalid UTF16" instead of an internal view error; the all-ASCII detection is a byte scan instead of a triple decode
* Sped up sys.thread.Deque and the thread message queue ~25x for deep queues (200k add+drain: 1070ms -> 43ms): popping advances a head offset with amortized compaction instead of memmoving the entire remaining queue per pop while holding the lock; uncontended queue operations also skip two GC-free-zone transitions via a try-lock fast path
* Reduced stop-the-world GC work: the per-collect class-statics walk iterates a dense registration list instead of chasing the class registry's hash buckets (the list replaces entries in place when cppia reloads re-register a name, so replaced classes do not stay rooted)
* Type.resolveClass no longer crashes when called by a native host before any class has booted

* Updated mbedtls to 2.28.2
* Updated sqlite to 3.40.1
* Updated zlib to 1.2.13

* Fixed SSL socket non blocking handshake throwing an exception on 64bit Windows
* Fixed Windows 64bit architecture detection
* Fixed critial error handler returning the wrong callstack
* Fixed ARM64 library names on Mac
* Fixed generational GC when used with HXCPP_ALIGN_ALLOC
* Fixed heap corruption in the moving/compacting GC (HXCPP_GC_MOVING) with HXCPP_ALIGN_ALLOC: the alignment padding added to the destination position was not deducted from the remaining hole length, so the free-space count drifted and an object could be relocated past the end of a block, corrupting adjacent objects
* Fixed the moving GC not adjusting pointers held by large (non-block) objects after a compaction, leaving them dangling
* Fixed heap corruption sorting arrays of Bool or small integer element types (Array&lt;Bool&gt;.sort, cpp.UInt8/Int16/etc): the buffer was reinterpreted as an array of Dynamic and read/written out of bounds
* Fixed Array.resize with a negative size zeroing memory before the array buffer
* Fixed integer overflow in array growth size math: huge reserve/resize requests now throw a catchable exception instead of corrupting the heap, and pushing past ~2GB of buffer no longer hangs in an infinite loop
* Fixed integer overflow in Array.splice with huge lengths (now clamps like other targets) and rejected negative element counts in array blit
* Fixed haxe.io.Bytes.fill with a negative position writing before the buffer (GC heap corruption)
* Fixed out-of-bounds read looking up a missing field on an anonymous object with exactly 5 fields, and missed lookups when field-name hashes collide
* Fixed String.fromCharCode with negative codes corrupting global memory (now throws), and made its lazy lookup-table init thread-safe
* Fixed charAt/substr on non-ASCII byte strings in legacy (non-smart-strings) builds passing negative char codes
* Fixed UTF-16 surrogate-pair validation: a lone high surrogate no longer swallows the following character, produces corrupt code points, or scans past the end of the buffer when converting to UTF-8 (out-of-bounds read)
* Fixed haxe.zip.Uncompress/Compress execute() returning cumulative stream totals instead of per-call counts, which broke haxe.zip.Uncompress.run for any output larger than the buffer size (64K default) and corrupted all streaming loops after the first call
* Fixed zlib state leaks: Compress.run/Uncompress.run now release zlib's internal state on all paths including errors, and closing a Compress/Uncompress no longer leaks the stream structure
* Fixed Windows console writes larger than 4096 UTF-16 units being silently discarded while reporting success (output is now converted and written in chunks), and fixed the broken surrogate constants in the partial-write accounting
* Fixed a lost wakeup in sys.thread.Deque on Windows: with multiple blocked consumers, coalesced signals could leave a consumer asleep while items sat in the queue
* Fixed comparison of boxed Int64 values above 2^53 (was routed through double, making distinct values compare equal)
* Fixed null Dynamic operands crashing in -, *, ++ and -- (now treated as 0, consistent with +, / and %)
* Fixed Std.parseInt undefined behaviour passing non-ASCII chars to isspace, and made overflow behaviour platform-independent (decimal values saturate to the Int range instead of depending on the platform's long size)
* Fixed sys.io.Process.close followed by stdin.close closing a stale (possibly recycled by the OS) handle
* Fixed EReg.matchSub integer overflow with huge lengths, regex runtime errors (e.g. match limit) being silently treated as no-match, and undefined behaviour passing unvalidated UTF-16 subjects to PCRE2 with the no-check flag
* Fixed sys.thread.Semaphore on Linux throwing spurious exceptions when a signal interrupts acquire/tryAcquire (EINTR is now retried)
* Fixed sys.thread.Semaphore.tryAcquire on Apple platforms truncating fractional timeouts to whole seconds (0.5 became 0), and a dispatch semaphore leak per Semaphore object
* Fixed pthread structured being unaligned
* Fixed cppia crash on functions with empty bodies
* Fixed regression parsing integers which wrap around
* Fixed compilation with HXCPP_GC_DEBUG_LEVEL define
* Fixed compilation with MinGW
* Fixed enum parameters potentially returning null
* Fixed HXCPP_CLANG not being set when using clang
* Fixed pcre2 and mbedtls not being compiled with the c99 standard
* Fixed behaviour of indexOf and lastIndexOf on empty strings not aligning with other targets
* Fixed behaviour of directory reading function not aligning with other targets
* Fixed haxelib not being invoked with the current working directory
* Fixed String(unsigned int) appending a literal "d" (from a "%ud" format) for raw unsigned ints formatted via the runtime
* Fixed Std.string of NaN/Infinity to match other targets ("NaN", "Infinity", "-Infinity") instead of platform printf output ("-nan(ind)", "inf")
* Fixed Std.string(Float) losing precision: now emits the shortest representation that round-trips (e.g. 0.1+0.2 prints "0.30000000000000004"), so String<->parseFloat is lossless and matches other targets
* Normalized float exponent formatting to minimal digits ("1e-7" instead of "1e-07"), matching other targets
* Fixed out-of-bounds read when deleting a fixed field of an anonymous object (Reflect.deleteField)
* Fixed String.indexOf with a negative start index reading out of bounds and returning a bogus negative result (now clamps to 0, matching other targets)
* Fixed String.lastIndexOf with a negative start index returning -1 instead of matching at index 0 (now clamps to 0, matching other targets)
* Fixed Dynamic modulo (%) dereferencing a null operand without a guard (now coerces null to 0 like the other dynamic operators)

* Removed Haxe 3 support

4.3
------------------------------------------------------------

* Bug fixes
* Upgrade to 4.3 API
* Use PCRE v2

4.2
------------------------------------------------------------

* Update MIN_IOS_VERSION
* Bug fixes
* Upgrade to 4.2 API
* Fixed mysql_select_db failing for newer mariadb versions due to extra 0x00 byte after database
* Fixed mysql_close not sending COM_QUIT before closing socket

4.1.15
------------------------------------------------------------
* Added Arm64 suport on windows
* Fixed crash with zero-sized alloc and generational GC
* Fixed crash with generational GC when old objects come back to life
* Fixed compile error with @:fixed Anons and arrays (socket select)
* Fixed lastIndexOf
* Optimized some equality functions

4.1.1
------------------------------------------------------------
* Added functions for haxe 4.1 Api.
* Added HXCPP_DEBUG_LINK_AND_STRIP to preserve symbolic information when creating android release binaries.
* Added optional HXCPP_SIGNAL_THROW to convert memory errors to haxe exceptions without needing additional code
* Added string_hash_map_substr and __hxcpp_parse_substr_float/int to allow some substring processing without extra allocation

4.0.64
------------------------------------------------------------

* Upgrade buildserver to 4.01
* Better generational collection in high fragmentation case
* typeinfo include fix for MSVC
* Fix MySQL connections
* Fix bugs with HXCPP_GC_GENERATIONAL
* Add map.clear
* Better c++11 iOS support

4.0.19
------------------------------------------------------------

* Add Array.keyValueIterator
* General Utf16 string improvements
* Limit the amount of recursion in toString function
* Add float32 support to cppia
* Fix Gc race condition
* Throw exceptions according to the spec when casting
* Introduce hxcpp_smart_strings for unicode text

4.0.4
------------------------------------------------------------
* Compile Cppia against haxe 4.0 preview 4

4.0.2
------------------------------------------------------------
* Default Cppia to 64 bits on windows

4.0.1
------------------------------------------------------------
* More logic for determining the android NDK version
* Updated various opensource libraries (thanks robocoder)
* Updated version of zlib
* Updated version of sljit
* Updated version of pcre
* Updated version of sqlit3
* Updated version of mbedtls
* Some work on supporting utf16 strings (hx_smart_strings)
* Added process_kill
* Change root when calculating haxelib in build.xml files
* Fix cppia super calls across cpp boundary
* Add Array.resize
* Be consistent with mod in cppia
* Fix Sys.stderr
* Add 'embedName' file attribute to allow text to cpp conversion
* Updates for Msvc
* Updates for Xcode

3.4.188
------------------------------------------------------------
* Fix some threading crashes

3.4.185
------------------------------------------------------------
* Do not ship static libraries
* Use more lock-free structures in GC processing
* Added some documentation
* Added HXCPP_GC_SUMMARY option
* Added HXCPP_GC_GENERATIONAL option
* Added HXCPP_GC_DYNAMIC_SIZE option
* Some MSVC 2017 support
* Compile Cppia with JIT as an option by default

3.4.64
------------------------------------------------------------
* Fixed cppia native interface implementation
* Fixed debugger breakpoints
* More compatibility for inet_pton and inet_ntop
* Correct the order of thread housekeeping data

3.4.49
------------------------------------------------------------
* Fixed 2d-Arrays and unserialize

3.4.43
------------------------------------------------------------

* Added more options for code-size optimizations on android (thanks madrazo)
* Added version of stpcpy on android to allow building with platform > 21, and running on older devices
* Added some initial support for ipv6
* Experimental support for Cppia JIT
* Fixed issue with stale objects that use new pch files in cache
* Rethrowing exception now preserves stack correctly


3.4.2
------------------------------------------------------------

* Align float reads from memory for Arm architecture
* Removed some virtual functions not needed by newer versions of haxe
* Reworked the logic for compacting fragmented heaps with HXCPP_GC_MOVING
* Expose StackContext to allow inlining of allocation routine, and combine with Cppia context
* Fix some compare-with-dynamic issues
* Added WatchOs support
* Fixed for android NDK 13
* Fix Array closure equality
* Refactor the Cppia code
* Fix return codes for atomic decrease
* Fix some GC zone issues in the standard library
* Set minimum MacOS deployment target to 10.6
* Do not use typedefs for 'Int' and 'Bool' for newer api levels
* Added dll_link to create output dll
* Improved ObjC support
* Make Cppia order of operations of '+=' et al consistent with other targets
* Added NO_RECURSE flag to PCRE
* Fix bsd_signal undefines on android
* Add create/free abstract

3.3.49
------------------------------------------------------------
* Fix Dynamic != for haxe 3.2.1
* Fix Command line parsing on windows for triple quotes

3.3.45
------------------------------------------------------------
* Much better compile cache support
* Added tags to compiler flags to allow better targeting
* Added UCP support to regexp
* Added Array::fromData
* Added AtomicInt operations
* Added _hx_imod
* More improvements for tvos
* Fix blocking deque issue
* Improved native testing
* Added 'hxcpp run hxcpp cache ...' commands for managing cache
* Added cpp.Variant class for query of field values to avoid boxing
* Added more efficient version of finalizer
* Add non allocating version of __hxcpp_print
* More WinRT fixes
* Output 'HxcppConfig.h' with defines included for easier external integration
* Output list of output files if requested
* Add support functions for StdLib - alloc/free/sizeof
* Fix crash when marking stack names from GCRoots
* Add bitcode support for iOS
* Rename RegisterClass to avoid conflicts with windows
* Added 'VirtualArray' for arrays of unknown types
* Split Macros.tpl
* Added optional ShowParam to process_run
* Added inline functions for Int64 externs
* Add error check for allocating from a finalizer
* Fix null strings on Cffi Prime
* Use slow path if required for Win64 Tls
* Expand logic for detecting android toolchain from NDK name
* Remove the need for hxcpp binaries by compiling source directly into target
* Adjust the default verbosity level, and add HXCPP_VERBOSE/HXCPP_QUIET/HXCPP_SILENT
* Added some control options for copyFile directive
* Fix cppia decrement
* Add Array.removeRange, which does not require a return value
* Do not call setbuf(0) on stdin, since it messes with readLine
* Cppia now throws an error if loading fails
* Allocate EnumParam data inline to cut down on allocations
* Allow anonymous object data to be allocated inline to avoid allocations
* Add SSL library code
* Add NativeGen framework for interfaces
* Add macros to allow neater generated code
* Allow larger memory space with -D HXCPP_GC_BIG_BLOCKS
* Improve Array.join speed

3.2.205
------------------------------------------------------------
* Initial support for HXCPP_OPTIMIZE_FOR_SIZE
* Support HXCPP_DEBUG_LINK on more targets
* Support for cross compiling to windows from linux
* Added array removeAt
* Some telemety fixes (thanks Jeff)
* Check contents when comparing Dynamics with same pointer (Math.Nan!=Math.Nan)
* Numerous WinRT fixes (thanks madrazo)
* Fixed bug causing GC to crash marking constant strings (eg, resources)
* Updated default SDK for Tizen (thanks Joshua)
* Fixed command line args on linux (thanks Andy)

3.2.193
------------------------------------------------------------
* Some improvements for tvos
* Start on some GC defragging code
* Fix android thread access to GC structures
* Add socket socket_recv_from and socket_send_to
* Fixed memory leak in GC collection code
* Allow cross-compile to windows via MINGW
* Fix overflow error that meant GC would work with a too-small buffer in some cases

3.2.180
------------------------------------------------------------
* Initial support for tvos
* Change name of ObjectType to hxObjectType to avoid clashes with iOS
* Try to keep windows.h out of haxe-generated code
* Fix null access bug in array-of-array
* Create separate library for msvc 19

------------------------------------------------------------
* Try to get the pdb server working better for MSVS 2015
* So not export symbols on windows unless HXCPP_DLL_EXPORT is set (-D dll_export) - makes exe smaller
* Avoid dynamic-cast if possible when converting 2D arrays
* Some RPi fixes
* Some CFFI Prime fixes (thanks Joshua)
* Fix build tool for next version of neko
* Improve msvc cl.exe version checking for non-English environments
* Add more control over how much Gc memory is used
* Add faster(inline) thread local storage for Gc on windows.
* Add some Gc load balancing when marking large arrays with multiple threads
* Change the Gc memory layout to be a bit larger, but simpler.  This allows most of the allocation to be simplified and inlined.
* Explicitly scan registers for Gc references because the stack scanning was missing them sometimes
* Some additions to Undefine.h for windows
* When static linking using MSVC 2015, compile the libraries directly into the exe to avoid compatibility issues
* Move standard libraries into their own build.xml files
* Make it easier to change the generated output filename
* Allow targets from one build.xml file to be merged into another
* Some more work on HXCPP_COMPILE_CACHE
* Allow automatic grouping of obj files into librarys to avoid linking all the symbols in all obj files
* Add implicit conversion to referenced type from cpp.Reference
* Allow build.xml files to be imported relative to importing file
* Allow '-' in command-line defines
* Fix warnings from Hash class
* Fix setsockopt for Mac
* Support to MSVC2015
* Fix for Blackberry 10.3
* Fix debug break by linenumber
* Better objc integration (thanks Caue)
* Increase number of variables captured in closures to 20
* Initial support for telemetry (thanks Jeff)
* Align allocations for better emscripten support

------------------------------------------------------------
* Fix gc_lock error in remove_dir
* Some cppia bug fixes - enum and resources overrides
* More android atof fixes
* Improved haxelib seek logic

Haxe 3.2.0
------------------------------------------------------------

* Improve testing
* Allow dll_load path to be set programatically and simplified the dll search sequence.
* Improved cffi_prime, and added String class
* Fixed static linking of mysql5
* Moved static link code in general to cpp.link package, not hxcpp package
* URL decode now does not need to performe reallocs
* Ensure HXCPP_API_LEVEL is always defined
* Added __hxcpp_unload_all_libraries to cleanly unload dlls
* Added some utc date functions
* Better support for non-console apps in windows XP 64
* Increased use of HXCPP_DEBUG_LINK for gcc based targets
* Class 'hasField' is now more consistent with other functions/targets
* 'haxelib run hxcpp test.cppia' will run Cppia on the specified file
* Add fast-select option for sockets
* Allow code to run without HXCPP_VISIT_ALLOCS defined
* Fix debugger thread deadlocks
* Allow up to 27 dynamic arguments
* Fixes for Emscripten - byte align access and disable threads
* Allow emscripten to generate 'executables' (.js/.html) and add options for specifying memory
* Allow spaces in exe names again
* Make cpp::Struct compare via memcmp, and mark correctly
* Fix catch block in cppia
* Treat '-debug' as an alias for "-Ddebug"
* Expose ArrayBase for use with some generic or external code
* Clarify the role of 'buffer' in cffi

------------------------------------------------------------
* Only put a minimal run.n in source-control, and use this to boot hxcpp.n
* Added cpp.Struct and cpp.Reference classes, which are handy for extern classes
* Moved Class to hx namespace
* Simplified 'main' logic
* Allow new android compilers to work for old devices (thanks google)
* Correctly read hxcpp_api_level from Build.xml
* Verbose logging prints which file is being compiled
* Handle undefining the INT_ constants differently to allow std::string to still compile
* Remove entries form Options.txt that do not influence the cpp build
* Add optional destination= command-line option to allow copying the result to named file
* Static libraries will be prefixed with 'lib' now
* val_is_buffer always returns false on neko
* Add val_iter_field_vals, which is like val_iter_fields but consistent with neko
* Remove NekoApi binaries
* Add Cppia binaries
* Add Windows64 binaries
* Make compares between Dynamic and numeric types false, unless the Dynamic is actaully numeric

------------------------------------------------------------
* Even more optimizations for hashes
* Some more optimizations for small hashes
* Fix for google changing inlining in platform21 headers (atof, rand, srand)
* Re-tuned Hash for small objects too (improves Anon object perforamce)
* Reverted change that automatically threw 'BadCast'.  Now required HXCPP_STRICT_CASTS

------------------------------------------------------------
* Cached dynamic versions of small ints and 1-char-strings for speed
* Added support for weak hashes - needs latest haxe version
* Use internal hash structure for maps - now faster.  New version of haxe makes it faster still.
* Changed the way development versions are bootstrapped to avoid committing binaries
* Improved mingw support
* Dont append -debug to dll name
* Reorder xml includes to allow early parts to correctly influence older parts
* Fix busy wait in semaphore lock
* Fixed GC issue when constructing exrernal primitive objects
* Added armv7s and arm64 targets for ios
* Some fixes for neko cffi - wstring and warning for neko_init
* Fix file read (and copy) from thread

------------------------------------------------------------
* Compile fix for blackberry
* Pass on haxe_api_level
* Add -nocolor flag

------------------------------------------------------------
* Add support for prelinker
* Cygwin toolchain fix
* Add HXCPP_NO_COLOUR  and HXCPP_NO_M32
* Fix windows trace output
* Add initial support for GCWO compile
* Fix bug with losing GC references in Array.sort
* Fix bug with zombie marking
* Add support for optimised sort routines
* Add support for haxe.ds.Vector optimisation
* Add support for cpp.Pointer, cpp.NativeArray, cpp.NativeString

------------------------------------------------------------
* Add BlackBerry and Tizen binaries
* Fix issues when using names like ANDROID or IPHONE in an enum
* Added more info in verbose mode (setenv HXCPP_VERBOSE)
* Refactor build files to allow greater customisation
* Fix bug with 'lock' where some threads may not get released
* Add optimised arrays access
* Add optimised memory operations for arrays and haxe.io.Bytes
* Avoid blocking in gethostbyname
* Upgrade run tool output and layout
* Restore sys_time for windows

3.1.1
------------------------------------------------------------
* Fixed MSVC support for 64-bit targets (vc11, vc12)
* Initial work on cpp.Pointer (not fully functional)
* Fixed callstack when throwing from native function

3.1.0
------------------------------------------------------------

* VC 2013 support - used as default now
* Add winxp compatibility flags
* Allow cross-compiling from mac to linux
* Added NSString helper conversion
* Better auto-detection for android toolchain
* Allow foreign threads to easily attach and detach from GC system
* Weak references to closures keep object alive
* Added HXCPP_API_LEVEL define to allow for future compatibility
* Fixed clearing finalizers twice
* Int multiply and minus are performed with integers now
* Fix comparing +- infinities
* Use multiple threads in the mark phase of GC
* IOS now defaults cpp11 binary linkage
* Added HXCPP_VERBOSE environment var to enable extra output
* Fixed spin loop in pthread_cond_wait
* Added ability to link several .a files into a single .a file
* Removed dependence on STL runtime for supplied modules
* Renamed some directories to be more standard
* Moved some extra build files into obj directory
* Use sys.io.Process instead of Sys.command to avoid threading slowdown writing to console
* Add hxcpp.Builder to help with building multiple binaries
* Add android x86 support
* Drop pre-compiled support for everything excepth windows,mac,linux,ios and android
* Allow libraries and files to accumulated in the build.xml
* Supply pre-build lib files for static linking on supported platforms
* Support for static linking of all modules
* Support for hxcpp-debugger project
* Binaries have been removed from repo, and are built using a server
* Use build.n script to build all appropriate binaries
* Some initial support for mysql and sqlite databases
* Add free_abstract for safe releasing of data references
* Change process lauching to get better thread usage on mac
* Fix GC error in string resources
* Give obj files in libraries unique names

3.0.2
------------------------------------------------------------
* Fix Dynamic + Int logic
* Reverted linux compiler to older version
* Cast Array at call site if required
* Tweak Array.map return value

3.0.1
------------------------------------------------------------
* Added nekoapi for linux64
* Upgrade nekoapi to v2
* Added haxe vector support
* Added socket_set_fast_send
* Fixed android build
* Expanded native memory access methods
* Fix exception dump
* Added initial Emscriptm support
* Allow specification of ANDROID_HOST
* Inital work on auto-setup of win64
* Support call-site casting of Arrays


3.0.0
------------------------------------------------------------
* Support haxe3 syntax
* Added socket poll function
* Added some initial support for dll_import/dll_export
* Allow full path name when loading dynamic libraries
* Allow dynamic toString function
* Added initial support for Raspberry Pi
* Array sort now uses std::stable_sort
* Fixed Dynamic+null string output
* Fix splice size calculation
* Add object ids for use in maps
* Add map/filter functions to arrays
* GC will now collect more often when big arrays are used
* You can specify a number of args > 5 for cffi functions if you want
* Fix internal hash size variable
* Class static field list does not report super members now
* Fix casting of null to any object
* Do not read input twice in sys_getch
* Link in PCH generated obj data on msvs 2012
* Date is now consistent with UTC
* Hash 'remove' now returns correct value
* CPP native WeakRef now works, and has a 'set' function
* Fixed compile error when assigning to a base class
* Fixed compile error when using != and Dynamic
* Math/floor/ceil/trunc/min/max now pass unit tests
* More control over android sdk installation
* Regexp_match fix
* Fix val_callN CFFI

2.10.3
------------------------------------------------------------
* Added initial build support for WinRT
* Android toolchain improvements
* Minor compile fixes
* Other minor improvements

2.10.2
------------------------------------------------------------
* Fixes for BlackBerry 10 compatibility
* Fixes for iOS 6 compatibility
* CFFI improvements
* Minor Linux improvements
* Minor OS X improvements

2.10.1
------------------------------------------------------------
* Fix trace() output
* Clang options for OS X compiler
* Small fixes

2.10.0
------------------------------------------------------------
* GC upgrades - moving/defragging/releasing
* Built-in profiler
* Build-in debugger
* Fix mac ndll finding bug
* Add Int32 member functions
* Clang options for ios compiler
* Add a few pre-boxed constants
* Some general bug fixes

2.09.3
------------------------------------------------------------
* Fix Xml enum usage

2.09.2
------------------------------------------------------------
* Resolve library paths when launching Mac apps from Finder
* Compile fix for the BlackBerry toolchain
* Fix interface comparison
* Fix api_val_array_value for NekoApi
* Add workaround for optional Strings in interfaces 
* Tweak the timing og the GC run
* Remove setProperty conditional compiles
* String charCodeAt only returns positive values
* Fix modulo for negative numbers
* Remove extra space from array output
* Treat '.' and '_' as literals in urlEncode
* Dynamically generated, 0 param, enum instances match the static version


2.09
------------------------------------------------------------
* Improved precision in random implementations
* Added some experimental support for float32
* Added some experimental support for generic getProcAddress
* String::fromCharCode generates single-byte strings
* Fix method compares
* Plug memory leak in finalizers
* Fix debug link flags
* Separate get/SetField from get/setProperty
* Added Null<T> for optional parameters

2.08.3
------------------------------------------------------------
* Actually add blackberry toolchain

2.08.2
------------------------------------------------------------
* Add blackberry support
* Add armv7 options
* Support new xcode layout
* Fix const qualifiers on interface functions
* Fix webOS obj directory

2.08.1
------------------------------------------------------------
* Fix Math.random returning 1.0 sometimes
* Std.is( 2.0, Int ) is now true
* Make static library building more separated - refactor defines to control this 
* Do not use @files for linking on mac
* toString on Anon objects will now get called
* Fix fast memory access with --no-inline
* Android tool host now set to linux-x86
* Allow use of __compare as operator== overload
* Add toNativeInt
* Add weak references
* Implement some neko/cffi compatibility operations
* Fix mac deployment using environment variable
* Fix reentrant mutexes
* Do not explicitly specify version of g++
* Speedup some code by avoiding dynamic_cast if possible
* Some fixes to allow Android multi-threading in normal operation

2.08
------------------------------------------------------------
* Do not create a new class definition for each member function
* Allow 5 fast and up to 20 slow dynamic function arguments
* Support utf8 class
* Added "Fast Memory" API similar to flash
* Added support for webOS
* Fix uncompress buffers
* Added file to undefined pesky processor macros
* Setup default config in user area
* Auto-detect msvc and iphone version
* Force compilation for mac 10.5
* Some support for cygwin compilers
* Remove Boehm GC as an option
* Integrate properly now with Android ndk-r6
* Make Int32 pass haxe unit tests (shift/modulo)
* Fix bug in "join"
* Fix bug with marking the "this" pointer in closures
* Fix bug with returning NAN from parseFloat
* Fix linux link flags
* Fix bug where string of length 0 would be null
* Made String cca return value consistent
* Added control over @file syntax
* Removed need for nekoapi.ndll
* Allow for neko.so to end in ".0"

2.07
------------------------------------------------------------
* Added initial support for Mac64, Linux64, MinGW and GPH and refactored build tool.
* Return the count of traced objects
* Fix interface operator ==
* Initial work on msvc10 batch file
* Add bounds check on String.cca
* Build static libraries, if requrested
* Added exe stripping
* Added val_field_name, val_iter_fields
* Fixed nekoapi string length
* Fixed Sys.args

2.06.1
------------------------------------------------------------
* Close files if required in GC
* Added fix for File.write
* Fixed String UTF8 Encode
* Nekoapi is now a "ndll", not a "dso".
* Fix array compile issue on linux
* Fix stack setting on firced collect

2.06.0
------------------------------------------------------------
* Updates to match haxe 2.06 compiler features
* Numerous bug fixes
* Add additional context to GC collection process
* Swapped from wchar_t* to utf8 char*
* Added templated iterators
* Use strftime for Dates
* Fix socket select and "_s" members
* Seed Math.random
* Fixed dynamic integer compare
* Added __hxcpp_obj_id
* Added some Android support

2.05.1
------------------------------------------------------------
* Updated windows nekoapi.dll binary
* Added -m32 compile flags to force 32 bit

2.05.0
------------------------------------------------------------

* Default to IMMIX based internal garbage collection.
* Reorginised files - split big ones, and moved common ones out of "runtime".
* Put internal classes in "hx" namespace, or HX_ prefix for macros.
* Remove multiple-inheritance, and use delegation instead.
* Write "Options.txt" from compiler so dependency can be determined.
* Require -D HXCPP_MULTI_THREADED for multi-threaded classes - to avoid overhead if not required.
* Build thread code into executable for better control.
* Fix return values of parseINt/parseFloat.
* Added comprehensive list of reserved member names.
* Put if/else statements in blocks.
* Added assert, NULL, LITTLE_ENDIAN, BIG_ENDIAN as keywords.
* Added control over how fast-cffi routines are created by requiring cpp.rtti.FastIntergerLookup to be "implemented".
* Construct anonymous object fields in deterministic (as declared) order.
* Fix code generation for some complex inline cases.
* Added cpp.zip.Compress
* Change "Reflect" class to be more standard
* Use array of dynamics for StringBuf.
* Fix setting of attributes in XML nodes.

Build-tool:
* Allow multiple build threads (via setenv HXCPP_COMPILE_THREADS N) for faster building on multi-code boxes.
* Added FileGroup dependencies
* Added pre-compiled headers (windows only, at the moment since gcc seems buggy)


1.0.7
-----------------
Changelog starts.
