#include <hxcpp.h>
#include "ZLibCompress.hpp"

#include <memory>
#include <limits>

#define ZLIB_OBJ_CLOSED ::hx::Throw(HX_CSTRING("Compress closed"))

// Surface the zlib failure detail (code and msg) instead of a bare
// "ZLib Error" - msg is valid until deflateEnd/inflateEnd runs
static void zlibThrow(z_stream *stream, int code)
{
	String msg = HX_CSTRING("ZLib Error ") + String(code);
	if (stream && stream->msg)
		msg = msg + HX_CSTRING(" (") + String::create(stream->msg) + HX_CSTRING(")");
	hx::Throw(msg);
}

hx::zip::Compress hx::zip::Compress_obj::create(int level)
{
	auto handle = std::unique_ptr<z_stream>(new z_stream());
	auto error  = deflateInit(handle.get(), level);

	if (error != Z_OK)
	{
		zlibThrow(handle.get(), error);
	}

	return new hx::zip::zlib::ZLibCompress(handle.release());
}

Array<uint8_t> hx::zip::Compress_obj::run(cpp::marshal::View<uint8_t> src, int level)
{
	auto error  = 0;
	auto handle = std::unique_ptr<z_stream>(new z_stream());

	if (Z_OK != (error = deflateInit(handle.get(), level)))
	{
		zlibThrow(handle.get(), error);
	}

	// Release zlib's internal state on every exit, including throws
	struct Closer
	{
		z_stream *stream;
		~Closer() { deflateEnd(stream); }
	} closer = { handle.get() };

	auto bounds = deflateBound(handle.get(), src.length);
	if (bounds > std::numeric_limits<int32_t>::max()) {
		hx::Throw(HX_CSTRING("Size Error"));
	}

	auto output = Array<uint8_t>(bounds, bounds);
	auto dst    = cpp::marshal::View<uint8_t>(output->getBase(), bounds);

	// Keep the allocation starts provably in this conservatively scanned
	// frame - deflate runs in a GC free zone with raw pointers into the
	// movable buffers
	uint8_t * volatile pinSrc = src.ptr;
	uint8_t * volatile pinDst = dst.ptr;

	handle->next_in = src.ptr;
	handle->next_out = dst.ptr;
	handle->avail_in = src.length;
	handle->avail_out = dst.length;

	EnterGCFreeZone();
	error = deflate(handle.get(), Z_FINISH);
	ExitGCFreeZone();

	if (Z_STREAM_END != error) {
		zlibThrow(handle.get(), error);
	}

	// Shrink in place - slice() would allocate and copy the whole result
	output->__SetSize(static_cast<int>(handle->total_out));
	return output;
}

hx::zip::zlib::ZLibCompress::ZLibCompress(z_stream* inHandle) : handle(inHandle), flush(0)
{
	_hx_set_finalizer(this, [](Dynamic obj) { reinterpret_cast<ZLibCompress*>(obj.mPtr)->close(); });
}

hx::zip::Result hx::zip::zlib::ZLibCompress::execute(cpp::marshal::View<uint8_t> src, cpp::marshal::View<uint8_t> dst)
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
	auto error = deflate(handle, flush);
	ExitGCFreeZone();

	if (error < 0)
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

void hx::zip::zlib::ZLibCompress::setFlushMode(Flush mode)
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

int hx::zip::zlib::ZLibCompress::getBounds(const int length)
{
	if (handle == nullptr)
	{
		ZLIB_OBJ_CLOSED;
	}

	auto bounds = deflateBound(handle, length);
	if (bounds > std::numeric_limits<int32_t>::max())
	{
		hx::Throw(HX_CSTRING("Size Error"));
	}

	return static_cast<int>(bounds);
}

void hx::zip::zlib::ZLibCompress::close()
{
	if (nullptr == handle)
	{
		return;
	}

	deflateEnd(handle);

	delete handle;

	handle = nullptr;

	_hx_set_finalizer(this, nullptr);
}
