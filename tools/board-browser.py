"""Build the standalone Sonidero browser instrument using the real local bank."""
import base64
import json
import os
from pathlib import Path
from runpy import run_path
import subprocess
import sys
from set_catalog import panel_catalog

root = Path(__file__).resolve().parents[1]
os.chdir(root)
clang = sys.argv[1]
folder = root/"build/board"
for script, args in (("score.py",[]),("board-bank.py",[os.environ["COMP_STUDIO"]]),
                     ("board-piano.py",[]),("board-set.py",[])):
    subprocess.run([sys.executable,"tools/"+script,*args],check=True)
hanan = os.environ.get("COMP_HANAN")
if hanan:
    subprocess.run([sys.executable, "tools/board-hanan.py", hanan], check=True)
bank = (folder/"727.pack").read_bytes()
(folder/"browser-bank.c").write_text("const unsigned char compas_packed727[]={\n"+
    "\n".join(",".join(map(str,bank[i:i+32]))+"," for i in range(0,len(bank),32))+"\n};\n")
wasm = folder/"browser-board.wasm"
exports = ("encoders boot song_title cue input master audio snapshot snapshot_size read_view splash draw "
           "set_buffer set_size playing pull_saved_set update_set_display rhythm_buffer rhythm_size rhythm_export rhythm_import rhythm_slots_buffer rhythm_slots_size show_export lang rails fx_shed").split()
if hanan:
    exports.append("kit_restore")
subprocess.run([clang,"--target=wasm32","-fuse-ld=lld","-Oz","-std=c11","-Wall","-Wextra","-Werror",
                "-Wno-unused-function","-ffreestanding","-fno-builtin","-nostdlib","-DCOMPAS_BOARD","-DCOMPAS_EMU_STORE",
                *(["-DCOMPAS_HANAN"] if hanan else []),
                "-Iboard","-Isrc","-Ibuild","-Ibuild/board","-Wl,--no-entry","-Wl,--strip-all",
                "-Wl,-z,stack-size=8192",*("-Wl,--export=bench_"+name for name in exports),
                "web/board-bench.c","src/engine.c","board/board.c","board/panel.c","board/piano.c",
                str(folder/"browser-bank.c"),"-o",str(wasm)],check=True)
encoded = lambda path: base64.b64encode(path.read_bytes()).decode()
audio_renderer = (root/"web/board-audio.mjs").read_text()
worklet = folder.parent/"board-worklet.mjs"
worklet.write_text(audio_renderer + (root/"web/board-worklet.mjs").read_text())
script = ("const module=await WebAssembly.compile(Uint8Array.from(atob('"+encoded(wasm)+
          "'),c=>c.charCodeAt(0)));\nconst embeddedWorkletURL='data:text/javascript;base64,"+
          encoded(worklet)+"';\n"+
          audio_renderer + (root/"web/board-bench.mjs").read_text())
validator = (root/"web/set-format.mjs").read_text().replace("export {normalizeSet};", "")
packer = (root/"web/set-payload.mjs").read_text().split("\n", 1)[1].replace("export {preparedPayload};", "")
catalog = json.dumps(panel_catalog(root), ensure_ascii=False).replace("<", "\\u003c")
show = (root/"web/show-format.mjs").read_text().replace("export {showPayload, serializeShow, showSongSlots, showSongTitle, reorderShow};", "")
key_ui = (root/"web/key-presentation.mjs").read_text().replace("export {keyPresentation,computerKeys,whiteKeys};", "")
script = key_ui + "\n" + show + "\n" + "const setCatalog=" + catalog + ";\n" + validator + "\n" + packer + "\n" + script
# All entry points share one compiled engine and the same panel controls.
# The focused edition boots the checked-in set through the ordinary importer;
# it never rewrites the sixteen-slot factory order or an existing saved show.
profiles = [
    ("factory", "Factory set", "board/set.json", "Compas-Sonidero-Board.html"),
    ("tribute", "Johnny Canales tribute · solo rehearsal", "repertoire/johnny-canales-rehearsal-set.json", "Compas-Johnny-Canales.html"),
    ("tejano-rhythm", "Tejano rhythm section · kit, hand percussion and bass", "repertoire/tejano-rhythm-section-rehearsal.json", None),
    ("san-antonio", "San Antonio · rhythm comparison", "repertoire/san-antonio-rhythm-rehearsal.json", None),
    ("regional", "Regional cumbia · ten original studies", "repertoire/regional-audition-set.json", "Compas-Regional-Cumbia.html"),
    ("colombian", "Colombian cumbia · traditional percussion", "repertoire/colombian-cumbia-audition-set.json", None),
    ("merenhouse", "Merenhouse · New York rhythm study", "repertoire/merenhouse-rehearsal-set.json", None),
    ("jarocho-bamba", "La Bamba · Veracruz accompaniment", "repertoire/jarocho-bamba-rehearsal-set.json", None),
    ("merengue", "Merengue · Derecho, Maco and Juana Quilina", "repertoire/merengue-audition-set.json", "Compas-Merengue.html"),
    ("valentina", "La Valentina · Apke guitar edition", "repertoire/la-valentina-rehearsal-set.json", None),
    ("adelita", "La Adelita · Kemp quartet rehearsal", "repertoire/adelita-rehearsal-set.json", "Compas-La-Adelita.html"),
    ("siboney", "Siboney · 1929 Feist voice and piano", "repertoire/siboney-rehearsal-set.json", "Compas-Siboney.html"),
    ("zacatecas", "Zacatecas March · 1895 Levy piano print", "repertoire/zacatecas-rehearsal-set.json", "Compas-Zacatecas.html"),
    ("la-mora", "La Mora · 1918 Havana piano excerpts", "repertoire/la-mora-rehearsal-set.json", "Compas-La-Mora.html"),
    ("la-mora-finale", "La Mora · singer to finale", "repertoire/la-mora-singer-finale-set.json", None),
    ("cielito", "Cielito Lindo · printed piano rehearsal", "repertoire/cielito-rehearsal-set.json", "Compas-Cielito-Lindo.html"),
    ("cucaracha", "La Cucaracha · printed piano rehearsal", "repertoire/cucaracha-rehearsal-set.json", "Compas-La-Cucaracha.html"),
    ("jurame", "Júrame · Grever’s Schirmer song", "repertoire/jurame-song-rehearsal-set.json", None),
    ("bolero-trova", "Cuban bolero · trova and canción", "repertoire/bolero-trova-rehearsal.json", None),
    ("golondrina", "La Golondrina · piano, bolero and singer’s key", "repertoire/golondrina-rehearsal-set.json", None),
    ("ma-teodora", "Má Teodora · 1929 compound-meter rehearsal", "repertoire/ma-teodora-rehearsal-set.json", None),
    ("sobre-las-olas", "Sobre las olas · 1895 first strain to Fine", "repertoire/sobre-las-olas-rehearsal-set.json", None),
]
rehearsals = {key: {"name": name, "set": json.loads((root/path).read_text())}
              for key, name, path, _ in profiles}
compile_set = run_path(str(root / "tools/board-set.py"))["compile_set"]
for key, _, path, _ in profiles:
    try:
        compile_set(rehearsals[key]["set"])
    except ValueError as error:
        raise SystemExit(f"{path}: {error}") from error
for key, name, filename, command in (
    ("colombian-727", "Colombian cumbia · 727 percussion show", "Colombian-Cumbia-727.cpshow",
     ["tools/colombian-727-show.mjs"]),
    ("son-salsa", "Son and salsa · 727 clave comparison", "Son-Salsa-Source-Show.cpshow",
     ["tools/son-salsa-audition.mjs", "--show-only"]),
):
    prepared_show = subprocess.check_output(["node", *command])
    (root/"build"/filename).write_bytes(prepared_show)
    rehearsals[key] = {"name": name, "set": json.loads(prepared_show)}
script = "const rehearsalSets="+json.dumps(rehearsals,ensure_ascii=False).replace("<","\\u003c")+";\n"+script
for key, name, _, filename in profiles:
    if filename is None:
        continue
    initial = None if key == "factory" else rehearsals[key]["set"]
    startup = "const startupSet="+json.dumps(initial,ensure_ascii=False).replace("<","\\u003c")+";\n"
    startup += "const startupName="+json.dumps(name,ensure_ascii=False)+";\nconst startupHanan=false;\n"
    page = (root/"web/board-bench.html").read_text().replace("/* COMPAS_BOARD_SCRIPT */",startup+script)
    if initial:
        heading = "Compás · "+name.split(" · ",1)[0]
        page = page.replace("<title>Compás · Sonidero board bench</title>","<title>"+heading+"</title>")
        page = page.replace('<h1>Compás · Sonidero</h1>', '<h1>'+heading+'</h1>')
    if hanan:
        credit = ('<p>The four Kumbia Kit sounds and patterns come from '
                  '<a href="https://www.oficinadesonido.org/wp-content/uploads/2026/07/Hanan-Cumbia.pdf">Hanan Cumbia by Oficina de Sonido</a> '
                  '(hanan26Taller, 2026-07-21), based on Bleep Labs’ Bleep Drum. '
                  'The factory data retain their <a href="https://creativecommons.org/licenses/by-sa/3.0/">CC BY-SA 3.0</a> licence.</p>\n')
        page = page.replace('<fieldset><legend>Your 727 rhythms</legend>', credit + '<fieldset><legend>Your 727 rhythms</legend>')
    (root/"build"/filename).write_text(page)
    if hanan and key == "factory":
        standalone = page.replace("<title>Compás · Sonidero board bench</title>", "<title>Compás · Kumbia Kit</title>")
        standalone = standalone.replace("<h1>Compás · Sonidero</h1>", "<h1>Compás · Kumbia Kit</h1>")
        standalone = standalone.replace('const startupName="Factory set";', 'const startupName="Kumbia Kit · Factory Hanan sounds";')
        (root/"build"/"Compas-Kumbia.html").write_text(standalone.replace("startupHanan=false", "startupHanan=true"))

(root/"build"/"Start Compas.html").write_text((root/"web/rehearsal-hub.html").read_text())
