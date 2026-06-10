#include <hxcpp.h>
#include <dispatch/dispatch.h>
#include <hx/thread/CountingSemaphore.hpp>

struct hx::thread::CountingSemaphore_obj::Impl
{
	dispatch_semaphore_t semaphore;

	static void finalise(hx::Object* obj)
	{
		auto impl = reinterpret_cast<hx::thread::CountingSemaphore_obj*>(obj)->impl;

		// dispatch objects are reference counted - without the release the
		// kernel object leaks
		dispatch_release(impl->semaphore);

		delete impl;
	}
};

hx::thread::CountingSemaphore_obj::CountingSemaphore_obj(int value) : impl(new Impl())
{
	impl->semaphore = dispatch_semaphore_create(value);

	hx::GCSetFinalizer(this, Impl::finalise);
}

void hx::thread::CountingSemaphore_obj::acquire()
{
	hx::EnterGCFreeZone();

	if (0 != dispatch_semaphore_wait(impl->semaphore, DISPATCH_TIME_FOREVER))
	{
		hx::ExitGCFreeZone();
		hx::Throw(HX_CSTRING("Failed to wait for semaphore"));
	}

	hx::ExitGCFreeZone();
}

void hx::thread::CountingSemaphore_obj::release()
{
	dispatch_semaphore_signal(impl->semaphore);
}

bool hx::thread::CountingSemaphore_obj::tryAcquire(Null<double> timeout)
{
	hx::AutoGCFreeZone zone;

	// Convert to nanoseconds before truncating - casting the seconds value
	// first discards the fractional part, so tryAcquire(0.5) returned
	// immediately instead of waiting 500ms
	return
		(0 == dispatch_semaphore_wait(
			impl->semaphore,
			dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeout.Default(0) * 1e9))));
}