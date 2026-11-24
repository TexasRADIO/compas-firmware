"""Freeze generated native inputs before Docker reads them; never use live generators."""
from hashlib import sha256
import json
from pathlib import Path
import tempfile
import argparse

def freeze(root, *, compact=False, hanan=False):
    root=Path(root)
    files={name:root/'build'/name for name in ('score.inc','score-test.inc','panel-score.inc','board-grooves.inc')}
    files.update({name:root/'build/board'/name for name in ('727.pack','727.inc','piano.inc','set.inc')})
    if hanan:files['hanan-data.inc']=root/'build/board/hanan-data.inc'
    data={name:path.read_bytes() for name,path in files.items()}
    if compact and (b'static const uint32_t piano_increments[]' not in data['piano.inc'] or
                    b'static const uint32_t increments_' in data['piano.inc']):
        raise ValueError('USB native build requires the compact score input; a concurrent generator replaced it')
    target=Path(tempfile.mkdtemp(prefix='native-inputs-',dir=root/'build/board'))
    for name,content in data.items():(target/name).write_bytes(content)
    manifest={'compactScores':compact,'hanan':hanan,'files':{
        name:{'bytes':len(content),'sha256':sha256(content).hexdigest()} for name,content in data.items()}}
    (target/'INPUTS.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return target.resolve()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compact',action='store_true');parser.add_argument('--hanan',action='store_true')
    args=parser.parse_args()
    print(freeze(Path(__file__).resolve().parents[1],compact=args.compact,hanan=args.hanan))
