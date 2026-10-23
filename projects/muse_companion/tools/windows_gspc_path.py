"""Stage the local CMD adapter in the Windows tool cache without workspace spaces."""
import os
import shutil
import tempfile
from pathlib import Path
source=Path(__file__).parent
cache=Path(os.environ['LOCALAPPDATA'])/'esp-mosaico'/'build-adapters'/'muse_companion_abi2'
if ' ' in str(cache):
    # CMake's env launcher cannot reliably execute a CMD file under a spaced
    # path. Keep only this small adapter in the ordinary Windows temp cache.
    cache=Path(tempfile.gettempdir())/'esp-mosaico-build-adapters'/'muse_companion_abi2'
    if ' ' in str(cache):
        raise RuntimeError('GSPC CMD adapter needs a Windows temporary directory without spaces')
cache.mkdir(parents=True,exist_ok=True)
for name in ['gspc_windows_wrapper.py','gspc_windows_wrapper.cmd']:
    shutil.copy2(source/name,cache/name)
print((cache/'gspc_windows_wrapper.cmd').as_posix())
