"""The same board study and score registry for both offline browser surfaces."""
import json


def panel_catalog(root):
    score = json.loads((root / "src/score.json").read_text())
    studies = score["studies"]
    forms = score["conductorForms"]
    for study in studies:
        study["conductorParts"] = forms[study.get("conductor", "default")]
    scores = json.loads((root / "repertoire/board-scores.json").read_text())
    for piano, entry in enumerate(scores, 1):
        study = {"id": entry["id"], "title": entry["title"], "bpm": entry["bpm"],
                 "piano": piano, "guide": "guide" in entry or "guide_part" in entry}
        backing = json.loads((root / "repertoire" / entry["backing"]).read_text())
        if backing["meter"] == [6, 8]:
            study["compoundPulse"] = True
        studies.append(study)
    return studies
