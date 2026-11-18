"""Package corresponding source, including pinned JUCE, without build caches or Git data."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import re

root = Path(__file__).resolve().parent.parent
version = re.search(r'project\(Anyvoc VERSION ([\d.]+)', (root / 'CMakeLists.txt').read_text()).group(1)
destination = root / 'dist' / f'Anyvoc-{version}-Source.zip'
destination.parent.mkdir(exist_ok=True)
roots = ['Source', 'Tests', 'scripts', 'docs', 'third_party/JUCE']
files = [root / name for name in ['CMakeLists.txt', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', '.gitignore']]
files.extend(root / name for name in ['.gitattributes', '.gitmodules'] if (root / name).is_file())
if (root / '.github').is_dir():
    roots.append('.github')
for folder in roots:
    files.extend(path for path in (root / folder).rglob('*')
                 if path.is_file() and '.git' not in path.relative_to(root).parts and '__pycache__' not in path.parts)
with ZipFile(destination, 'w', ZIP_DEFLATED, compresslevel=6) as archive:
    for path in sorted(files):
        archive.write(path, 'Anyvoc-Source/' + path.relative_to(root).as_posix())
print(destination)
