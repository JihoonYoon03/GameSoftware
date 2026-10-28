"""Immutable recovery copy before the all-part budget migration."""
import json,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'Source/Archives/Pre200Delivery_20261026.zip';out.parent.mkdir(parents=True,exist_ok=True)
if out.exists():
    print('PRE200_SNAPSHOT_EXISTS',out)
else:
    paths=list((ROOT/'Export').rglob('*'))+list((ROOT/'Documentation').glob('*'))+list((ROOT/'Source').glob('*.blend'))
    paths+=[p for p in (ROOT/'Validation').glob('*.json') if not p.name.startswith(('size_budget','part_budget'))]
    stage=out.with_suffix('.zip.new')
    with zipfile.ZipFile(stage,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=4) as archive:
        for p in paths:
            if p.is_file():archive.write(p,str(p.relative_to(ROOT)))
    stage.replace(out)
    print('PRE200_SNAPSHOT_SAVED',out,out.stat().st_size)
