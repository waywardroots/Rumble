#!/usr/bin/env python3
"""Convert the signed EULA PDF text into installer-ready license files.

Source of truth: ../End User License Agreement - FuzzyAudio - Rumble.pdf
Produces:
  EULA.txt  - pure ASCII, 78-col wrapped  (NSIS / Inno / macOS pkg plain license)
  EULA.rtf  - rich text                    (WiX / Inno / macOS pkg formatted license)

Segmentation note: the PDF has no reliable blank line between some paragraphs
(page breaks drop the separator), so we start a new paragraph on a blank line OR
on a clause marker ("N.", "(a)", "(ii)") OR at the "BY CLICKING" acceptance block.
This never deletes body text; it only removes bare page-number lines.
"""
import re, subprocess, textwrap, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
PDF  = os.path.join(HERE, "..", "End User License Agreement - FuzzyAudio - Rumble.pdf")

raw = subprocess.check_output(["pdftotext", "-layout", "-nopgbrk", PDF, "-"]).decode("utf-8")
raw = raw.replace("\u200b", "")                       # drop zero-width spaces
lines = raw.split("\n")

MARK = re.compile(r"^(?:\d{1,2}\.\s|\([a-z]{1,3}\)\s)")  # "N. ", "(a) ", "(ii) "
PAGENUM = re.compile(r"^\s*\d+\s*$")                     # bare page-number line

paras, cur = [], []
def flush():
    if cur:
        paras.append(re.sub(r"\s+", " ", " ".join(cur).strip()))
        cur.clear()

title = lines[0].strip()
for ln in lines[1:]:
    s = ln.strip()
    if PAGENUM.fullmatch(ln):          # page number -> boundary, drop it
        flush();  continue
    if s == "":                        # blank line -> paragraph boundary
        flush();  continue
    if MARK.match(s) or s.startswith("BY CLICKING"):
        flush()
    cur.append(s)
flush()

# --- character normalization -------------------------------------------------
def to_ascii(t):
    return (t.replace("\u2019", "'").replace("\u2018", "'")
             .replace("\u201c", '"').replace("\u201d", '"')
             .replace("\u2013", "-").replace("\u2014", "--")
             .replace("\u00a9", "(c)").replace("\u00a0", " "))

def rtf_escape(t):                     # keep (c) symbol, straighten quotes
    t = (t.replace("\\", r"\\").replace("{", r"\{").replace("}", r"\}")
          .replace("\u2019", "'").replace("\u2018", "'")
          .replace("\u201c", '"').replace("\u201d", '"')
          .replace("\u2013", "-").replace("\u2014", "--")
          .replace("\u00a0", " ").replace("\u00a9", r"\'a9"))
    return t

# --- EULA.txt (ASCII, wrapped) ----------------------------------------------
out = [to_ascii(title), ""]
for p in paras:
    out.append(textwrap.fill(to_ascii(p), width=78))
    out.append("")
txt = "\n".join(out).rstrip() + "\n"
assert all(ord(c) < 127 for c in txt), "non-ascii leaked into EULA.txt"
open(os.path.join(HERE, "EULA.txt"), "w", encoding="ascii").write(txt)

# --- EULA.rtf ----------------------------------------------------------------
rtf = [r"{\rtf1\ansi\ansicpg1252\deff0{\fonttbl{\f0\fswiss Helvetica;}}",
       r"\fs20\sa120\sl276\slmult1"]
rtf.append(r"\qc\b\fs28 " + rtf_escape(title) + r"\b0\fs20\par\sa120")
rtf.append(r"\ql")
for p in paras:
    rtf.append(rtf_escape(p) + r"\par")
rtf.append("}")
open(os.path.join(HERE, "EULA.rtf"), "w", encoding="ascii").write("\n".join(rtf))

print(f"paragraphs: {len(paras)}")
print(f"EULA.txt : {len(txt)} bytes")
print(f"EULA.rtf : {os.path.getsize(os.path.join(HERE,'EULA.rtf'))} bytes")
