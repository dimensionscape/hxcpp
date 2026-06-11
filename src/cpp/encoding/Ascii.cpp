#include <hxcpp.h>

using namespace cpp::marshal;

bool cpp::encoding::Ascii::isEncoded(const String& string)
{
	if (null() == string)
	{
		hx::NullReference("String", false);
	}

	return string.isAsciiEncoded();
}

int64_t cpp::encoding::Ascii::encode(const String& string, View<uint8_t> buffer)
{
	if (null() == string)
	{
		hx::NullReference("String", false);
	}

	if (string.isUTF16Encoded())
	{
		hx::Throw(HX_CSTRING("String cannot be encoded to ASCII"));
	}

	auto src = cpp::marshal::View<char>(string.raw_ptr(), string.length).reinterpret<uint8_t>();

	if (src.tryCopyTo(buffer))
	{
		return src.length;
	}
	else
	{
		return hx::Throw(HX_CSTRING("Buffer too small"));
	}
}

String cpp::encoding::Ascii::decode(View<uint8_t> view)
{
	// Consistent with the Utf8/Utf16 decoders - empty input is ""
	if (view.isEmpty())
	{
		return String::emptyString;
	}

	// The view length is authoritative: scanning for a NUL silently
	// truncated binary-ish payloads ("a\0b" decoded as "a"), and Haxe
	// strings legally contain NULs
	auto bytes = view.length;

	auto backing = hx::NewGCPrivate(0, bytes + sizeof(char));

	std::memcpy(backing, view.ptr.ptr, bytes);
	// NewGCPrivate does not zero - the terminator must be written
	static_cast<char*>(backing)[bytes] = 0;

	return String(static_cast<const char*>(backing), (int)bytes);
}
