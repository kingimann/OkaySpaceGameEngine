// Browser (page engine): the HTML reader (engine/include/okay/Browser/Html.hpp). Each case reads a page and compares the whole tree
// dump with the expected one, including pages with bad markup (missing end tags, stray end tags, implied <p> and <li> ends,
// unquoted attributes, entities, comments and raw text), and the page's character encoding (Encoding.hpp: BOM, Content-Type,
// the <meta> prescan, the decoders) with a tab showing a page that is not UTF-8.
#include "test_framework.hpp"
#include "okay/Browser/Browser.hpp"
#include "okay/Browser/Encoding.hpp"
#include "okay/Browser/Html.hpp"

#include <string>

using namespace okay::browser;

static bool Tree(const char* html, const std::string& want) {
    const HtmlDocument d = ParseHtml(html);
    const std::string got = DumpTree(*d.root);
    if (got != want) {
        std::cerr << "  input: " << html << "\n  --- want ---\n" << want << "  --- got ---\n" << got;
        return false;
    }
    return true;
}

int main() {
    RUN_SUITE("page html");

    // A complete, well-formed page.
    CHECK(Tree("<!DOCTYPE html><html><head><title>Hi</title></head><body><p class=\"a\">Hello <b>world</b></p></body></html>",
               "<!DOCTYPE html>\n"
               "<html>\n"
               "  <head>\n"
               "    <title>\n"
               "      \"Hi\"\n"
               "  <body>\n"
               "    <p>\n"
               "      class=\"a\"\n"
               "      \"Hello \"\n"
               "      <b>\n"
               "        \"world\"\n"));

    // html, head and body are made when they are left out; the title goes into head.
    CHECK(Tree("<title>T</title>Text",
               "<html>\n"
               "  <head>\n"
               "    <title>\n"
               "      \"T\"\n"
               "  <body>\n"
               "    \"Text\"\n"));

    // A <p> is closed by the next <p> and by a block start tag; </p> with no open <p> makes an empty one.
    CHECK(Tree("<p>one<p>two<div>three</div></p>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <p>\n"
               "      \"one\"\n"
               "    <p>\n"
               "      \"two\"\n"
               "    <div>\n"
               "      \"three\"\n"
               "    <p>\n"));

    // <li> closes the open <li>; a nested list keeps its own items.
    CHECK(Tree("<ul><li>a<li>b<ul><li>c</ul><li>d</ul>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <ul>\n"
               "      <li>\n"
               "        \"a\"\n"
               "      <li>\n"
               "        \"b\"\n"
               "        <ul>\n"
               "          <li>\n"
               "            \"c\"\n"
               "      <li>\n"
               "        \"d\"\n"));

    // dt / dd close each other.
    CHECK(Tree("<dl><dt>t<dd>d<dt>u</dl>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <dl>\n"
               "      <dt>\n"
               "        \"t\"\n"
               "      <dd>\n"
               "        \"d\"\n"
               "      <dt>\n"
               "        \"u\"\n"));

    // Void elements take no children; a self-closing slash on a normal element is ignored; </br> is a <br>.
    CHECK(Tree("<p>a<br>b<img src=x.png alt='A B'>c</br><span/>d</p>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <p>\n"
               "      \"a\"\n"
               "      <br>\n"
               "      \"b\"\n"
               "      <img>\n"
               "        src=\"x.png\"\n"
               "        alt=\"A B\"\n"
               "      \"c\"\n"
               "      <br>\n"
               "      <span>\n"
               "        \"d\"\n"));

    // Attributes: unquoted, single and double quotes, no value, upper case names, a repeated name keeps the first.
    CHECK(Tree("<div ID=Main data-x='1' hidden title=\"a&amp;b\" id=other></div>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <div>\n"
               "      id=\"Main\"\n"
               "      data-x=\"1\"\n"
               "      hidden=\"\"\n"
               "      title=\"a&b\"\n"));

    // Character references in text: named, decimal, hex, a legacy name without ';', an unknown name, the C1 table.
    CHECK(Tree("<p>&lt;&gt;&amp;&quot;&#65;&#x42;&copy &bogus; &#128;&nbsp;x</p>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <p>\n"
               "      \"<>&\"AB\xC2\xA9 &bogus; \xE2\x82\xAC\xC2\xA0x\"\n"));
    CHECK(DecodeCharRefs("a&amp;b&#0;c&#xD800;", false) == "a&b\xEF\xBF\xBD" "c\xEF\xBF\xBD");
    // In an attribute, a legacy name followed by '=' or a letter stays as written (query strings).
    CHECK(DecodeCharRefs("?a=1&copy=2&ampx", true) == "?a=1&copy=2&ampx");

    // Comments and a <style> / <script> body are not parsed as tags; <style> text is collected.
    {
        const HtmlDocument d = ParseHtml("<style>p > b { color: red } /* <p> */</style><!-- <b>no</b> --><script>if (a<b) x='</p>';</script><p>z");
        CHECK(d.styleSheets.size() == 1 && d.styleSheets[0] == "p > b { color: red } /* <p> */");
        CHECK(DumpTree(*d.root) ==
              "<html>\n"
              "  <head>\n"
              "    <style>\n"
              "      \"p > b { color: red } /* <p> */\"\n"
              "    <!--  <b>no</b>  -->\n"
              "    <script>\n"
              "      \"if (a<b) x='</p>';\"\n"
              "  <body>\n"
              "    <p>\n"
              "      \"z\"\n");
    }

    // Stray end tags are ignored; an end tag that would cross a block is ignored; unclosed elements close at the end.
    CHECK(Tree("</i><div><b>bold<p>para</b>still</div></span>tail",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <div>\n"
               "      <b>\n"
               "        \"bold\"\n"
               "        <p>\n"
               "          \"parastill\"\n"
               "    \"tail\"\n"));

    // Headings: a heading start tag closes an open heading.
    CHECK(Tree("<h1>one<h2>two</h2>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <h1>\n"
               "      \"one\"\n"
               "    <h2>\n"
               "      \"two\"\n"));

    // A second <a> closes the first; <pre> drops one newline right after its start tag; <title> decodes references.
    CHECK(Tree("<a href=1>x<a href=2>y</a><pre>\nline</pre>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <a>\n"
               "      href=\"1\"\n"
               "      \"x\"\n"
               "    <a>\n"
               "      href=\"2\"\n"
               "      \"y\"\n"
               "    <pre>\n"
               "      \"line\"\n"));
    {
        const HtmlDocument d = ParseHtml("<title>  A &amp;\n B </title><base href='/docs/'><base href='/other/'>");
        CHECK(d.title == "A & B");
        CHECK(d.baseHref == "/docs/");
    }

    // Tables: td closes td, tr closes tr.
    CHECK(Tree("<table><tr><td>1<td>2<tr><th>3</table>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <table>\n"
               "      <tr>\n"
               "        <td>\n"
               "          \"1\"\n"
               "        <td>\n"
               "          \"2\"\n"
               "      <tr>\n"
               "        <th>\n"
               "          \"3\"\n"));

    // Attributes on a second <body> / <html> are added to the first one.
    CHECK(Tree("<body class=a>x<body id=b class=c>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    class=\"a\"\n"
               "    id=\"b\"\n"
               "    \"x\"\n"));

    // Anchors and text content.
    {
        const HtmlDocument d = ParseHtml("<h2 id=top>Top</h2><a name=old>o</a><p>a<b>b</b>c</p>");
        CHECK(d.FindAnchor("top") && d.FindAnchor("top")->name == "h2");
        CHECK(d.FindAnchor("old") && d.FindAnchor("old")->name == "a");
        CHECK(d.FindAnchor("none") == nullptr);
        CHECK(d.Body() && d.Body()->TextContent() == "Topoabc");
        CHECK(!d.truncated);
    }

    // Upper-case tags and text with a lone '<' and '&'.
    CHECK(Tree("<P>1 < 2 & 3 <> <!x></P>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <p>\n"
               "      \"1 < 2 & 3 <> \"\n"
               "      <!-- x -->\n"));

    // Character encodings (Encoding.hpp). Expected code points worked out from the code pages' charts, not from the tables.
    {
        CHECK(EncodingForLabel(" ISO-8859-1 ") == TextEncoding::Windows1252);
        CHECK(EncodingForLabel("latin1") == TextEncoding::Windows1252);
        CHECK(EncodingForLabel("US-ASCII") == TextEncoding::Windows1252);
        CHECK(EncodingForLabel("utf8") == TextEncoding::Utf8);
        CHECK(EncodingForLabel("iso-8859-9") == TextEncoding::Windows1254);
        CHECK(EncodingForLabel("tis-620") == TextEncoding::Windows874);
        CHECK(EncodingForLabel("shift_jis") == TextEncoding::Unknown);   // not decoded yet
        CHECK(EncodingForLabel("utf-8x") == TextEncoding::Unknown);
        CHECK(std::string(EncodingName(TextEncoding::Iso8859_15)) == "ISO-8859-15");
        CHECK(CharsetFromContentType("text/html; charset=ISO-8859-1") == "ISO-8859-1");
        CHECK(CharsetFromContentType("text/html;Charset=\"utf-8\"; q=1") == "utf-8");
        CHECK(CharsetFromContentType("text/html").empty());

        // windows-1252: the 0x80-0x9F row is not C1 controls (euro, quotes, dashes), five bytes pass through.
        CHECK(DecodeToUtf8("Fran\xE7" "ais", TextEncoding::Windows1252) == "Fran\xC3\xA7" "ais");
        CHECK(DecodeToUtf8("\x80\x93\x94\x9F\x81", TextEncoding::Windows1252) == "\xE2\x82\xAC\xE2\x80\x9C\xE2\x80\x9D\xC5\xB8\xC2\x81");
        CHECK(DecodeToUtf8("\xA9\xFF", TextEncoding::Windows1252) == "\xC2\xA9\xC3\xBF");
        // ISO-8859-15: 0xA4 is the euro sign, 0xBC/0xBD are OE/oe.
        CHECK(DecodeToUtf8("\xA4\xBC\xBD", TextEncoding::Iso8859_15) == "\xE2\x82\xAC\xC5\x92\xC5\x93");
        // windows-1251 (Cyrillic): 0xCF 0xF0 0xE8 = "При"; KOI8-R 0xF0 0xD2 0xC9 = the same word.
        CHECK(DecodeToUtf8("\xCF\xF0\xE8", TextEncoding::Windows1251) == "\xD0\x9F\xD1\x80\xD0\xB8");
        CHECK(DecodeToUtf8("\xF0\xD2\xC9", TextEncoding::Koi8R) == "\xD0\x9F\xD1\x80\xD0\xB8");
        // ISO-8859-2: 0xA3 = L with stroke; windows-1250 0x8A = S with caron; ISO-8859-7 0xC1 = Greek capital alpha.
        CHECK(DecodeToUtf8("\xA3", TextEncoding::Iso8859_2) == "\xC5\x81");
        CHECK(DecodeToUtf8("\x8A", TextEncoding::Windows1250) == "\xC5\xA0");
        CHECK(DecodeToUtf8("\xC1", TextEncoding::Iso8859_7) == "\xCE\x91");
        // A byte with no mapping gives U+FFFD (ISO-8859-3 0xA5; windows-1253 0xAA).
        CHECK(DecodeToUtf8("a\xA5" "b", TextEncoding::Iso8859_3) == "a\xEF\xBF\xBD" "b");
        CHECK(DecodeToUtf8("\xAA", TextEncoding::Windows1253) == "\xEF\xBF\xBD");
        // UTF-8: one U+FFFD per maximal subpart; UTF-16 with a pair, a lone surrogate and an odd last byte.
        CHECK(DecodeToUtf8("a\xE2\x82" "b\xFF", TextEncoding::Utf8) == "a\xEF\xBF\xBD" "b\xEF\xBF\xBD");
        CHECK(DecodeToUtf8(std::string("A\0\x3D\xD8\x00\xDE", 6), TextEncoding::Utf16LE) == "A\xF0\x9F\x98\x80");
        CHECK(DecodeToUtf8(std::string("\0A\xD8\x00\0B\0", 7), TextEncoding::Utf16BE) == "A\xEF\xBF\xBD" "B\xEF\xBF\xBD");
        CHECK(DecodeToUtf8("abc", TextEncoding::Replacement) == "\xEF\xBF\xBD");
        // A BOM wins over the label and is dropped.
        CHECK(DecodeToUtf8("\xEF\xBB\xBF" "\xC3\xA9", TextEncoding::Windows1252) == "\xC3\xA9");
        CHECK(DecodeToUtf8(std::string("\xFF\xFEh\0i\0", 6), TextEncoding::Windows1252) == "hi");
        // The table's windows-1252 row is the same one HTML uses for &#128; .. &#159;.
        CHECK(SingleByteCodePoint(TextEncoding::Windows1252, 0x80) == 0x20AC);
        CHECK(SingleByteCodePoint(TextEncoding::Utf8, 0x80) == 0xFFFD);

        // The <meta> prescan (HTML 13.2.3.2).
        CHECK(PrescanMetaCharset("<html><head><meta charset=\"ISO-8859-15\"><title>x</title>") == TextEncoding::Iso8859_15);
        CHECK(PrescanMetaCharset("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=windows-1251\">") == TextEncoding::Windows1251);
        CHECK(PrescanMetaCharset("<META CONTENT='text/html;charset=koi8-r' HTTP-EQUIV=content-type>") == TextEncoding::Koi8R);
        CHECK(PrescanMetaCharset("<meta content=\"text/html; charset=windows-1251\">") == TextEncoding::Unknown);   // no pragma
        CHECK(PrescanMetaCharset("<!-- <meta charset=latin2> --><meta charset=utf-8>") == TextEncoding::Utf8);
        CHECK(PrescanMetaCharset("<div title='<meta charset=latin2>'><meta charset=koi8-u>") == TextEncoding::Koi8U);
        CHECK(PrescanMetaCharset("<meta charset=utf-16le>") == TextEncoding::Utf8);
        CHECK(PrescanMetaCharset("<meta charset=x-user-defined>") == TextEncoding::Windows1252);
        CHECK(PrescanMetaCharset(std::string(1100, ' ') + "<meta charset=latin2>") == TextEncoding::Unknown);   // past 1024 bytes
        CHECK(PrescanMetaCharset("<meta charset=\"") == TextEncoding::Unknown);   // cut short: no crash
        CHECK(EncodingFromMetaContent("text/html; charset = 'iso-8859-2' ") == TextEncoding::Iso8859_2);
        CHECK(EncodingFromMetaContent("charsetx; charset=latin1") == TextEncoding::Windows1252);

        // The whole sniffing order: BOM > Content-Type > meta > UTF-8 detection > windows-1252.
        CHECK(DecodePage("<meta charset=utf-8>Fran\xE7" "ais", "text/html; charset=ISO-8859-1").text ==
              "<meta charset=utf-8>Fran\xC3\xA7" "ais");
        CHECK(std::string(DecodePage("x", "text/html; charset=latin1").source) == "content-type");
        CHECK(DecodePage("<meta charset=windows-1252>\x93q\x94", "").text == "<meta charset=windows-1252>\xE2\x80\x9Cq\xE2\x80\x9D");
        CHECK(DecodePage("caf\xC3\xA9", "").encoding == TextEncoding::Utf8);
        CHECK(DecodePage("caf\xE9", "").text == "caf\xC3\xA9");   // not UTF-8, no label: windows-1252
        CHECK(DecodePage("\xEF\xBB\xBF" "caf\xC3\xA9", "text/html; charset=latin1").text == "caf\xC3\xA9");
    }

    // A tab showing a page that is not UTF-8: the text reaches the document as UTF-8 (the owner's "Fran?ais").
    {
        BrowserTab tab;
        CHECK(tab.Open("https://enc.test/"));
        tab.TakeFetches();
        tab.DeliverPage("https://enc.test/", true, 200,
                        "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=ISO-8859-1\"></head>"
                        "<body><p>Fran\xE7" "ais \x93" "ok\x94</p></body></html>");
        CHECK(tab.Document() && tab.Document()->Body() &&
              tab.Document()->Body()->TextContent() == "Fran\xC3\xA7" "ais \xE2\x80\x9C" "ok\xE2\x80\x9D");
        // The server's Content-Type wins over the page's own <meta> (HTML 13.2.3.2: the transport layer comes first).
        BrowserTab tab2;
        CHECK(tab2.Open("https://enc.test/"));
        tab2.TakeFetches();
        tab2.DeliverPage("https://enc.test/", true, 200, "<meta charset=utf-8><p>Fran\xE7" "ais", std::string(),
                         "text/html; charset=ISO-8859-1");
        CHECK(tab2.Document() && tab2.Document()->Body() && tab2.Document()->Body()->TextContent() == "Fran\xC3\xA7" "ais");
    }

    // </dd> and </dt> close only their own element (13.2.6.4.7 "generate implied end tags, except for ... the same tag name"):
    // they used to pop everything up to <html>, so the rest of the page landed after <body> (MDN's menus, any definition list)
    CHECK(Tree("<body><div><dl><dt>t</dt><dd>x</dd></dl><p>after</p></div>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <div>\n"
               "      <dl>\n"
               "        <dt>\n"
               "          \"t\"\n"
               "        <dd>\n"
               "          \"x\"\n"
               "      <p>\n"
               "        \"after\"\n"));
    CHECK(Tree("<ul><li>a<dl><dt>b</dt></dl></li><li>c</li></ul>",
               "<html>\n"
               "  <head>\n"
               "  <body>\n"
               "    <ul>\n"
               "      <li>\n"
               "        \"a\"\n"
               "        <dl>\n"
               "          <dt>\n"
               "            \"b\"\n"
               "      <li>\n"
               "        \"c\"\n"));

    // Quirks mode from the DOCTYPE (HTML 13.2.6.4.1).
    CHECK(!ParseHtml("<!DOCTYPE html><p>x").quirks);
    CHECK(!ParseHtml("<!doctype HTML>").quirks);
    CHECK(ParseHtml("<p>x").quirks);
    CHECK(ParseHtml("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"><p>").quirks);
    CHECK(!ParseHtml("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\"><p>").quirks);
    CHECK(!ParseHtml("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">").quirks);
    CHECK(ParseHtml("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">").quirks);
    CHECK(ParseHtml("<!DOCTYPE svg>").quirks);
    CHECK(ParseHtml("<p>x<!DOCTYPE html>").quirks);   // too late: after the first tag

    TEST_MAIN_RESULT();
}
