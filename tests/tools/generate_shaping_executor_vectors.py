"""Generate pinned-font executor oracle vectors, independent HarfBuzz result columns."""
from pathlib import Path
import hashlib
import json
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import noto_shaping_closure_prototype as closure
from fontTools.ttLib import TTFont
import fontTools
import uharfbuzz as hb
if fontTools.__version__!='4.60.1' or hb.__version__!='0.57.0': raise RuntimeError('pinned dependencies required')
font_dir=Path(sys.argv[1]); output=Path(sys.argv[2]); output.mkdir(parents=True,exist_ok=True)
report={'fontTools':fontTools.__version__,'uharfbuzz':hb.__version__,'cases':[],'advances':[]}
for face,digest in closure.FACES.items():
 path=font_dir/f'NotoSansSC-{face}.otf'; data=path.read_bytes()
 if hashlib.sha256(data).hexdigest()!=digest: raise RuntimeError('source mismatch')
 with TTFont(path) as font:
  # Inspect original source formats, never infer them from sparse encoding.
  selected_ids={lookup_id for script,language in closure.PROFILE_SCRIPTS for _,lookup_id in closure.selected_lookup_plan(font,'GPOS',script,language,('kern','mark'))}
  for lookup_id in selected_ids:
   lookup=font['GPOS'].table.LookupList.Lookup[lookup_id]
   if lookup.LookupType == 2:
    for table in lookup.SubTable:
     if table.ValueFormat2 != 0: raise RuntimeError('original source VF2 unsupported')
  cmap=font.getBestCmap()
  metrics=[]
  for gid,name in enumerate(font.getGlyphOrder()): metrics.append(font['hmtx'].metrics[name][0])
  used_gids=set()
  for name,text,script,language in closure.CASES:
   normalized=closure._canonical_scalars(text,cmap)
   clusters=sorted(set(cluster for _,cluster in normalized))+[len(text)]
   ends={clusters[i]:clusters[i+1] for i in range(len(clusters)-1)}
   records=[]
   for scalar,cluster in normalized:
    scalar=ord(scalar)
    if scalar not in cmap: raise RuntimeError('unmapped normalized input')
    records.append((font.getGlyphID(cmap[scalar]),len(text[:cluster].encode()),len(text[:ends[cluster]].encode())))
   expected=closure.harfbuzz_shape(data,text,script,language,closure.PROFILE_FEATURES)['glyphs']
   used_gids.update(row['gid'] for row in expected)
   expected_clusters=sorted(set(row['cluster'] for row in expected))+[len(text)]
   expected_ends={expected_clusters[i]:expected_clusters[i+1] for i in range(len(expected_clusters)-1)}
   selection=list(closure.PROFILE_SCRIPTS).index((script.lower(),language))
   lines=[f'{selection} {len(records)} {len(expected)}']
   lines += [' '.join(map(str,row)) for row in records]
   lines += [f"{row['gid']} {row['xAdvance']} {row['yAdvance']} {row['xOffset']} {row['yOffset']} {len(text[:row['cluster']].encode())} {len(text[:expected_ends[row['cluster']]].encode())}" for row in expected]
   target=output/f'{face.lower()}-{name}.txt'; target.write_text('\n'.join(lines)+'\n')
   report['cases'].append({'file':target.name,'sourceSha256':digest,'input':text,'normalization':'offline canonical scalars from closure prototype','oracle':'HarfBuzz pinned source original units','sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
  target=output/f'{face.lower()}-advances.txt'
  target.write_text('\n'.join(f'{gid} {metrics[gid]}' for gid in sorted(used_gids))+'\n')
  report['advances'].append({'file':target.name,'sourceSha256':digest,'records':len(used_gids),'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
(output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
