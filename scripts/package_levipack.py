import argparse, json, zipfile
from pathlib import Path

def write_package(library: Path, icon: Path, output: Path, version_header: Path):
    text = version_header.read_text(encoding='utf-8')
    vals = {}
    for key in ('Name','Author','Description','Version'):
        marker = f'std::string_view {key} = "'
        pos = text.find(marker)
        if pos < 0: raise RuntimeError(f'missing {key}')
        start = pos + len(marker)
        end = text.find('"', start)
        vals[key] = text[start:end]
    manifest = {
        'type':'preload-native', 'name':vals['Name'], 'author':vals['Author'],
        'description':vals['Description'], 'version':vals['Version'],
        'entry':'libBetterThirdPerson.so', 'icon':'icon.png',
        'overwrite_files':['icon.png'], 'overwrite_folders':[]
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists(): output.unlink()
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        z.writestr('manifest.json', json.dumps(manifest, indent=2)+'\n')
        z.write(library,'libBetterThirdPerson.so')
        z.write(icon,'icon.png')
    with zipfile.ZipFile(output) as z:
        assert set(z.namelist()) == {'manifest.json','libBetterThirdPerson.so','icon.png'}
        assert json.loads(z.read('manifest.json')) == manifest

def main():
    p=argparse.ArgumentParser(); p.add_argument('--library',type=Path,required=True); p.add_argument('--icon',type=Path,required=True); p.add_argument('--version-header',type=Path,required=True); p.add_argument('--output',type=Path,required=True)
    a=p.parse_args(); write_package(a.library,a.icon,a.output,a.version_header); print(a.output.resolve())
if __name__=='__main__': main()
