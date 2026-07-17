#!/usr/bin/env python3
"""Embed web/index.html into web_page.h as a PROGMEM raw string literal."""
import pathlib, sys

here = pathlib.Path(__file__).parent
html = (here / "index.html").read_text(encoding="utf-8")

if ')html"' in html:
    sys.exit("ERROR: index.html contains the raw-literal terminator )html\"")

out = (
    "// Auto-embedded from web/index.html — do not edit by hand.\n"
    "// Regenerate with:  python3 web/embed.py\n"
    "#pragma once\n"
    'static const char INDEX_HTML[] PROGMEM = R"html(' + html + ')html";\n'
)
(here.parent / "web_page.h").write_text(out, encoding="utf-8")
print(f"web_page.h written: {len(html)} bytes of HTML")
