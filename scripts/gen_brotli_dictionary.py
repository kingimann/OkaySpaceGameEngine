#!/usr/bin/env python3
"""Write engine/src/Core/BrotliDictionary.inc from the text of RFC 7932 (Brotli Compressed Data Format).

The data comes from the RFC itself, nothing else:
  - Appendix A: the static dictionary DICT (122,784 bytes, printed as hexadecimal lines), checked against the length and
    CRC-32 the appendix states (0x5136cb04);
  - Appendix B: the 121 word transforms (prefix, elementary transform, suffix), checked against the serialised length and
    CRC-32 the appendix states (648 bytes, 0x3d965f81).
It also writes the licence text the RFC asks extracted Code Components to carry (IETF Trust Legal Provisions section 4.e,
Simplified BSD License) next to the data and into the shipped font folder (assets/fonts/ is copied into the Windows package).

Usage: python3 scripts/gen_brotli_dictionary.py path/to/rfc7932.txt
The RFC text is not kept in the repository; get it from https://www.rfc-editor.org/rfc/rfc7932.txt.
Standard library only. Our own script (CLAUDE.md rule 19a): it parses the RFC's text layout.
"""
import os
import re
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_INC = os.path.join(ROOT, "engine", "src", "Core", "BrotliDictionary.inc")
OUT_LICENSES = [os.path.join(ROOT, "engine", "src", "Core", "BrotliDictionary-LICENSE.txt"),
                os.path.join(ROOT, "assets", "fonts", "RFC7932-Brotli-dictionary-LICENSE.txt")]

DICT_SIZE, DICT_CRC = 122784, 0x5136CB04
TRANSFORMS_SIZE, TRANSFORMS_CRC = 648, 0x3D965F81
# The value of each elementary transform in the serialised form, as Appendix B defines it.
KINDS = ["Identity", "FermentFirst", "FermentAll"] + ["OmitFirst%d" % k for k in range(1, 10)] + ["OmitLast%d" % k for k in range(1, 10)]

LICENSE = """The Brotli static dictionary and the word transform list in BrotliDictionary.inc are data taken from
RFC 7932, "Brotli Compressed Data Format", Appendix A and Appendix B (J. Alakuijala and Z. Szabadka, July 2016,
https://www.rfc-editor.org/info/rfc7932). The RFC states that Code Components extracted from it must include the
Simplified BSD License text described in Section 4.e of the IETF Trust Legal Provisions. That text follows.

Copyright (c) 2016 IETF Trust and the persons identified as authors of the code. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

- Redistributions of source code must retain the above copyright notice, this list of conditions and the following
  disclaimer.

- Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
  disclaimer in the documentation and/or other materials provided with the distribution.

- Neither the name of Internet Society, IETF or IETF Trust, nor the names of specific contributors, may be used to
  endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""


def page_free_lines(text):
    """The RFC's lines without the page footers, the form feeds and the page headers."""
    out = []
    for line in text.splitlines():
        line = line.replace("\f", "")
        if re.match(r"^Alakuijala & Szabadka\s+Informational\s+\[Page \d+\]$", line):
            continue
        if re.match(r"^RFC 7932\s+Brotli\s+July 2016$", line):
            continue
        out.append(line)
    return out


def section(lines, start, stop):
    a = next(i for i, l in enumerate(lines) if l.startswith(start))
    b = next(i for i in range(a + 1, len(lines)) if lines[i].startswith(stop))
    return lines[a:b]


def parse_dictionary(lines):
    hexdigits = []
    for line in section(lines, "Appendix A.", "Appendix B."):
        s = line.strip()
        if re.fullmatch(r"[0-9a-f]+", s) and len(s) % 2 == 0:
            hexdigits.append(s)
    data = bytes.fromhex("".join(hexdigits))
    if len(data) != DICT_SIZE or zlib.crc32(data) != DICT_CRC:
        sys.exit("dictionary: %d bytes, CRC-32 0x%08x; the RFC states %d and 0x%08x" % (len(data), zlib.crc32(data), DICT_SIZE, DICT_CRC))
    return data


def c_string(token):
    """A C string literal of Appendix B (with its quotes) as bytes: \\" \\n \\t \\xHH escapes."""
    assert token[0] == '"' and token[-1] == '"', token
    body, out, i = token[1:-1], bytearray(), 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out += c.encode("ascii"); i += 1; continue
        e = body[i + 1]
        if e == "n": out.append(10); i += 2
        elif e == "t": out.append(9); i += 2
        elif e == '"': out.append(34); i += 2
        elif e == "\\": out.append(92); i += 2
        elif e == "x": out.append(int(body[i + 2:i + 4], 16)); i += 4
        else: sys.exit("unknown escape in %s" % token)
    return bytes(out)


def parse_transforms(lines):
    rows = []
    row = re.compile(r'^\s*(\d+)\s+("(?:[^"\\]|\\.)*")\s+(\w+)\s+("(?:[^"\\]|\\.)*")\s*$')
    for line in section(lines, "Appendix B.", "Appendix C."):
        m = row.match(line)
        if m:
            rows.append((int(m.group(1)), c_string(m.group(2)), m.group(3), c_string(m.group(4))))
    if [r[0] for r in rows] != list(range(121)):
        sys.exit("transforms: expected ids 0..120, got %d rows" % len(rows))
    ser = bytearray()
    for _, prefix, kind, suffix in rows:
        ser += prefix + b"\0" + bytes([KINDS.index(kind)]) + suffix + b"\0"
    if len(ser) != TRANSFORMS_SIZE or zlib.crc32(bytes(ser)) != TRANSFORMS_CRC:
        sys.exit("transforms: %d bytes, CRC-32 0x%08x; the RFC states %d and 0x%08x" % (len(ser), zlib.crc32(bytes(ser)), TRANSFORMS_SIZE, TRANSFORMS_CRC))
    return rows


def c_bytes(b):
    """A C++ string literal for a short byte string (escapes for everything but plain letters and spaces)."""
    return '"' + "".join(chr(c) if (48 <= c <= 57 or 65 <= c <= 90 or 97 <= c <= 122 or c in b" ,.:;()=/'") else "\\%03o" % c for c in b) + '"'


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="ascii") as f:
        lines = page_free_lines(f.read())
    data = parse_dictionary(lines)
    rows = parse_transforms(lines)
    out = []
    out.append("// GENERATED by scripts/gen_brotli_dictionary.py from the text of RFC 7932 -- do not edit by hand.")
    out.append("// Data from RFC 7932 (Copyright (c) 2016 IETF Trust and the persons identified as the document authors), used as a")
    out.append("// Code Component under the Simplified BSD License: see BrotliDictionary-LICENSE.txt next to this file.")
    out.append("// Appendix A: the static dictionary, %d bytes, CRC-32 0x%08x (as the appendix states)." % (len(data), DICT_CRC))
    out.append("// Appendix B: the 121 word transforms {prefix, elementary transform, suffix}; serialised as the appendix describes they")
    out.append("// are %d bytes with CRC-32 0x%08x (as the appendix states). Elementary transform values: 0 Identity, 1 FermentFirst," % (TRANSFORMS_SIZE, TRANSFORMS_CRC))
    out.append("// 2 FermentAll, 3..11 OmitFirst1..9, 12..20 OmitLast1..9.")
    out.append("// Included by engine/src/Core/Brotli.cpp only.")
    out.append("static const unsigned char kBrotliDictionary[%d] = {" % len(data))
    for i in range(0, len(data), 24):
        out.append(",".join(str(c) for c in data[i:i + 24]) + ",")
    out.append("};")
    out.append("struct BrotliTransformRow { const char* prefix; unsigned char kind; const char* suffix; };")
    out.append("static const BrotliTransformRow kBrotliTransforms[121] = {")
    for tid, prefix, kind, suffix in rows:
        out.append("    {%s, %d, %s},   // %d %s" % (c_bytes(prefix), KINDS.index(kind), c_bytes(suffix), tid, kind))
    out.append("};")
    with open(OUT_INC, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    for p in OUT_LICENSES:
        with open(p, "w", encoding="ascii", newline="\n") as f:
            f.write(LICENSE)
    print("wrote %s (%d dictionary bytes, %d transforms) and %d licence files" % (os.path.relpath(OUT_INC, ROOT), len(data), len(rows), len(OUT_LICENSES)))


if __name__ == "__main__":
    main()
