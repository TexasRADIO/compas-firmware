"""Validate and compile the Compas release score data. Python 3.10+, no packages."""
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
score = json.loads((root / "src/score.json").read_text())
studies = score["studies"]
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
    return "{" + ",".join(map(str, values)) + "}"

rows = []
for s in studies:
    assert s["meter"] in (2, 4) and 60 <= s["bpm"] <= 180 and s["refs"]
    steps = s["meter"] * 8
    names = [p[0] for p in s["parts"]]
    assert len(names) == len(set(names)) == 6 and set(s["break"]) <= set(names)
    mask = sum(1 << i for i, name in enumerate(names) if name in s["break"])
    roles = []
    for _, voice, *variants in s["parts"]:
        assert len(variants) == 3
        roles.append("{" + ",".join([str(sounds.index(voice)), *[phrase(v, steps, voice, i)
                                                              for i, v in enumerate(variants)]]) + "}")
    rows.append("{" + f"{steps},{s['bpm']},{mask}," + "{" + ",\n".join(roles) + "}}")
out = root / "build"
out.mkdir(exist_ok=True)
(out / "score.inc").write_text("/* Generated from score.json. */\n" +
    "static const Study library[] = {\n" + ",\n".join(rows) + "\n};\n")

def ctext(s):
    text = s.replace("—","NO PART").replace("ö","o").upper()
    return json.dumps(text.translate(str.maketrans("ÁÉÍÓÚÑÜ","aeiounv")))

# GLO family tags come from the registry: the conductor family, with the few
# studies whose dance family differs from their conductor named explicitly.
FAMILY_BY_ID = {"tejano": "Tejano", "son": "Son", "conjunto": "Conjunto", "bolero": "Bolero",
                "bolero-trova": "Bolero", "tribal": "Tribal", "merenhouse": "Merengue",
                "jarocho-bamba": "Jarocho"}
def family(s):
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
(out / "panel-score.inc").write_text(
    "static const char *const studies[] = {" + ",".join(ctext(s["short"]) for s in studies) + "};\n"
    "static const char *const names[][6] = {" +
    ",".join("{" + ",".join(ctext(row[0]) for row in s["parts"]) + "}" for s in studies) + "};\n"
    "static const uint8_t meter[] = {" + ",".join(str(s["meter"]) for s in studies) + "};\n"
    "static const char *const conductor_forms[][6][2] = {" + ",".join(form_rows) + "};\n"
    "static const uint8_t study_form[] = {" + ",".join(
        str(form_index[s.get("conductor","default")]) for s in studies) + "};\n"
    "static const uint8_t study_bass[] = {" + ",".join(
        str(int(s["parts"][5][1] == "bass")) for s in studies) + "};\n" +
    "static const char *const study_family[] = {" + ",".join(ctext(family(s)) for s in studies) + "};\n" +
    "static const uint8_t study_flags[] = {" + ",".join(str(study_flags(s)) for s in studies) + "};\n")

# Generate the board routing alongside the rhythm ledger: appended studies cannot
# index stale hand-maintained 727/overlay tables. Values are flash-only.
for s in studies:
    b=s["board"]
    assert len(b["kit"])==6 and all(type(n) is int and 0<=n<=15 for n in b["kit"])
    assert type(b["overlayFamily"]) is int and 0<=b["overlayFamily"]<4
    assert type(b["assignMask"]) is int and 0<=b["assignMask"]<64
    assert not b["kit"][5] and not b["assignMask"] & 32
(out / "board-grooves.inc").write_text(
    "/* Generated board routes; study indices are append-only for saved sets. */\n" +
    "static const uint8_t groove_kit[][6]={" +
    ",".join("{"+",".join(map(str,s["board"]["kit"]))+"}" for s in studies)+"};\n"+
    "static const uint8_t groove_overlay[]={"+
    ",".join(str(s["board"]["overlayFamily"]) for s in studies)+"};\n"+
    "static const uint8_t groove_assign[]={"+
    ",".join(str(s["board"]["assignMask"]) for s in studies)+"};\n")

# Source-labelled conga excerpts: retain native events; repeat one printed bar.
congas=json.loads((root/'repertoire/cr8000-conga-excerpts.json').read_text())
assert congas['schema']==1 and len(congas['patterns'])==8
assert congas['adaptation']['mapping']=={'HC':2,'MC':3,'LC':4}
rhythm_rows=[]
def rhythm_row(title,hits,accents):
    assert 0<len(title)<=18 and len(hits)==len(accents)==15
    for a,b in ((0,1),(2,3),(7,8),(9,10),(11,12)):
        assert not hits[a]&hits[b], '727 exclusive pair collision'
    assert all(not (accent & ~hit) for hit,accent in zip(hits,accents))
    return '{'+ctext(title)+', {'+','.join(str(x)+'u' for x in hits)+'}, {'+','.join(str(x)+'u' for x in accents)+'}}'
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
    file.write('static const struct { const char *title; uint32_t hits[15], accents[15]; } rhythm_bank[]={\n'+',\n'.join(rhythm_rows)+'\n};\n')

with (out/'board-grooves.inc').open('a') as file:
    file.write('static const uint8_t groove_steps[]={'+','.join(str(s['meter']*8) for s in studies)+'};\n')

with (out/"board-grooves.inc").open("a") as file:
    file.write("static const char *const groove_titles[]={"+",".join(ctext(s["short"]) for s in studies)+"};\n")
