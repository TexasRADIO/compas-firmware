"""Validate and compile the private score ledger. Python 3.10+, no packages."""
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
score = json.loads((root / "src/score.json").read_text())
studies = score["studies"]
public_studies = [s for s in studies if not s.get("testOnly")]
sounds = "none low skin hand shake scrape kick bell stick bass shell snare pluck".split()
assert score["revision"] == 1 and len(studies) == len({s["id"] for s in studies}) <= 255

def phrase(text, steps, voice, phase):
    values = [0] * steps
    for token in text.split():
        m = re.fullmatch(r"(\d+)(!|:[0-9]+)?", token)
        assert m, token
        idx, annotation = int(m[1]), m[2] or ""
        assert idx < steps and values[idx] == 0 and voice != "none", token
        if phase == 2:
            assert idx >= steps - 4 and voice != "bass", token
        if voice == "bass":
            assert annotation.startswith(":"), token
            note = int(annotation[1:])
            assert 0 <= note <= 11, token
            values[idx] = 16 + note
        else:
            assert annotation in ("", "!"), token
            values[idx] = 2 if annotation else 1
    return values

def meter_spec(study):
    value = study["meter"]
    numerator, denominator = (value, 4) if type(value) is int else value
    assert (numerator, denominator) in ((2, 4), (4, 4), (6, 8), (12, 8))
    # One Compás phrase holds two written bars.  Its existing 4/4 grid is
    # therefore eight quarter-note pulses across 32 steps; one written
    # denominator note occupies 16 / denominator steps.
    steps_per_note = 16 // denominator
    steps = numerator * 32 // denominator
    # A normal compound phrase has equal dotted-quarter groups.  A future
    # reviewed sesquiáltera study can instead state its two written bars in
    # denominator-note groups, e.g. [[3,3],[2,2,2]] for 6/8 then 3/4.  This
    # changes visible pulses/count-in only: the underlying sixteenth clock is
    # deliberately constant across both bars.
    groups = study.get("pulseGroups")
    if groups is None:
        # Groups are expressed in written denominator-note units.  A 4/4
        # bar therefore has four one-quarter-note groups, which become four
        # four-sixteenth-step pulses below.
        groups = [[1] * numerator for _ in range(2)] if denominator == 4 else [[3] * (numerator // 3) for _ in range(2)]
    assert type(groups) is list and len(groups) == 2
    assert all(type(bar) is list and bar and sum(bar) == numerator and
               all(type(group) is int and group > 0 for group in bar) for bar in groups)
    pulse_widths = [group * steps_per_note for bar in groups for group in bar]
    assert 0 < len(pulse_widths) <= 8 and sum(pulse_widths) == steps
    return (numerator, denominator, steps, pulse_widths,
            4 if denominator == 4 else 6)

# Pattern rows are pooled: most rows are silent, many repeat between studies,
# and a sounding row is mostly rests. Row 0 is silence. A stored row is a
# step mask (LSB first) followed by one value per set bit.
rows, pool, pool_test, row_ids = [], [], [], {}
def row_id(row, test):
    key = tuple(row)
    if not any(key): return 0
    if key not in row_ids:
        (pool_test if test else pool).append(key)
        row_ids[key] = None
    return key
study_rows = []
for index, s in enumerate(studies):
    _, _, steps, pulse_widths, steps_per_pulse = meter_spec(s)
    assert steps <= 48 and 60 <= s["bpm"] <= 180 and s["refs"]
    names = [p[0] for p in s["parts"]]
    assert len(names) == len(set(names)) == 6 and set(s["break"]) <= set(names)
    mask = sum(1 << i for i, name in enumerate(names) if name in s["break"])
    sounds_row, keys = [], []
    for _, voice, *variants in s["parts"]:
        assert len(variants) == 3
        sounds_row.append(str(sounds.index(voice)))
        for phase, variant in enumerate(variants):
            keys.append(row_id(phrase(variant, steps, voice, phase), s.get("testOnly")))
    study_rows.append((s, f"{steps},{s['bpm']},{mask},{len(pulse_widths)},{steps_per_pulse},"+"{"+",".join(sounds_row)+"}", keys))
# Production rows come first so their ids never depend on the test fixtures.
for number, key in enumerate(pool + pool_test, 1): row_ids[key] = number
assert len(row_ids) < 256
def packed(key):
    mask = [sum((key[i+j] != 0) << j for j in range(min(8, len(key)-i))) for i in range(0, len(key), 8)]
    return mask + [value for value in key if value]
def pool_text(keys, start):
    data, at = [], []
    for key in keys:
        at.append(start + len(data)); data.extend(packed(key))
    return data, at
data, at = pool_text(pool, 0)
test_data, test_at = pool_text(pool_test, len(data))
assert len(data) + len(test_data) < 65536
for s, head, keys in study_rows:
    row = "{"+head+",{"+",".join(str(row_ids[key] if key else 0) for key in keys)+"}}"
    rows.append("#ifdef COMPAS_COMPOUND_TEST\n"+row+",\n#endif" if s.get("testOnly") else row+",")
def guarded(values):
    return "\n#ifdef COMPAS_COMPOUND_TEST\n,"+",".join(map(str, values))+"\n#endif\n" if values else ""
out = root / "build"
out.mkdir(exist_ok=True)
(out / "score.inc").write_text("/* Generated from score.json. */\n" +
    "static const uint8_t study_rows[]={"+",".join(map(str, data))+guarded(test_data)+"};\n" +
    "static const uint16_t study_row_at[]={"+",".join(map(str, at))+guarded(test_at)+"};\n" +
    "static const Study library[] = {\n" + "\n".join(rows) + "\n};\n")
(out / "score-test.inc").write_text("/* Generated test-only study IDs. */\n" +
    "\n".join("#define CP_TEST_STUDY_"+s["id"].replace("-", "_").upper()+" "+str(i)
              for i,s in enumerate(studies) if s.get("testOnly")) + "\n")

def ctext(s):
    text = s.replace("—","NO PART").replace("ö","o").upper()
    return json.dumps(text.translate(str.maketrans("ÁÉÍÓÚÑÜ","aeiounv")))

# GLO family tags come from the registry: the conductor family, with the few
# studies whose dance family differs from their conductor named explicitly.
FAMILY_BY_ID = {"tejano": "Tejano", "son": "Son", "conjunto": "Conjunto", "bolero": "Bolero",
                "bolero-trova": "Bolero", "tribal": "Tribal", "merenhouse": "Merengue",
                "jarocho-bamba": "Jarocho", "carimbo-marapanim": "Carimbo",
                "tecnobrega-belem": "Tecno", "lambada-para": "Lambada",
                "guitarrada-para": "Guitarra", "chilena-costa-chica-study": "Chilena",
                "bembe-pusztai-12-8-study": "Bembe"}
def family(s):
    if s.get("testOnly"):
        return "TEST"
    name = FAMILY_BY_ID.get(s["id"]) or {"cumbia": "Cumbia", "salsa": "Salsa",
                                         "merengue": "Merengue"}.get(s.get("conductor"))
    assert name and len(name) <= 8, s["id"]
    return name
# Bit 0: the rhythm body is source-backed (documented or traditional onsets).
# Bit 1: the source is written in cut time, so musicians count it in two.
SOURCED = ("traditional", "documented", "source conga", "published notation")
def study_flags(s):
    text = (s.get("evidence", "") + " " + s.get("note", "")).lower()
    sourced = any(word in text for word in SOURCED) and not text.startswith("original compás")
    return int(sourced) | (2 if "cut-time" in text else 0)

forms = score["conductorForms"]
for entries in forms.values():
    assert len(entries)==6 and all(1<=len(name)<=12 and 1<=len(short)<=3 for name,short in entries)
form_rows = [
    "{" + ",".join("{" + ctext(name) + "," + ctext(short) + "}" for name, short in entries) + "}"
    for entries in forms.values()
]
form_index = {name: index for index, name in enumerate(forms)}

# The board's public catalogue deliberately excludes engine-only fixtures.
# Panel tests compile with COMPAS_COMPOUND_TEST, however, so render metadata
# must retain the engine's append-only study IDs in that build.  Keeping the
# conditional entries in-place makes fixture IDs 34 and 35 safe without ever
# exposing them in an ordinary board/browser build.
def panel_rows(render):
    rows = []
    for study in studies:
        row = render(study) + ","
        if study.get("testOnly"):
            rows.extend(["#ifdef COMPAS_COMPOUND_TEST", row, "#endif"])
        else:
            rows.append(row)
    return "\n".join(rows)

(out / "panel-score.inc").write_text(
    "static const char *const studies[] = {\n" + panel_rows(lambda s: ctext(s["short"])) + "\n};\n"
    "static const char *const names[][6] = {\n" +
    panel_rows(lambda s: "{" + ",".join(ctext(row[0]) for row in s["parts"]) + "}") + "\n};\n"
    "static const uint8_t meter_top[] = {\n" + panel_rows(lambda s: str(meter_spec(s)[0])) + "\n};\n"
    "static const uint8_t meter_bottom[] = {\n" + panel_rows(lambda s: str(meter_spec(s)[1])) + "\n};\n"
    "static const uint8_t meter_steps[] = {\n" + panel_rows(lambda s: str(meter_spec(s)[2])) + "\n};\n"
    "static const uint8_t meter_pulses[] = {\n" + panel_rows(lambda s: str(len(meter_spec(s)[3]))) + "\n};\n"
    "static const uint8_t meter_pulse_widths[][8] = {\n" + panel_rows(
        lambda s: "{"+",".join(map(str,meter_spec(s)[3]+[0]*(8-len(meter_spec(s)[3]))))+"}") + "\n};\n"
    "static const char *const conductor_forms[][6][2] = {" + ",".join(form_rows) + "};\n"
    "static const uint8_t study_form[] = {\n" + panel_rows(
        lambda s: str(form_index[s.get("conductor","default")])) + "\n};\n"
    "static const uint8_t study_bass[] = {\n" + panel_rows(
        lambda s: str(int(s["parts"][5][1] == "bass"))) + "\n};\n" +
    "static const char *const study_family[] = {\n" + panel_rows(lambda s: ctext(family(s))) + "\n};\n" +
    "static const uint8_t study_flags[] = {\n" + panel_rows(lambda s: str(study_flags(s))) + "\n};\n")

# Generate the board routing alongside the rhythm ledger: appended studies cannot
# index stale hand-maintained 727/overlay tables. Values are flash-only.
for s in public_studies:
    b=s["board"]
    assert len(b["kit"])==6 and all(type(n) is int and 0<=n<=15 for n in b["kit"])
    assert type(b["overlayFamily"]) is int and 0<=b["overlayFamily"]<4
    assert type(b["assignMask"]) is int and 0<=b["assignMask"]<64
    assert not b["kit"][5] and not b["assignMask"] & 32
(out / "board-grooves.inc").write_text(
    "/* Generated board routes; study indices are append-only for saved sets. */\n" +
    "static const uint8_t groove_kit[][6]={" +
    ",".join("{"+",".join(map(str,s["board"]["kit"]))+"}" for s in public_studies)+"};\n"+
    "static const uint8_t groove_overlay[]={"+
    ",".join(str(s["board"]["overlayFamily"]) for s in public_studies)+"};\n"+
    "static const uint8_t groove_assign[]={"+
    ",".join(str(s["board"]["assignMask"]) for s in public_studies)+"};\n")

# Source-labelled conga excerpts: retain native events; repeat one printed bar.
congas=json.loads((root/'repertoire/cr8000-conga-excerpts.json').read_text())
assert congas['schema']==1 and len(congas['patterns'])==8
assert congas['adaptation']['mapping']=={'HC':2,'MC':3,'LC':4}
rhythm_rows=[]
rhythm_masks=[0]
def rhythm_row(title,hits,accents):
    assert 0<len(title)<=18 and len(hits)==len(accents)==15
    for a,b in ((0,1),(2,3),(7,8),(9,10),(11,12)):
        assert not hits[a]&hits[b], '727 exclusive pair collision'
    assert all(not (accent & ~hit) for hit,accent in zip(hits,accents))
    # Lanes are pooled: most are empty and many repeat. Mask 0 is silence.
    def lane(mask):
        if mask not in rhythm_masks: rhythm_masks.append(mask)
        return str(rhythm_masks.index(mask))
    return '{'+ctext(title)+', {'+','.join(map(lane,hits))+'}, {'+','.join(map(lane,accents))+'}}'
def repeated_bar(steps):
    assert len(steps)==len(set(steps)) and all(type(n) is int and 1<=n<=16 for n in steps)
    bits=sum(1<<(n-1) for n in steps)
    return bits | bits<<16

for item in congas['patterns']:
    assert item['sourceSteps']==16 and item['meter']=='4/4'
    accents=repeated_bar(item['sourceAccent'])
    hits=[repeated_bar(item['sourceVoices'][v]) for v in ('HC','MC','LC')]
    assert not hits[0]&hits[1], '727 high conga choke pair collision'
    rhythm_rows.append(rhythm_row(item['title'],[0,0]+hits+[0]*10,[0,0]+[x&accents for x in hits]+[0]*10))

# Transcribed basic cells retain onset, register and stroke before translating
# to the narrower 727 palette. Only the two-bar rotation/combination is authored.
cells=json.loads((root/'repertoire/afro-cuban-727-cells.json').read_text())
assert cells['schema']==1 and cells['grid']['steps']==32 and cells['grid']['meter']=='4/4'
assert len(cells['presets'])==6
assert len({p['id'] for p in cells['presets']})==6
for item in cells['presets']:
    assert item['rotateSteps'] in (0,16) and item['cells']
    hits=[0]*15; accents=[0]*15
    for name in item['cells']:
        cell=cells['cells'][name]; used=set()
        for step,register,stroke in cell['events']:
            assert type(step) is int and 0<=step<32 and step not in used
            used.add(step)
            voice,accent=cells['adaptation'][cell['family']][register+':'+stroke]
            assert type(voice) is int and 0<=voice<15 and type(accent) is bool
            bit=1<<((step+item['rotateSteps'])%32)
            assert not hits[voice]&bit, 'duplicate combined stroke'
            hits[voice]|=bit
            if accent: accents[voice]|=bit
    rhythm_rows.append(rhythm_row(item['title'],hits,accents))

# Preserve separate Dominican and Cuban bolero source ledgers and append-only IDs.
for filename in ('merengue-727-cells.json','bolero-727-cells.json'):
    ledger=json.loads((root/'repertoire'/filename).read_text())
    assert ledger['schema']==1 and ledger['grid']=={'meter':'4/4','bars':2,'steps':32,'index':'zero-based sixteenths'}
    assert len(ledger['patterns'])==2 and len({p['id'] for p in ledger['patterns']})==2
    for item in ledger['patterns']:
        hits=[0]*15; accents=[0]*15; used=set()
        for step,register,stroke in item['events']:
            assert type(step) is int and 0<=step<32 and step not in used
            used.add(step)
            voice,accent=ledger['adaptation'][register+':'+stroke]
            assert type(voice) is int and voice in (2,3,4) and type(accent) is bool
            hits[voice]|=1<<step
            if accent: accents[voice]|=1<<step
        rhythm_rows.append(rhythm_row(item['title'],hits,accents))
# Roland's four-lane example is a one-bar native 727 grid with no accents.
lesson=json.loads((root/'repertoire/tr727-step-write.json').read_text())
assert lesson['schema']==1 and lesson['sourceSteps']==16 and lesson['meter']=='4/4'
assert lesson['sourceVoices'].keys()==lesson['rackVoices'].keys()
assert lesson['rackVoices']=={'Hi Bongo':0,'Open Hi Conga':3,'Low Conga':4,'Hi Agogo':7}
hits=[0]*15
for voice,steps in lesson['sourceVoices'].items():
    hits[lesson['rackVoices'][voice]]=repeated_bar(steps)
accents=repeated_bar(lesson['sourceAccent'])
rhythm_rows.append(rhythm_row(lesson['title'],hits,[hit&accents for hit in hits]))

# Append these IDs after the Roland manual so earlier saved origins stay valid.
cumbia=json.loads((root/'repertoire/colombian-cumbia-cells.json').read_text())
rack=cumbia['rack727']
shared=cumbia['shared']
voices=rack['voiceMap']
assert len(set(voices.values()))==len(voices) and all(0<=v<15 for v in voices.values())
assert len(cumbia['patterns'])==len(rack['titles'])==3

def cumbia_bar_mask(steps):
    assert len(steps)==len(set(steps)) and all(type(n) is int and 0<=n<8 for n in steps)
    return sum(1<<n for n in steps)

def cumbia_body_mask(odd,even=None):
    first=cumbia_bar_mask(odd)
    second=cumbia_bar_mask(odd if even is None else even)
    bar_pair=first | (second<<8)
    return bar_pair | (bar_pair<<16)

for item,title in zip(cumbia['patterns'],rack['titles']):
    hits=[0]*15; accents=[0]*15
    round_hits=cumbia_body_mask(item['alegreRound'])
    alegre=cumbia_body_mask(item['alegre'])
    assert round_hits & ~alegre == 0
    hits[voices['alegreCross']]=alegre & ~round_hits
    hits[voices['alegreRound']]=round_hits
    hits[voices['llamador']]=cumbia_body_mask(item['llamador'])
    hits[voices['tamboraRim']]=cumbia_body_mask(item['rimOdd'],item['rimEven'])
    hits[voices['tamboraHead']]=cumbia_body_mask([],shared['headEven'])
    hits[voices['maraca']]=cumbia_body_mask(shared['maraca'])
    hits[voices['guache']]=cumbia_body_mask(shared['guacheAttack']) | (1<<shared['initialGuacheAttack'])
    if rack['accents']['alegreRound']:
        accents[voices['alegreRound']]=round_hits
    accents[voices['llamador']]=cumbia_body_mask(rack['accents']['llamador'])
    rhythm_rows.append(rhythm_row(title,hits,accents))

with (out/'board-grooves.inc').open('a') as file:
    assert len(rhythm_masks)<256
    file.write('static const uint32_t rhythm_masks[]={'+','.join(str(x)+'u' for x in rhythm_masks)+'};\n')
    file.write('static const struct { const char *title; uint8_t hits[15], accents[15]; } rhythm_bank[]={\n'+',\n'.join(rhythm_rows)+'\n};\n')

with (out/'board-grooves.inc').open('a') as file:
    file.write('static const uint8_t groove_steps[]={'+','.join(str(meter_spec(s)[2]) for s in public_studies)+'};\n')

with (out/"board-grooves.inc").open("a") as file:
    file.write("static const char *const groove_titles[]={"+",".join(ctext(s["short"]) for s in public_studies)+"};\n")
