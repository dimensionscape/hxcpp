#include <hxcpp.h>
#include "hx/thread/RecursiveMutex.hpp"
#include <mutex>

struct hx::thread::RecursiveMutex_obj::Impl
{
	std::recursive_mutex mutex;
};

hx::thread::RecursiveMutex_obj::RecursiveMutex_obj() : impl(new Impl())
{
	GCSetFinalizer(this, [](hx::Object* obj)
	{
		auto mutex = reinterpret_cast<RecursiveMutex_obj*>(obj);

		delete mutex->impl;
	});
}

void hx::thread::RecursiveMutex_obj::acquire()
{
	// Uncontended fast path - no need for the GC free zone (stack capture
	// plus collector handshake) unless we are actually going to block
	// The free zone below was this thread's handshake with the collector. A
	// thread that only ever takes the lock uncontended, allocating nothing,
	// otherwise never reaches a safe point, and a collection waits for it
	// for ever.
	if (hx::gPauseForCollect)
		__hxcpp_gc_safe_point();
	if (impl->mutex.try_lock())
		return;

	hx::AutoGCFreeZone zone;

	impl->mutex.lock();
}

void hx::thread::RecursiveMutex_obj::release()
{
	impl->mutex.unlock();
}

bool hx::thread::RecursiveMutex_obj::tryAcquire()
{
	return impl->mutex.try_lock();
}
