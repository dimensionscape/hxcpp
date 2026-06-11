#include <hxcpp.h>

#include <hx/Thread.h>
#include <time.h>
#include <hx/thread/ConditionVariable.hpp>
#include <hx/thread/RecursiveMutex.hpp>
#include <hx/thread/CountingSemaphore.hpp>
#include <atomic>

DECLARE_TLS_DATA(class hxThreadInfo, tlsCurrentThread);

// Thread number 0 is reserved for the main thread
static std::atomic_int g_nextThreadNumber(1);


// How to manage hxThreadInfo references for non haxe threads (main, extenal)?
// HXCPP_THREAD_INFO_PTHREAD - use pthread api
// HXCPP_THREAD_INFO_LOCAL - use thread_local storage
// HXCPP_THREAD_INFO_SINGLETON - use one structure for all threads. Not ideal.

#if __cplusplus > 199711L && !defined(__BORLANDC__)
   #define HXCPP_THREAD_INFO_LOCAL
#elif defined (HXCPP_PTHREADS)
   #define HXCPP_THREAD_INFO_PTHREAD
#else
   #define HXCPP_THREAD_INFO_SINGLETON
#endif


// --- Deque ----------------------------------------------------------

struct Deque : public Array_obj<Dynamic>
{
	Deque() : Array_obj<Dynamic>(0,0), head(0) { }

	static Deque *Create()
	{
		Deque *result = new Deque();
		result->mFinalizer = new hx::InternalFinalizer(result,clean);
		return result;
	}

	// Consumed prefix.  PopFront advances this instead of shift()ing,
	// which memmoved the entire remaining queue per pop - draining an
	// n-deep mailbox was O(n^2) bytes moved while holding the lock
	int head;

	inline bool hasItems() const { return length > head; }

	Dynamic popHead()
	{
		if (length <= head)
			return null();
		Dynamic *items = (Dynamic *)mBase;
		Dynamic result = items[head];
		items[head] = null();
		head++;
		if (head == length)
		{
			// Drained - recycle the buffer from the start
			length = 0;
			head = 0;
		}
		else if (head > 64 && head >= length - head)
		{
			// Compact once the consumed prefix dominates - O(1) amortized
			int remaining = length - head;
			::memmove(items, items + head, remaining * sizeof(Dynamic));
			::memset(items + remaining, 0, head * sizeof(Dynamic));
			length = remaining;
			head = 0;
		}
		return result;
	}

	void pushHead(Dynamic inValue)
	{
		if (head > 0)
		{
			Dynamic *items = (Dynamic *)mBase;
			head--;
			items[head] = inValue;
			HX_OBJ_WB_GET(this, inValue.mPtr);
		}
		else
			unshift(inValue);
	}
	static void clean(hx::Object *inObj)
	{
		Deque *d = dynamic_cast<Deque *>(inObj);
		if (d) d->Clean();
	}
	void Clean()
	{
		#ifdef HX_WINDOWS
		mMutex.Clean();
		#endif
		mSemaphore.Clean();
	}

   #ifdef HXCPP_VISIT_ALLOCS
  	void __Visit(hx::VisitContext *__inCtx)
	{
		Array_obj<Dynamic>::__Visit(__inCtx);
		mFinalizer->Visit(__inCtx);
	}
   #endif


	// Try-lock first: the uncontended case skips two fence-bearing
	// GC-free-zone transitions per message (the same fast path the
	// sys.thread primitives use)
	template<typename LOCKABLE>
	inline void lockFor(LOCKABLE &inMutex)
	{
		if (!inMutex.TryLock())
		{
			hx::EnterGCFreeZone();
			inMutex.Lock();
			hx::ExitGCFreeZone();
		}
	}

	#ifndef HX_THREAD_SEMAPHORE_LOCKABLE
	HxMutex     mMutex;
	void PushBack(Dynamic inValue)
	{
		lockFor(mMutex);
		push(inValue);
		mSemaphore.Set();
		mMutex.Unlock();
	}
	void PushFront(Dynamic inValue)
	{
		lockFor(mMutex);
		pushHead(inValue);
		mSemaphore.Set();
		mMutex.Unlock();
	}


	Dynamic PopFront(bool inBlock)
	{
		lockFor(mMutex);
		if (inBlock)
		{
			// Ok - wait for something on stack...
			while(!hasItems())
			{
				mSemaphore.Reset();
				mMutex.Unlock();
				hx::EnterGCFreeZone();
				mSemaphore.Wait();
				hx::ExitGCFreeZone();
				lockFor(mMutex);
			}
		}
		// Re-signal while items remain, like the posix branch below -
		// otherwise two pushes whose Set calls coalesce on the auto-reset
		// event leave a second blocked consumer asleep with the item queued
		Dynamic result = popHead();
		if (hasItems())
			mSemaphore.Set();
		else if (inBlock)
			mSemaphore.Reset();
		mMutex.Unlock();
		return result;
	}
	#else
	void PushBack(Dynamic inValue)
	{
		lockFor(mSemaphore.mMutex);
		push(inValue);
		mSemaphore.QSet();
		mSemaphore.mMutex.Unlock();
	}
	void PushFront(Dynamic inValue)
	{
		lockFor(mSemaphore.mMutex);
		pushHead(inValue);
		mSemaphore.QSet();
		mSemaphore.mMutex.Unlock();
	}


	Dynamic PopFront(bool inBlock)
	{
		lockFor(mSemaphore.mMutex);
		if (inBlock && !hasItems())
		{
			hx::EnterGCFreeZone();
			while(!hasItems())
				mSemaphore.QWait();
			hx::ExitGCFreeZone();
		}
		Dynamic result = popHead();
		if (hasItems())
			mSemaphore.QSet();
		mSemaphore.mMutex.Unlock();
		return result;
	}
	#endif

	hx::InternalFinalizer *mFinalizer;
	HxSemaphore mSemaphore;
};

Dynamic __hxcpp_deque_create()
{
	return Deque::Create();
}

void __hxcpp_deque_add(Dynamic q,Dynamic inVal)
{
	Deque *d = dynamic_cast<Deque *>(q.mPtr);
	if (!d)
		throw HX_INVALID_OBJECT;
	d->PushBack(inVal);
}

void __hxcpp_deque_push(Dynamic q,Dynamic inVal)
{
	Deque *d = dynamic_cast<Deque *>(q.mPtr);
	if (!d)
		throw HX_INVALID_OBJECT;
	d->PushFront(inVal);
}

Dynamic __hxcpp_deque_pop(Dynamic q,bool block)
{
	Deque *d = dynamic_cast<Deque *>(q.mPtr);
	if (!d)
		throw HX_INVALID_OBJECT;
	return d->PopFront(block);
}



// --- Thread ----------------------------------------------------------

class hxThreadInfo : public hx::Object
{
public:
	typedef
#if (HXCPP_API_LEVEL>=500)
		hx::Callable<void()>
#else
		Dynamic
#endif
		ThreadFuncType;

   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdThreadInfo };

	hxThreadInfo(ThreadFuncType inFunction, int inThreadNumber)
        : mFunction(inFunction), mThreadNumber(inThreadNumber), mTLS(0,0)
	{
		mSemaphore = new HxSemaphore;
		mDeque = Deque::Create();
      HX_OBJ_WB_NEW_MARKED_OBJECT(this);
	}
	hxThreadInfo()
	{
		mSemaphore = 0;
		mDeque = Deque::Create();
      HX_OBJ_WB_NEW_MARKED_OBJECT(this);
	}
    int GetThreadNumber() const
    {
        return mThreadNumber;
    }
	void CleanSemaphore()
	{
		delete mSemaphore;
		mSemaphore = 0;
	}
	void Send(Dynamic inMessage)
	{
		mDeque->PushBack(inMessage);
	}
	Dynamic ReadMessage(bool inBlocked)
	{
		return mDeque->PopFront(inBlocked);
	}
	String toString()
	{
		return String(GetThreadNumber());
	}
	void SetTLS(int inID,Dynamic inVal) {
      mTLS->__SetItem(inID,inVal);
   }
	Dynamic GetTLS(int inID) { return mTLS[inID]; }

	void __Mark(hx::MarkContext *__inCtx)
	{
		HX_MARK_MEMBER(mFunction);
		HX_MARK_MEMBER(mTLS);
		if (mDeque)
			HX_MARK_OBJECT(mDeque);
	}
   #ifdef HXCPP_VISIT_ALLOCS
  	void __Visit(hx::VisitContext *__inCtx)
	{
		HX_VISIT_MEMBER(mFunction);
		HX_VISIT_MEMBER(mTLS);
		if (mDeque)
			HX_VISIT_OBJECT(mDeque);
	}
   #endif


	Array<Dynamic> mTLS;
	HxSemaphore *mSemaphore;
	ThreadFuncType mFunction;
   int mThreadNumber;
	Deque   *mDeque;
};


THREAD_FUNC_TYPE hxThreadFunc( void *inInfo )
{
   // info[1] will the the "top of stack" - values under this
   //  (ie info[0] and other stack values) will be in the GC conservative range
	hxThreadInfo *info[2];
   info[0] = (hxThreadInfo *)inInfo;
   info[1] = 0;

	tlsCurrentThread = info[0];

	hx::SetTopOfStack((int *)&info[1], true);

	// Release the creation function
	info[0]->mSemaphore->Set();

    // Call the debugger function to annouce that a thread has been created
    //__hxcpp_dbg_threadCreatedOrTerminated(info[0]->GetThreadNumber(), true);

	if ( info[0]->mFunction.GetPtr() )
	{
		// An exception escaping a raw thread proc is std::terminate - the
		// whole process died with no diagnostic at all
		try
		{
			info[0]->mFunction();
		}
		catch(Dynamic e)
		{
			hx::strbuf buf;
			String err = e==null() ? HX_CSTRING("null") : e->toString();
			fprintf(stderr, "Uncaught exception in thread: %s\n", err.utf8_str(&buf));
		}
		catch(...)
		{
			fprintf(stderr, "Uncaught native exception in thread\n");
		}
		// Release the closure and its captures - the Thread handle may
		// outlive the run by a long time
		info[0]->mFunction = null();
	}

    // Call the debugger function to annouce that a thread has terminated
    //__hxcpp_dbg_threadCreatedOrTerminated(info[0]->GetThreadNumber(), false);

	hx::UnregisterCurrentThread();

	tlsCurrentThread = 0;

	THREAD_FUNC_RET
}


#if (HXCPP_API_LEVEL>=500)
Dynamic __hxcpp_thread_create(hx::Callable<void()> inStart)
#else
Dynamic __hxcpp_thread_create(Dynamic inStart)
#endif
{
    #ifdef EMSCRIPTEN
    return hx::Throw( HX_CSTRING("Threads are not supported on Emscripten") );
    #else
    int threadNumber = g_nextThreadNumber++;

	hxThreadInfo *info = new hxThreadInfo(inStart, threadNumber);

	hx::GCPrepareMultiThreaded();
	hx::EnterGCFreeZone();

    bool ok = HxCreateDetachedThread(hxThreadFunc, info);
    if (ok)
    {
       info->mSemaphore->Wait();
    }

    hx::ExitGCFreeZone();
    info->CleanSemaphore();

    if (!ok)
       throw Dynamic( HX_CSTRING("Could not create thread") );
    return info;
    #endif
}

#ifdef HXCPP_THREAD_INFO_PTHREAD
static pthread_key_t externThreadInfoKey;;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static void destroyThreadInfo(void *i)
{
   hx::Object **threadRoot = (hx::Object **)i;
   hx::GCRemoveRoot(threadRoot);
   delete threadRoot;
}
static void make_key()
{
   pthread_key_create(&externThreadInfoKey, destroyThreadInfo);
}
#elif defined(HXCPP_THREAD_INFO_LOCAL)
struct ThreadInfoHolder
{
   hx::Object **threadRoot;
   ThreadInfoHolder() : threadRoot(0) { }
   ~ThreadInfoHolder()
   {
      if (threadRoot)
      {
         hx::GCRemoveRoot(threadRoot);
         delete threadRoot;
      }
   }
   void set(hx::Object **info) { threadRoot = info; }
   hxThreadInfo *get() { return threadRoot ? (hxThreadInfo *)*threadRoot : nullptr; }
   
};
static thread_local ThreadInfoHolder threadHolder;
#else
static hx::Object **sMainThreadInfoRoot = 0;
#endif

static hxThreadInfo *GetCurrentInfo(bool createNew = true)
{
	hxThreadInfo *info = tlsCurrentThread;
	if (!info)
   {
      #ifdef HXCPP_THREAD_INFO_PTHREAD
      pthread_once(&key_once, make_key);
      hxThreadInfo **pp = (hxThreadInfo **)pthread_getspecific(externThreadInfoKey);
      if (pp)
         info = *pp;
      #elif defined(HXCPP_THREAD_INFO_LOCAL)
      info = threadHolder.get();
      #else
      if (sMainThreadInfoRoot)
      info = (hxThreadInfo *)*sMainThreadInfoRoot;
      #endif
   }

	if (!info && createNew)
	{
      // New, non-haxe thread - might be the first thread, or might be a new
      //  foreign thread.
		info = new hxThreadInfo(null(), 0);
      // The creation-handshake semaphore is only used by thread_create;
      // foreign-thread infos leaked it (a kernel event handle on Windows)
      // every time a native thread first touched a Haxe thread API
      info->CleanSemaphore();
      hx::Object **threadRoot = new hx::Object *;
      *threadRoot = info; 
		hx::GCAddRoot(threadRoot);
      #ifdef HXCPP_THREAD_INFO_PTHREAD
      pthread_setspecific(externThreadInfoKey, threadRoot);
      #elif defined(HXCPP_THREAD_INFO_LOCAL)
      threadHolder.set(threadRoot);
      #else
      sMainThreadInfoRoot = threadRoot;
      #endif
	}
	return info;
}

Dynamic __hxcpp_thread_current()
{
	return GetCurrentInfo();
}

void __hxcpp_thread_send(Dynamic inThread, Dynamic inMessage)
{
	hxThreadInfo *info = dynamic_cast<hxThreadInfo *>(inThread.mPtr);
	if (!info)
		throw HX_INVALID_OBJECT;
	info->Send(inMessage);
}

Dynamic __hxcpp_thread_read_message(bool inBlocked)
{
	hxThreadInfo *info = GetCurrentInfo();
	return info->ReadMessage(inBlocked);
}

bool __hxcpp_is_current_thread(hx::Object *inThread)
{
   hxThreadInfo *info = tlsCurrentThread;
   return info==inThread;
}

// --- TLS ------------------------------------------------------------

Dynamic __hxcpp_tls_get(int inID)
{
	return GetCurrentInfo()->GetTLS(inID);
}

void __hxcpp_tls_set(int inID,Dynamic inVal)
{
	GetCurrentInfo()->SetTLS(inID,inVal);
}



// --- Mutex ------------------------------------------------------------

Dynamic __hxcpp_mutex_create()
{
	return new hx::thread::RecursiveMutex_obj();
}
void __hxcpp_mutex_acquire(Dynamic inMutex)
{
	auto mutex = inMutex.Cast<hx::thread::RecursiveMutex>();

	mutex->acquire();
}
bool __hxcpp_mutex_try(Dynamic inMutex)
{
	auto mutex = inMutex.Cast<hx::thread::RecursiveMutex>();

	return mutex->tryAcquire();
}
void __hxcpp_mutex_release(Dynamic inMutex)
{
	auto mutex = inMutex.Cast<hx::thread::RecursiveMutex>();

	mutex->release();
}

// --- Semaphore ------------------------------------------------------------

Dynamic __hxcpp_semaphore_create(int value) {
	return new hx::thread::CountingSemaphore_obj(value);
}
void __hxcpp_semaphore_acquire(Dynamic inSemaphore) {
	auto semaphore = inSemaphore.Cast<hx::thread::CountingSemaphore>();

	semaphore->acquire();
}
bool __hxcpp_semaphore_try_acquire(Dynamic inSemaphore, double timeout) {
	auto semaphore = inSemaphore.Cast<hx::thread::CountingSemaphore>();

	return semaphore->tryAcquire(timeout);
}
void __hxcpp_semaphore_release(Dynamic inSemaphore) {
	auto semaphore = inSemaphore.Cast<hx::thread::CountingSemaphore>();

	semaphore->release();
}

// --- Condition ------------------------------------------------------------

Dynamic __hxcpp_condition_create(void)
{
	return new hx::thread::ConditionVariable_obj();
}
void __hxcpp_condition_acquire(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();
  
	condition->acquire();
}
bool __hxcpp_condition_try_acquire(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	return condition->tryAcquire();
}
void __hxcpp_condition_release(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	condition->release();
}
void __hxcpp_condition_wait(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	condition->wait();
}
bool __hxcpp_condition_timed_wait(Dynamic inCond, double timeout)
{
	return hx::Throw(HX_CSTRING("Not Implemented"));
}
void __hxcpp_condition_signal(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	condition->signal();
}
void __hxcpp_condition_broadcast(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	condition->broadcast();
}

// --- Lock ------------------------------------------------------------

class hxLock : public hx::Object
{
public:

	hxLock()
	{
		// Explicit rather than relying on GC allocations being zeroed
		mAvailable = 0;
		mFinalizer = new hx::InternalFinalizer(this);
		mFinalizer->mFinalizer = clean;
	}

   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdLock };

   #ifdef HXCPP_VISIT_ALLOCS
	void __Visit(hx::VisitContext *__inCtx) { mFinalizer->Visit(__inCtx); }
   #endif

	hx::InternalFinalizer *mFinalizer;

	#if defined(HX_WINDOWS)
	double Now()
	{
		// Monotonic and 64-bit - clock() is a 32-bit millisecond count on
		// MSVC, which wraps negative after ~24.8 days of process uptime and
		// breaks every timed wait from then on
		return (double)GetTickCount64()*0.001;
	}
	#elif defined(__SNC__)
	double Now()
	{
		return (double)clock()/CLOCKS_PER_SEC;
	}
	#else
	double Now()
	{
		struct timeval tv;
		gettimeofday(&tv,0);
		return tv.tv_sec + tv.tv_usec*0.000001;
	}
	#endif

	static void clean(hx::Object *inObj)
	{
		hxLock *l = dynamic_cast<hxLock *>(inObj);
		if (l)
		{
			l->mNotEmpty.Clean();
			l->mAvailableLock.Clean();
		}
	}
	bool Wait(double inTimeout)
	{
		double stop = 0;
		if (inTimeout>=0)
			stop = Now() + inTimeout;
		while(1)
		{
			mAvailableLock.Lock();
			if (mAvailable)
			{
				--mAvailable;
		      if (mAvailable>0)
               mNotEmpty.Set();
				mAvailableLock.Unlock();
				return true;
			}
			mAvailableLock.Unlock();
			double wait = 0;
			if (inTimeout>=0)
			{
				wait = stop-Now();
				if (wait<=0)
					return false;
			}

			hx::EnterGCFreeZone();
			if (inTimeout<0)
				mNotEmpty.Wait( );
			else
				mNotEmpty.WaitSeconds(wait);
			hx::ExitGCFreeZone();
		}
	}
	void Release()
	{
		AutoLock lock(mAvailableLock);
		mAvailable++;
		mNotEmpty.Set();
	}


	HxSemaphore mNotEmpty;
   HxMutex     mAvailableLock;
	int         mAvailable;
};



Dynamic __hxcpp_lock_create()
{
	return new hxLock;
}
bool __hxcpp_lock_wait(Dynamic inlock,double inTime)
{
	hxLock *lock = dynamic_cast<hxLock *>(inlock.mPtr);
	if (!lock)
		throw HX_INVALID_OBJECT;
	return lock->Wait(inTime);
}
void __hxcpp_lock_release(Dynamic inlock)
{
	hxLock *lock = dynamic_cast<hxLock *>(inlock.mPtr);
	if (!lock)
		throw HX_INVALID_OBJECT;
	lock->Release();
}


int __hxcpp_GetCurrentThreadNumber()
{
    // Can't allow GetCurrentInfo() to create the main thread's info
    // because that can cause a call loop.
    hxThreadInfo *threadInfo = GetCurrentInfo(false);
    if (!threadInfo) {
        return 0;
    }
    return threadInfo->GetThreadNumber();
}

// --- Atomic ---

bool _hx_atomic_exchange_if(::cpp::Pointer<cpp::AtomicInt> inPtr, int test, int  newVal )
{
   return _hx_atomic_compare_exchange(inPtr, test, newVal) == test;
}

int _hx_atomic_inc(::cpp::Pointer<cpp::AtomicInt> inPtr )
{
   return _hx_atomic_add(inPtr, 1);
}

int _hx_atomic_dec(::cpp::Pointer<cpp::AtomicInt> inPtr )
{
   return _hx_atomic_sub(inPtr, 1);
}


