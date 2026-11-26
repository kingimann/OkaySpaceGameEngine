// Browser (page engine): the HTML reader (engine/include/okay/Browser/Html.hpp). Each case reads a page and compares the whole tree
// dump with the expected one, including pages with bad markup (missing end tags, stray end tags, implied <p> and <li> ends,
// unquoted attributes, entities, comments and raw text).
#include "test_framework.hpp"
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

    TEST_MAIN_RESULT();
}
