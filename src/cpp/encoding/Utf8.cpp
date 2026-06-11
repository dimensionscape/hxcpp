#include <hxcpp.h>
#include <array>

using namespace cpp::marshal;

namespace
{
    bool isAsciiBuffer(const View<uint8_t>& buffer)
    {
        // A UTF-8 buffer is ASCII iff no byte has the high bit set - the
        // old per-codepoint decode here tripled the work of every decode
        // for the common all-ASCII case
        for (int64_t i = 0; i < buffer.length; i++)
        {
            if (buffer.ptr.ptr[i] & 0x80)
            {
                return false;
            }
        }

        return true;
    }
}

int cpp::encoding::Utf8::getByteCount(const null&)
{
    hx::NullReference("String", false);
    return 0;
}

int cpp::encoding::Utf8::getByteCount(const char32_t& codepoint)
{
    if (codepoint <= 0x7F)
    {
        return 1;
    }
    else if (codepoint <= 0x7FF)
    {
        return 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        return 3;
    }
    else
    {
        return 4;
    }
}

int64_t cpp::encoding::Utf8::getByteCount(const String& string)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

    if (string.isAsciiEncoded())
    {
        return string.length;
    }

#if defined(HX_SMART_STRINGS)
    auto source = View<char16_t>(string.raw_wptr(), string.length).reinterpret<uint8_t>();
    auto length = source.length;
    auto bytes  = int64_t{ 0 };
    auto i      = int64_t{ 0 };

    while (i < source.length)
    {
        auto slice = source.slice(i);
        auto p     = Utf16::codepoint(slice);

        i     += Utf16::getByteCount(p);
        bytes += getByteCount(p);
    }

    return bytes;
#else
    return hx::Throw(HX_CSTRING("Unexpected encoding error"));
#endif
}

int cpp::encoding::Utf8::getCharCount(const null&)
{
    hx::NullReference("String", false);
    return 0;
}

int cpp::encoding::Utf8::getCharCount(const char32_t& codepoint)
{
    return getByteCount(codepoint) / sizeof(char);
}

int64_t cpp::encoding::Utf8::getCharCount(const String& string)
{
    return getByteCount(string) / sizeof(char);
}

int cpp::encoding::Utf8::encode(const null&, const cpp::marshal::View<uint8_t>& buffer)
{
    hx::NullReference("String", false);
    return 0;
}

int64_t cpp::encoding::Utf8::encode(const String& string, const cpp::marshal::View<uint8_t>& buffer)
{
    if (null() == string)
    {
        hx::NullReference("String", false);
    }

    if (0 == string.length)
    {
        return 0;
    }

    if (buffer.isEmpty())
    {
        return hx::Throw(HX_CSTRING("Buffer too small"));
    }

    if (string.isAsciiEncoded())
    {
        auto src = cpp::marshal::View<uint8_t>(reinterpret_cast<uint8_t*>(const_cast<char*>(string.raw_ptr())), string.length);

        if (src.tryCopyTo(buffer))
        {
            return src.length;
        }
        else
        {
            return hx::Throw(HX_CSTRING("Buffer too small"));
        }
    }

#if defined(HX_SMART_STRINGS)
    if (getByteCount(string) > buffer.length)
    {
        hx::Throw(HX_CSTRING("Buffer too small"));
    }

    auto initialPtr = buffer.ptr.ptr;
    auto source     = View<char16_t>(string.raw_wptr(), string.length).reinterpret<uint8_t>();
    auto i          = int64_t{ 0 };
    auto k          = int64_t{ 0 };

    while (i < source.length)
    {
        auto p = Utf16::codepoint(source.slice(i));

        i += Utf16::getByteCount(p);
        k += encode(p, buffer.slice(k));
    }

    return k;
#else
    return hx::Throw(HX_CSTRING("Unexpected encoding error"));
#endif
}

int cpp::encoding::Utf8::encode(const char32_t& codepoint, const cpp::marshal::View<uint8_t>& buffer)
{
    if (codepoint <= 0x7F)
    {
        buffer[0] = static_cast<uint8_t>(codepoint);

        return 1;
    }
    else if (codepoint <= 0x7FF)
    {
        auto data = std::array<uint8_t, 2>
        { {
            static_cast<uint8_t>(0xC0 | (codepoint >> 6)),
            static_cast<uint8_t>(0x80 | (codepoint & 63))
        } };
        auto src = View<uint8_t>(data.data(), data.size());
        
        src.copyTo(buffer);

        return data.size();
    }
    else if (codepoint <= 0xFFFF)
    {
        auto data = std::array<uint8_t, 3>
        { {
            static_cast<uint8_t>(0xE0 | (codepoint >> 12)),
            static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 63)),
            static_cast<uint8_t>(0x80 | (codepoint & 63))
        } };

        auto src = View<uint8_t>(data.data(), data.size());

        src.copyTo(buffer);

        return data.size();
    }
    else
    {
        auto data = std::array<uint8_t, 4>
        { {
            static_cast<uint8_t>(0xF0 | (codepoint >> 18)),
            static_cast<uint8_t>(0x80 | ((codepoint >> 12) & 63)),
            static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 63)),
            static_cast<uint8_t>(0x80 | (codepoint & 63))
        } };

        auto src = View<uint8_t>(data.data(), data.size());

        src.copyTo(buffer);

        return data.size();
    }
}

String cpp::encoding::Utf8::decode(const cpp::marshal::View<uint8_t>& buffer)
{
    if (buffer.isEmpty())
    {
        return String::emptyString;
    }

    if (isAsciiBuffer(buffer))
    {
        return Ascii::decode(buffer);
    }

#if defined(HX_SMART_STRINGS)
    auto chars = int64_t{ 0 };
    auto i     = int64_t{ 0 };

    while (i < buffer.length)
    {
        auto p = codepoint(buffer.slice(i));

        i     += getByteCount(p);
        chars += Utf16::getCharCount(p);
    }

    auto backing = View<char16_t>(::String::allocChar16Ptr(chars), chars);
    auto output  = backing.reinterpret<uint8_t>();
    auto k       = int64_t{ 0 };

    i = 0;
    while (i < buffer.length)
    {
        auto p = codepoint(buffer.slice(i));

        i += getByteCount(p);
        k += Utf16::encode(p, output.slice(k));
    }
        
    return String(backing.ptr.ptr, chars);
#else
    // +1 for the terminator - the allocation is not zeroed and hxcpp
    // strings are assumed NUL-terminated by native consumers
    auto backing = View<char>(hx::InternalNew(buffer.length + 1, false), buffer.length + 1);

    std::memcpy(backing.ptr.ptr, buffer.ptr.ptr, buffer.length);
    backing.ptr.ptr[buffer.length] = 0;

    return String(backing.ptr.ptr, static_cast<int>(buffer.length));
#endif
}

char32_t cpp::encoding::Utf8::codepoint(const cpp::marshal::View<uint8_t>& buffer)
{
    // Strict decoding.  Unvalidated continuation bytes silently produced
    // mojibake from malformed input (and consumed a valid following
    // character), and accepting overlong forms desynchronized the decode
    // loops, which advance by the re-encoded length of the value
    auto b0 = static_cast<char32_t>(buffer[0]);

    if ((b0 & 0x80) == 0)
    {
        return b0;
    }
    else if ((b0 & 0xE0) == 0xC0)
    {
        auto b1 = static_cast<char32_t>(buffer.slice(1)[0]);
        if ((b1 & 0xC0) != 0x80)
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        auto p = (static_cast<char32_t>(b0 & 0x1F) << 6) | (b1 & 0x3F);
        if (p < 0x80) // overlong
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        return p;
    }
    else if ((b0 & 0xF0) == 0xE0)
    {
        auto staging = std::array<uint8_t, 2>();
        auto dst     = View<uint8_t>(staging.data(), staging.size());

        buffer.slice(1, staging.size()).copyTo(dst);

        if ((staging[0] & 0xC0) != 0x80 || (staging[1] & 0xC0) != 0x80)
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        auto p = (static_cast<char32_t>(b0 & 0x0F) << 12) | (static_cast<char32_t>(staging[0] & 0x3F) << 6) | static_cast<char32_t>(staging[1] & 0x3F);
        if (p < 0x800) // overlong
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        return p;
    }
    else if ((b0 & 0xF8) == 0xF0)
    {
        auto staging = std::array<uint8_t, 3>();
        auto dst     = View<uint8_t>(staging.data(), staging.size());

        buffer.slice(1, staging.size()).copyTo(dst);

        if ((staging[0] & 0xC0) != 0x80 || (staging[1] & 0xC0) != 0x80 || (staging[2] & 0xC0) != 0x80)
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        auto p =
            (static_cast<char32_t>(b0 & 0x07) << 18) |
            (static_cast<char32_t>(staging[0] & 0x3F) << 12) |
            (static_cast<char32_t>(staging[1] & 0x3F) << 6) |
            static_cast<char32_t>(staging[2] & 0x3F);
        // Overlong or beyond U+10FFFF (out-of-range values leaked
        // uninitialized memory into the decoded String via the silent
        // Utf16::encode failure)
        if (p < 0x10000 || p > 0x10FFFF)
        {
            return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
        }
        return p;
    }
    else
    {
        return int{ hx::Throw(HX_CSTRING("Failed to read codepoint")) };
    }
}
