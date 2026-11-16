"""Embed the set and score ledgers in the offline authoring page."""
from pathlib import Path
import base64
import json
from repertoire_midi import midi, rehearsal_midi
from set_catalog import panel_catalog

root=Path(__file__).resolve().parents[1]
out=root/"build"
out.mkdir(exist_ok=True)
def payload(path):
    return json.dumps(json.loads(path.read_text()),ensure_ascii=False).replace("<","\\u003c")
page=(root/"web/set-editor.html").read_text()
page=page.replace("__COMPAS_SET__",payload(root/"board/set.json"))
registry=json.loads((root/"repertoire/board-scores.json").read_text())
page=page.replace("__COMPAS_STUDIES__",json.dumps(panel_catalog(root),ensure_ascii=False).replace("<","\\u003c"))
validator=(root/"web/set-format.mjs").read_text().replace("export {normalizeSet};", "")
page=page.replace("/* COMPAS_SET_FORMAT */",validator)
catalog_path=root/"repertoire/catalog.json"
catalog={entry["id"]:entry for entry in json.loads(catalog_path.read_text())["entries"]}
page=page.replace("__REPERTOIRE__",payload(catalog_path))
def encoded(data):
    return base64.b64encode(data).decode()
page=page.replace("__WORDMARK__",encoded((root/"web/art/compas.svg").read_bytes()))
downloads={}
arrangements={}
score_sessions=set()
excerpt_licenses = {
    "cc-by-sa-4.0": ("CC BY-SA 4.0", "https://creativecommons.org/licenses/by-sa/4.0/"),
    "cc-by-sa-3.0": ("CC BY-SA 3.0", "https://creativecommons.org/licenses/by-sa/3.0/"),
    "cc-by-4.0": ("CC BY 4.0", "https://creativecommons.org/licenses/by/4.0/"),
}
licensed_backings={}
for entry in registry:
    catalog_id=entry.get("catalog",entry["id"])
    items=downloads.setdefault(catalog_id,[])
    ledgers={}
    source_entry=catalog[catalog_id]
    rights=source_entry["rights"]["status"]
    if rights in excerpt_licenses and not ({"guide", "right_hand"} & entry.keys()):
        key=(catalog_id,entry["backing"],rights)
        licensed_backings.setdefault(key,[]).append(entry["id"])
        continue
    if rights!="us-public-domain-edition":
        raise ValueError(f"{entry['id']}: no approved edition recorded for score backing")
    if "scan" not in source_entry:
        # Historic excerpts keep their existing download names and labels.
        editions = source_entry.get("historicScoreLeads", [])
        for role in ("backing", "guide", "right_hand"):
            if role not in entry:
                continue
            source = entry[role]
            track = json.loads((root / "repertoire" / source).read_text())
            if not any(track["source"] == edition.get("asset") and track["source_sha256"] == edition.get("sha256")
                       and any(excerpt["ledger"] == source and excerpt["rightsStatus"] == rights
                               for excerpt in edition.get("midiExcerpts", [])) for edition in editions):
                raise ValueError(f"{entry['id']}: score part lacks a matching approved historic excerpt")
        arrangements.setdefault(catalog_id, []).append(entry["id"])
        continue
    for role,label in (("backing","Piano left-hand MIDI"),("guide","Canto guide MIDI"),
                       ("right_hand","Piano right-hand MIDI")):
        if role not in entry:
            continue
        source=Path(entry[role])
        track=json.loads((root/"repertoire"/source).read_text())
        if track["source"]!=source_entry["source"] or track["source_sha256"]!=source_entry["scan"]["sha256"]:
            raise ValueError(f"{entry['id']}: MIDI ledger differs from the catalog's retained edition")
        ledgers[role]=track
        filename=source.with_suffix(".mid").name
        if all(item["filename"] != filename for item in items):
            items.append({"title":label,"filename":filename,"base64":encoded(midi(track))})
    arrangements.setdefault(catalog_id,[]).append(entry["id"])
    score_parts=(catalog_id,entry["backing"],entry.get("guide"),entry.get("right_hand"))
    if "guide" in ledgers and score_parts not in score_sessions:
        score_sessions.add(score_parts)
        right_hand=ledgers.get("right_hand")
        session=rehearsal_midi(source_entry["title"],ledgers["backing"],ledgers["guide"],right_hand)
        label="Rehearsal MIDI · both hands, guide and form markers" if right_hand else "Rehearsal MIDI · left hand, guide and form markers"
        items.append({"title":label,
                      "filename":entry["id"]+"-rehearsal.mid","base64":encoded(session)})
for song in catalog.values():
    for edition in song.get("historicScoreLeads", []):
        excerpts = list(edition.get("midiExcerpts", []))
        if "midiExcerpt" in edition:
            excerpts.insert(0, edition["midiExcerpt"])
        for excerpt in excerpts:
            rights = excerpt["rightsStatus"]
            if rights != "us-public-domain-edition" and rights not in excerpt_licenses:
                raise ValueError(f"{song['id']}: no approved rights basis recorded for MIDI excerpt")
            source = Path(excerpt["ledger"])
            score = json.loads((root / "repertoire" / source).read_text())
            if score["source"] != edition["asset"] or score["source_sha256"] != edition["sha256"]:
                raise ValueError(f"{song['id']}: MIDI excerpt differs from the catalog's retained edition")
            guide = (json.loads((root / "repertoire" / excerpt["guideLedger"]).read_text())
                     if "guideLedger" in excerpt else None)
            right = (json.loads((root / "repertoire" / excerpt["rightHandLedger"]).read_text())
                     if "rightHandLedger" in excerpt else None)
            extra_source = excerpt.get("extraLedger")
            if extra_source:
                if not any(item["ledger"] == extra_source and item["rightsStatus"] == rights
                           for item in excerpts):
                    raise ValueError(f"{song['id']}: extra MIDI part lacks an approved edition")
                extra = json.loads((root / "repertoire" / extra_source).read_text())
                data = rehearsal_midi(song["title"], score, guide, right, extra=extra)
                filename = Path(extra_source).stem + "-rehearsal.mid"
            elif guide is not None or right is not None:
                data = rehearsal_midi(song["title"], score, guide, right)
                filename = source.stem + ("-piano-rehearsal.mid" if right else "-rehearsal.mid")
            else:
                data = midi(score)
                filename = source.with_suffix(".mid").name
            item = {"title": excerpt["label"], "filename": filename,
                    "base64": encoded(data)}
            if rights in excerpt_licenses:
                license_name, license_url = excerpt_licenses[rights]
                credit = score.get("attribution", {})
                if (not credit.get("author") or not credit.get("changes") or
                        credit.get("source_page") != edition["source"] or
                        credit.get("license") != license_name or
                        credit.get("license_url") != license_url or
                        "parts" not in score):
                    raise ValueError(f"{song['id']}: incomplete attribution for {rights}")
                item["attribution"] = credit
                if guide is None and right is None and not extra_source:
                    approved=licensed_backings.pop((song["id"],excerpt["ledger"],rights),[])
                    if approved:
                        arrangements.setdefault(song["id"],[]).extend(approved)
            items = downloads.setdefault(song["id"], [])
            same = next((v for v in items if v["filename"] == item["filename"]), None)
            if same is None:
                items.append(item)
            elif same["base64"] != item["base64"]:
                raise ValueError(f"{song['id']}: conflicting MIDI download filename")
if licensed_backings:
    raise ValueError(f"Licensed score backing lacks a matching attributed ledger: {tuple(licensed_backings)}")
page=page.replace("__SCORE_ARRANGEMENTS__",json.dumps(arrangements).replace("<","\\u003c"))
page=page.replace("__SCORE_DOWNLOADS__",json.dumps(downloads).replace("<","\\u003c"))
(out/"Compas-Set-Builder.html").write_text(page)
