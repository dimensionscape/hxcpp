#include <hxcpp.h>
#include "ZLibUncompress.hpp"

#include <memory>
#include <vector>
#include <limits>

#define ZLIB_OBJ_CLOSED ::hx::Throw(HX_CSTRING("Uncompress closed"))

// Surface the zlib failure detail (code and msg) instead of a bare
// "ZLib Error" - msg is valid until deflateEnd/inflateEnd runs
static void zlibThrow(z_stream *stream, int code)
{
	String msg = HX_CSTRING("ZLib Error ") + String(code);
	if (stream && stream->msg)
		msg = msg + HX_CSTRING(" (") + String::create(stream->msg) + HX_CSTRING(")");
	hx::Throw(msg);
}

hx::zip::Uncompress hx::zip::Uncompress_obj::create(int windowSize)
{
	auto handle = std::unique_ptr<z_stream>(new z_stream());
	auto error  = inflateInit2(handle.get(), windowSize);

	if (error != Z_OK)
	{
		zlibThrow(handle.get(), error);
	}

	return new hx::zip::zlib::ZLibUncompress(handle.release());
}

Array<uint8_t> hx::zip::Uncompress_obj::run(cpp::marshal::View<uint8_t> src, int bufferSize)
{
	auto handle = std::unique_ptr<z_stream>(new z_stream());
	auto error  = inflateInit2(handle.get(), 15);

	if (error != Z_OK)
	{
		zlibThrow(handle.get(), error);
	}

	// Release zlib's internal state on every exit, including throws
	struct Closer
	{
		z_stream *stream;
		~Closer() { inflateEnd(stream); }
	} closer = { handle.get() };

	// Pin the source data for the free-zone inflate calls (the per-chunk
	// slices below are interior pointers, which do not pin a moving GC)
	uint8_t * volatile pinSrc = src.ptr;

	// Accumulate in non-GC memory with amortized growth - appending each
	// chunk to the Array reallocated and copied the whole accumulated
	// output every iteration (quadratic), and inflate can run against
	// this buffer inside the free zone without GC concerns
	auto accumulated = std::vector<uint8_t>();
	size_t used      = 0;
	auto srcCursor   = 0;

	while (Z_STREAM_END != error)
	{
		auto srcView = src.slice(srcCursor);

		accumulated.resize(used + bufferSize);

		handle->next_in   = srcView.ptr;
		handle->next_out  = accumulated.data() + used;
		handle->avail_in  = srcView.length;
		handle->avail_out = bufferSize;

		EnterGCFreeZone();
		error = inflate(handle.get(), Z_SYNC_FLUSH);
		ExitGCFreeZone();

		// Z_NEED_DICT (a stream built with a preset dictionary) repeats
		// forever without consuming input - it must be an error here or
		// this loop never terminates
		if (error < 0 || error == Z_NEED_DICT)
		{
			zlibThrow(handle.get(), error);
		}

		used += bufferSize - handle->avail_out;

		srcCursor += srcView.length - handle->avail_in;
	}

	if (used > (size_t)std::numeric_limits<int32_t>::max())
	{
		hx::Throw(HX_CSTRING("Size Error"));
	}

	auto output = Array<uint8_t>(0, 0);
	output->memcpy(0, accumulated.data(), (int)used);
	return output;
}

hx::zip::zlib::ZLibUncompress::ZLibUncompress(z_stream* inHandle) : handle(inHandle), flush(0)
{
	_hx_set_finalizer(this, [](Dynamic obj) { reinterpret_cast<ZLibUncompress*>(obj.mPtr)->close(); });
}

hx::zip::Result hx::zip::zlib::ZLibUncompress::execute(cpp::marshal::View<uint8_t> src, cpp::marshal::View<uint8_t> dst)
{
	if (handle == nullptr)
	{
		ZLIB_OBJ_CLOSED;
	}

	handle->next_in   = src.ptr;
	handle->next_out  = dst.ptr;
	handle->avail_in  = src.length;
	handle->avail_out = dst.length;

	EnterGCFreeZone();
	auto error = inflate(handle, flush);
	ExitGCFreeZone();

	// Z_NEED_DICT repeats forever without consuming input - surfacing it
	// as an error stops the haxe.zip streaming loop from spinning
	if (error < 0 || error == Z_NEED_DICT)
	{
		zlibThrow(handle, error);
	}

	// Per-call counts - total_in/total_out are cumulative across the whole
	// stream, which breaks the haxe.zip streaming loops on the second call
	return
		Result(
			error == Z_STREAM_END,
			static_cast<int>(src.length - handle->avail_in),
			static_cast<int>(dst.length - handle->avail_out));
}

void hx::zip::zlib::ZLibUncompress::setFlushMode(Flush mode)
{
	if (handle == nullptr)
	{
		ZLIB_OBJ_CLOSED;
	}

	switch (mode)
	{
	case Flush::None:
		flush = Z_NO_FLUSH;
		break;

	case Flush::Sync:
		flush = Z_SYNC_FLUSH;
		break;

	case Flush::Full:
		flush = Z_FULL_FLUSH;
		break;

	case Flush::Finish:
		flush = Z_FINISH;
		break;

	case Flush::Block:
		flush = Z_BLOCK;
		break;
	}
}

void hx::zip::zlib::ZLibUncompress::close()
{
	if (nullptr == handle)
	{
		return;
	}

	inflateEnd(handle);

	delete handle;

	handle = nullptr;

	_hx_set_finalizer(this, nullptr);
}
