"""Initialize dependency gitlinks for a clone or source ZIP."""
import json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def git(*args):
    subprocess.run(['git','-C',str(root),*args],check=True)
if sys.version_info<(3,10):raise SystemExit('Python 3.10+ required')
if not (root/'.git').exists():git('init','-b','main')
pins=json.loads((root/'DEPENDENCIES.json').read_text())['submodules']
for name,sha in pins.items():
    result=subprocess.check_output(['git','-C',str(root),'ls-files','-s','--',name],text=True)
    if not result.strip():git('update-index','--add','--cacheinfo',f'160000,{sha},{name}')
git('-c','core.autocrlf=false','submodule','update','--init','--recursive')

# Muse's persistent character installer is a reviewed patch over a public pin.
# Never reset a user's dependency checkout or apply the same patch twice.
import hashlib
metadata=json.loads((root/'DEPENDENCIES.json').read_text())
for patch in metadata.get('patches',[]):
    source=root/patch['file']
    if hashlib.sha256(source.read_bytes()).hexdigest()!=patch['sha256']:
        raise SystemExit('Dependency patch hash mismatch: '+patch['file'])
    checkout=root/patch['submodule']
    args=['git','-C',str(checkout),'apply']
    if subprocess.run(args+['--reverse','--check',str(source)],capture_output=True).returncode==0:
        print('Already applied:',patch['file'])
        continue
    subprocess.run(args+['--check',str(source)],check=True)
    subprocess.run(args+[str(source)],check=True)

# The Windows compiler adapter is selected before IDF resolves components.
# Fetch the pinned official compiler now so a clean computer can build too.
if metadata.get('gspc'):
    subprocess.run([sys.executable,str(root/'submodule/esp-mosaico-utils/shared/gsp/fetch.py'),
                    '--tool-version',metadata['gspc']],check=True)
