"""Parse the actual inline modules in generated pages, without executing them."""
from html.parser import HTMLParser
from pathlib import Path
import subprocess
import sys


class Modules(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.modules, self.current = [], None

    def handle_starttag(self, tag, attrs):
        if tag == "script" and dict(attrs).get("type") == "module":
            if dict(attrs).get("src"):
                raise ValueError("standalone review must embed its module")
            self.current = []

    def handle_data(self, data):
        if self.current is not None:
            self.current.append(data)

    def handle_endtag(self, tag):
        if tag == "script" and self.current is not None:
            self.modules.append("".join(self.current))
            self.current = None


def check(path):
    parser = Modules()
    parser.feed(Path(path).read_text())
    if not parser.modules or parser.current is not None:
        raise ValueError(f"{path}: missing or unfinished inline module")
    for source in parser.modules:
        result = subprocess.run(["node", "--input-type=module", "--check"],
                                input=source, text=True, capture_output=True)
        if result.returncode:
            raise ValueError(f"{path}: inline module does not parse\n{result.stderr}")
    return len(parser.modules)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: check-browser-pages.py PAGE.html [...]")
    count = sum(check(path) for path in sys.argv[1:])
    print(f"Browser page modules parse: {count} in {len(sys.argv)-1} standalone pages.")
