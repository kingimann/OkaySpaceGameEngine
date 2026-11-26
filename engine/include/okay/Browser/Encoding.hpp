#pragma once
// Browser: character encodings of web pages (system "Browser", docs/feature_map/17-launcher-platform.md 17.18).
// A page arrives as bytes; the rest of the browser reads UTF-8. This file works out which encoding the bytes are in and
// turns them into UTF-8, written from the WHATWG Encoding standard (encoding names and labels, the UTF-8 / UTF-16 /
// single-byte decoders, BOM sniffing, "get an encoding") and the HTML standard 13.2.3 (the encoding sniffing algorithm,
// "prescan a byte stream to determine its encoding", "extract a character encoding from a meta element").
//
// The order (HTML 13.2.3.2): a byte order mark, then the charset of the Content-Type the server sent, then a
// <meta charset> / <meta http-equiv="Content-Type"> in the first 1024 bytes, then (our autodetection step) UTF-8 when
// the bytes are well-formed UTF-8, else windows-1252 (the standard's default for most locales).
//
// Decoded: UTF-8, UTF-16LE, UTF-16BE, the single-byte encodings (IBM866, ISO-8859-2 to -16, KOI8-R, KOI8-U, macintosh,
// windows-874, windows-1250 to -1258, x-mac-cyrillic; ISO-8859-1 and US-ASCII are labels of windows-1252, ISO-8859-9 of
// windows-1254, ISO-8859-11 of windows-874), x-user-defined and "replacement". Not yet: the CJK multi-byte encodings
// (Shift_JIS, EUC-JP, ISO-2022-JP, GBK, gb18030, Big5, EUC-KR); their labels are known but read as Unknown, so such a
// page falls through to the autodetection step. Bytes that do not map give U+FFFD. Standard library only.
#include <cstddef>
#include <cstdint>
#include <string>

namespace okay::browser {

enum class TextEncoding : std::uint8_t {
    Unknown,   ///< no label, or a label of an encoding we do not decode yet
    Utf8, Utf16LE, Utf16BE, Replacement, XUserDefined,
    // the single-byte encodings, in the order of their tables in Encoding.cpp
    Ibm866, Iso8859_2, Iso8859_3, Iso8859_4, Iso8859_5, Iso8859_6, Iso8859_7, Iso8859_8, Iso8859_10, Iso8859_13, Iso8859_14,
    Iso8859_15, Iso8859_16, Koi8R, Koi8U, Macintosh, Windows874, Windows1250, Windows1251, Windows1252, Windows1253, Windows1254,
    Windows1255, Windows1256, Windows1257, Windows1258, XMacCyrillic,
};

/// The standard's name of `e` ("UTF-8", "windows-1252" ...; "" for Unknown).
const char* EncodingName(TextEncoding e);

/// "Get an encoding": the encoding a label names ("latin1", " UTF8 ", "iso-8859-1" ...). Leading and trailing ASCII
/// whitespace is dropped and the match ignores ASCII case. Unknown for anything else.
TextEncoding EncodingForLabel(const std::string& label);

/// The encoding a byte order mark at the start of `bytes` names (UTF-8 EF BB BF, UTF-16BE FE FF, UTF-16LE FF FE), and its
/// length in `bomLength`; Unknown (and 0) when there is none.
TextEncoding SniffBom(const std::string& bytes, std::size_t& bomLength);

/// The value of the charset parameter of a Content-Type header ("text/html; charset=ISO-8859-1" -> "ISO-8859-1"; quotes
/// removed), or "" when there is none.
std::string CharsetFromContentType(const std::string& contentType);

/// HTML "extract a character encoding from a meta element": the encoding named by charset= in a <meta http-equiv> content
/// attribute ("text/html; charset=utf-8"), or Unknown.
TextEncoding EncodingFromMetaContent(const std::string& content);

/// HTML "prescan a byte stream to determine its encoding" over the first 1024 bytes: the encoding a <meta charset> or a
/// <meta http-equiv="content-type" content="...charset=..."> names (comments and other tags' attributes are skipped). A
/// UTF-16 answer becomes UTF-8 and x-user-defined becomes windows-1252, as the standard says. Unknown when there is none.
TextEncoding PrescanMetaCharset(const std::string& bytes);

/// The code point byte `b` stands for in single-byte encoding `e` (bytes below 0x80 are ASCII); U+FFFD when the byte has
/// no mapping, or `e` is not a single-byte encoding. windows-1252's 0x80-0x9F row is also HTML's numeric reference fix-up.
std::uint32_t SingleByteCodePoint(TextEncoding e, unsigned char b);

/// Decode `bytes` from `e` to UTF-8 (a BOM at the start is dropped). Unknown and Utf8 read as UTF-8; malformed input gives
/// U+FFFD per the standard's decoders.
std::string DecodeToUtf8(const std::string& bytes, TextEncoding e);

/// A page's bytes decoded by the HTML encoding sniffing algorithm (see the top of this file).
struct DecodedPage {
    std::string text;                             ///< UTF-8, without a BOM
    TextEncoding encoding = TextEncoding::Utf8;   ///< what the bytes were read as
    const char* source = "default";               ///< "bom", "content-type", "meta", "utf-8 detected" or "default"
};
DecodedPage DecodePage(const std::string& bytes, const std::string& contentType = std::string());

} // namespace okay::browser
