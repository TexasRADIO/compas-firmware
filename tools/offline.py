"""Build one offline listening/playing page from the tested web sources."""
from pathlib import Path
import base64

root = Path(__file__).resolve().parents[1]
web = root / "web"
panel = (web / "panel.mjs").read_text()
store = (web / "store.mjs").read_text().replace("export function", "function")
processor = base64.b64encode((web / "processor.mjs").read_bytes()).decode()
score = (root / "src/score.json").read_text()
wasm = base64.b64encode((root / "build/engine.wasm").read_bytes()).decode()
panel = panel.split("\n", 1)[1]
lines = panel.splitlines()
for i, line in enumerate(lines):
    if line.startswith("const score=await fetch"):
        lines[i] = "const score=" + score + ";"
    elif line.startswith("const wasm=fetch"):
        lines[i] = f"const wasm=WebAssembly.compile(Uint8Array.from(atob('{wasm}'),c=>c.charCodeAt(0))).catch(e=>e);"
    elif "await ctx.audioWorklet.addModule('processor.mjs');" in line:
        lines[i] = f"    await ctx.audioWorklet.addModule('data:text/javascript;base64,{processor}');"
script = store + "\n" + "\n".join(lines)
assert "</script" not in script and "fetch(" not in script and "atob(" in script
page = (web / "index.html").read_text()
for asset, mime in (("johnny-canales.png", "image/png"),
                    ("compas.svg", "image/svg+xml"), ("rhythm-section.svg", "image/svg+xml")):
    encoded = base64.b64encode((web / "art" / asset).read_bytes()).decode()
    page = page.replace(f'src="art/{asset}"', f'src="data:{mime};base64,{encoded}"')
page = page.replace('<link rel="stylesheet" href="panel.css">', '<style>\n'+(web/"panel.css").read_text()+'\n</style>')
page = page.replace('<script type="module" src="panel.mjs"></script>', '<script type="module">\n'+script+'\n</script>')
(root / "build/Compas-FM1.html").write_text(page)
