package tests.encoding;

import haxe.io.Bytes;
import cpp.encoding.Ascii;
import utest.Assert;
import utest.Test;

using cpp.marshal.ViewExtensions;

class TestAscii extends Test
{
	function test_isEncoded_null() {
		Assert.raises(() -> Ascii.isEncoded(null));
	}

	function test_isEncoded_ascii() {
		Assert.isTrue(Ascii.isEncoded("test"));
	}

	function test_isEncoded_utf16() {
		Assert.isFalse(Ascii.isEncoded("😂"));
	}

	function test_encode_null() {
		final buffer = Bytes.alloc(4);

		Assert.raises(() -> Ascii.encode(null, buffer.asView()));
	}

	function test_encode_small_buffer() {
		final buffer = Bytes.alloc(2);

		Assert.raises(() -> Ascii.encode("test", buffer.asView()));
	}

	function test_encode_utf16() {
		final buffer = Bytes.alloc(1024);

		Assert.raises(() -> Ascii.encode("😂", buffer.asView()));
	}

	function test_encode() {
		final buffer = Bytes.alloc(1024);

		Assert.equals(4i64, Ascii.encode("test", buffer.asView()));
		Assert.equals('t'.code, buffer.get(0));
		Assert.equals('e'.code, buffer.get(1));
		Assert.equals('s'.code, buffer.get(2));
		Assert.equals('t'.code, buffer.get(3));
	}

	function test_decode_empty() {
		// Like Utf8.decode and Utf16.decode
		Assert.equals('', Ascii.decode(ViewExtensions.empty()));
	}

	function test_decode() {
		final buffer = Bytes.alloc(4);
		buffer.set(0, 't'.code);
		buffer.set(1, 'e'.code);
		buffer.set(2, 's'.code);
		buffer.set(3, 't'.code);
		
		Assert.equals('test', Ascii.decode(buffer.asView()));
	}

	function test_decode_keeps_nul() {
		// The view's length is authoritative: a NUL is a character, not the end
		final buffer = Bytes.alloc(9);
		buffer.set(0, 't'.code);
		buffer.set(1, 'e'.code);
		buffer.set(2, 's'.code);
		buffer.set(3, 't'.code);
		buffer.set(4, 0);
		buffer.set(5, 't'.code);
		buffer.set(6, 'e'.code);
		buffer.set(7, 's'.code);
		buffer.set(8, 't'.code);

		final decoded = Ascii.decode(buffer.asView());
		Assert.equals(9, decoded.length);
		Assert.equals(0, decoded.charCodeAt(4));
		Assert.equals('test', decoded.substr(5));
	}

	function test_decode_only_nul() {
		final buffer = Bytes.alloc(1);

		final decoded = Ascii.decode(buffer.asView());
		Assert.equals(1, decoded.length);
		Assert.equals(0, decoded.charCodeAt(0));
	}
}