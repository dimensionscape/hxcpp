#include <hxcpp.h>
#include <semaphore.h>
#include <sys/time.h>
#include <memory>
#include <hx/thread/CountingSemaphore.hpp>

struct hx::thread::CountingSemaphore_obj::Impl
{
	sem_t semaphore;

	static void finalise(hx::Object* obj)
	{
		auto impl = std::unique_ptr<Impl>(reinterpret_cast<hx::thread::CountingSemaphore_obj*>(obj)->impl);

		sem_destroy(&impl->semaphore);
	}
};

hx::thread::CountingSemaphore_obj::CountingSemaphore_obj(int value) : impl(new Impl())
{
	if (0 != (sem_init(&impl->semaphore, false, value)))
	{
		hx::Throw(HX_CSTRING("Failed to create semaphore"));
	}

	hx::GCSetFinalizer(this, Impl::finalise);
}

void hx::thread::CountingSemaphore_obj::acquire()
{
	hx::EnterGCFreeZone();

	// sem_wait is never restarted after a caught signal, regardless of
	// SA_RESTART - retry on EINTR rather than throwing spuriously
	int r;
	do
	{
		r = sem_wait(&impl->semaphore);
	}
	while (0 != r && EINTR == errno);

	if (0 != r)
	{
		hx::ExitGCFreeZone();
		hx::Throw(HX_CSTRING("Failed to wait on semaphore"));
	}

	hx::ExitGCFreeZone();
}

void hx::thread::CountingSemaphore_obj::release()
{
	if (0 != sem_post(&impl->semaphore))
	{
		hx::Throw(HX_CSTRING("Failed to release semaphore"));
	}
}

bool hx::thread::CountingSemaphore_obj::tryAcquire(Null<double> timeout)
{
	if (0 == timeout.Default(0))
	{
		return 0 == sem_trywait(&impl->semaphore);
	}
	else
	{
		hx::EnterGCFreeZone();

		struct timeval tv;
		struct timespec t;
		auto delta = timeout.value;
		int idelta = (int)delta, idelta2;
		delta -= idelta;
		delta *= 1.0e9;
		gettimeofday(&tv, NULL);
		delta += tv.tv_usec * 1000.0;
		idelta2 = (int)(delta / 1e9);
		delta -= idelta2 * 1e9;
		t.tv_sec = tv.tv_sec + idelta + idelta2;
		t.tv_nsec = (long)delta;

		// Retry on EINTR - the absolute deadline keeps the timeout intact
		int r;
		do
		{
			r = sem_timedwait(&impl->semaphore, &t);
		}
		while (0 != r && EINTR == errno);

		if (0 == r)
		{
			hx::ExitGCFreeZone();
			return true;
		}
		else
		{
			hx::ExitGCFreeZone();
			if (errno == ETIMEDOUT)
			{
				return false;
			}
			else
			{
				return hx::Throw(HX_CSTRING("Failed to wait for semaphore"));
			}
		}
	}
}