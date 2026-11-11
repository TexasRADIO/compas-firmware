#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check design/strings.json against the screen source and the screen font.

The table lists every word Compás draws, with the box it is drawn into
(`max` characters at `scale`). This check fails the build when:

  1. a table entry no longer appears in the source it names (the table drifted);
  2. the screen source has a word the table does not list (a string was added);
  3. an English string is longer than its box;
  4. a Spanish string is longer than its box, or uses a character the font
     cannot draw (the font has Á É Í Ó Ú Ñ Ü, but no ¡ or ¿).

  tools/strings.py                 check, print a one-line summary
  tools/strings.py --sheet FILE    also write the translator's review sheet

Boxes were measured by building the golden replay and tests/stage-host.c with
-DSTAGE_PRINT_TRACE (board/stage.c logs every print() and text_line() box),
plus the draw calls the 85 recorded states never reach.
"""
import argparse, json, re, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
SOURCES = ["board/stage.c", "board/panel.c", "board/stage-rack.inc", "board/stage-bass.inc",
           "board/stage-percussion.inc", "board/board.c"]
# The sign font (print()): A-Z, digits, these marks, and the accented capitals.
SIGN = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -%+:./>!'#") | set("ÁÉÍÓÚÑÜ")
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def source_literals():
    found = {}
    for name in SOURCES:
        for n, line in enumerate((root / name).read_text(encoding="latin-1").splitlines(), 1):
            s = line.strip()
            if s.startswith(("#", "/*", "*")) or "_Static_assert" in s:
                continue
            for m in LITERAL.finditer(line):
                if re.search(r"[A-Za-z]{2}", m.group(1)):
                    found.setdefault(m.group(1).rstrip(), []).append(f"{name}:{n}")
    score = json.loads((root / "src/score.json").read_text())
    for form, parts in score["conductorForms"].items():
        for long, short in parts:
            for word in (long.upper(), short.upper()):
                found.setdefault(word, []).append(f"src/score.json:conductorForms.{form}")
    # tools/score.py also generates the role names (face row 3) and GLO family tags.
    for study in score["studies"]:
        for part in study["parts"]:
            word = part[0].replace("\u2014", "NO PART").replace("ö", "o").upper()
            if word:
                found.setdefault(word, []).append(f"src/score.json:studies.{study['id']}.parts")
    family = re.findall(r':\s*"([A-Za-z]+)"', (root / "tools/score.py").read_text().split("def family")[0].split("FAMILY_BY_ID")[1])
    for word in family + ["Cumbia", "Salsa", "Merengue"]:
        found.setdefault(word.upper(), []).append("tools/score.py:family")
    return found


PLACEHOLDER = re.compile(r"\{(\w+)\}")


def shown(text, fill):
    """Display width: a placeholder counts as the digits the code writes there."""
    return len(PLACEHOLDER.sub(lambda m: "0" * fill.get(m.group(1), 2), text))


def check(table):
    errors, found = [], source_literals()
    raw = "\n".join((root / name).read_text(encoding="latin-1") for name in SOURCES)
    listed = {r.get("source", r["en"]).rstrip() for r in table["strings"]} | {w.rstrip() for w in table["ignore"]}
    for r in table["strings"]:
        en, es, limit = r["en"], r.get("es", ""), r["max"]
        fill = r.get("fill", {})
        if r["kind"] in ("label", "keep", "hidden") and en.rstrip() not in found and f'"{en}' not in raw:
            errors.append(f"{r['id']}: {en!r} is no longer in the source")
        if r["kind"] in ("graphic",):
            continue
        if limit is None:
            errors.append(f"{r['id']}: no box size recorded")
            continue
        if r["kind"] in ("label", "keep", "template") and shown(en, fill) > limit and not r.get("known_overflow"):
            errors.append(f"{r['id']}: English {en!r} is {shown(en, fill)} characters, box holds {limit}")
        if r["kind"] in ("label", "template", "new") and es:
            if shown(es, fill) > limit:
                errors.append(f"{r['id']}: Spanish {es!r} is {shown(es, fill)} characters, box holds {limit}")
            if sorted(PLACEHOLDER.findall(es)) != sorted(PLACEHOLDER.findall(en)):
                errors.append(f"{r['id']}: Spanish {es!r} changes the placeholders of {en!r}")
            bad = sorted(set(PLACEHOLDER.sub("", es)) - SIGN)
            if bad:
                errors.append(f"{r['id']}: Spanish {es!r} uses {''.join(bad)!r}, which the font cannot draw")
    for word, where in sorted(found.items()):
        if word not in listed:
            errors.append(f"{where[0]}: {word!r} is drawn on screen but missing from design/strings.json")
    return errors


def sheet(table, path):
    rows = [r for r in table["strings"] if r["kind"] in ("label", "template", "new")]
    done = sum(bool(r.get("es")) for r in rows)
    out = ["# Compás 1.1 · Spanish screen strings, review sheet", "",
           "Generated by `tools/strings.py --sheet` from `design/strings.json`.",
           "", "```",
           f"Strings   {len(rows)} to translate, {done} drafted, {len(rows) - done} open",
           "Register  Mexican and Tejano working musicians (glossary v2 + project audit, 7 Nov)",
           "Limit     Max = characters that fit the box. The build fails if Spanish is longer.",
           "Font      Capitals, digits, - % + : . / > ! ' #, and Á É Í Ó Ú Ñ Ü. No ¡ or ¿.",
           "```", "",
           "Fill the Spanish column. Keep within Max. Leave a cell empty to keep English.",
           "", "| # | id | English | Max | Spanish | Note | Where |", "| ---: | --- | --- | ---: | --- | --- | --- |"]
    for i, r in enumerate(rows, 1):
        en = r["en"].rstrip() + (" (template)" if r["kind"] == "template" else "")
        where = ", ".join(sorted({":".join(w.split(":")[:2]) for w in r.get("where", [])}))
        out.append(f"| {i} | `{r['id']}` | {en} | {r['max']} | {r.get('es', '')} | {r.get('note', '')} | {where} |")
    keep = [r for r in table["strings"] if r["kind"] == "keep"]
    out += ["", "## Kept as they are", "", "| English | Why |", "| --- | --- |"]
    out += [f"| {r['en'].rstrip()} | {r.get('note', '')} |" for r in keep]
    Path(path).write_text("\n".join(out) + "\n")


# The screen font draws accented capitals from these lowercase codes (stage.c glyph).
FONT = str.maketrans({"Á": "a", "É": "e", "Í": "i", "Ó": "o", "Ú": "u", "Ñ": "n", "Ü": "v"})
LANGUAGES = ["es"]          # after English. A new column in the table adds a language.
# Ids whose Spanish depends on where the word is drawn; stage.c picks them by name.
CONTEXT = {"rack.pad.bongo_macho": "bongo_hi", "rack.pad.bongo_hembra": "bongo_lo",
           "value.repeat_hold": "repeat_hold", "knob.bell": "knob_bell"}
# Words drawn with a number; stage.c fills '#' with the digits (Spanish only).
FORMAT = {"COUNT {n}": "count", "IN {n}": "in", "{nn} MORE": "more", "NOW {value}": "now",
          "NEXT {value}": "next"}


def c_string(text):
    return '"' + text.translate(FONT).replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit(table):
    """board/lang.inc: a sorted English → other-language lookup, plus context words."""
    pairs, context, formats = {}, {}, {}
    for r in table["strings"]:
        en = r["en"]
        words = [r.get(lang, "") for lang in LANGUAGES]
        if r["id"] in CONTEXT:
            context[CONTEXT[r["id"]]] = (r["en"], words)
            continue
        if r["kind"] == "template" and en in FORMAT:
            formats[FORMAT[en]] = (en, [PLACEHOLDER.sub("#", w) for w in words])
            continue
        if r["kind"] not in ("label", "new") or not any(words) or all(w == en for w in words):
            continue
        key = en.translate(FONT)
        old = pairs.get(key)
        # Same English in two places: keep the longer form; each box cuts it to fit.
        if old is None or sum(map(len, words)) > sum(map(len, old)):
            pairs[key] = [w or en for w in words]
    keys = sorted(pairs)
    outputs = {w.translate(FONT) for ws in pairs.values() for w in ws}
    clash = sorted(set(keys) & outputs - {k for k in keys if k in pairs[k][0].translate(FONT)})
    if clash:
        raise SystemExit(f"strings: translations that are also English keys: {clash}")
    blob, offsets = [], []
    at = 0
    for k in keys:
        offsets.append(at)
        for part in [k] + [w.translate(FONT) for w in pairs[k]]:
            blob.append(part); at += len(part.encode("latin-1")) + 1
    if at > 65535:
        raise SystemExit("strings: language table over 64 KB")
    out = ["/* Generated by tools/strings.py from design/strings.json. Do not edit. */",
           f"enum {{ LANG_COUNT={len(LANGUAGES) + 1}, LANG_WORDS={len(keys)} }};",
           "/* Each entry: English, then one word per language, each NUL-terminated. */",
           f"static const char *const lang_name[LANG_COUNT] = {{\"EN\",{','.join(c_string(l.upper()) for l in LANGUAGES)}}};",
           "#ifdef LANG_TABLE /* panel.c only: the lookup itself */",
           "static const char lang_text[] ="]
    out += [f"    {c_string(b)} \"\\0\"" for b in blob]
    out[-1] += ";"
    out.append("static const uint16_t lang_index[LANG_WORDS] = {")
    out += ["    " + ",".join(map(str, offsets[i:i + 16])) + "," for i in range(0, len(offsets), 16)]
    out.append("};")
    out.append("#endif")
    for name, (en, words) in sorted(context.items()):
        out.append(f"static const char *const lang_{name}[LANG_COUNT] = {{{c_string(en)},{','.join(c_string(w) for w in words)}}};")
    for name, (en, words) in sorted(formats.items()):
        out.append(f"static const char *const lang_format_{name}[LANG_COUNT-1] = {{{','.join(c_string(w) for w in words)}}};")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--table", type=Path, default=root / "design/strings.json")
    parser.add_argument("--sheet", type=Path)
    parser.add_argument("--emit", action="store_true", help="rewrite board/lang.inc from the table")
    args = parser.parse_args()
    table = json.loads(args.table.read_text())
    errors = check(table)
    generated = emit(table)
    lang = root / "board/lang.inc"
    if args.emit:
        lang.write_text(generated, encoding="latin-1")
    elif not lang.exists() or lang.read_text(encoding="latin-1") != generated:
        errors.append("board/lang.inc is out of date: run tools/strings.py --emit")
    if args.sheet:
        sheet(table, args.sheet)
    for e in errors:
        print("strings:", e, file=sys.stderr)
    labels = [r for r in table["strings"] if r["kind"] in ("label", "template")]
    spanish = sum(bool(r.get("es")) for r in labels)
    print(f"Strings {'FAILED' if errors else 'fit'}: {len(labels)} screen strings, "
          f"{spanish} with Spanish, {len(errors)} problem(s).")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
