"""Fetch pinned, redistributable native dependencies once (no network at build time)."""
from pathlib import Path
import hashlib, io, json, shutil, tarfile, urllib.request, ssl, time
ROOT = Path(__file__).resolve().parents[1]
PACKAGES = {
    'assimp': ('https://codeload.github.com/assimp/assimp/tar.gz/refs/tags/v6.0.5', 'archive'),
    'imgui': ('https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.91.9b', 'archive'),
    'glfw': ('https://codeload.github.com/glfw/glfw/tar.gz/refs/tags/3.4', 'archive'),
    'json': ('https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp', 'json.hpp'),
    'glm': ('https://codeload.github.com/g-truc/glm/tar.gz/refs/tags/1.0.1', 'archive'),
    'pybind11': ('https://codeload.github.com/pybind/pybind11/tar.gz/refs/tags/v3.0.1', 'archive'),
    'stb_image': ('https://raw.githubusercontent.com/nothings/stb/f0569113c93ad095470c54bf34a17b36646bbbb5/stb_image.h', 'stb_image.h'),
    'stb_truetype': ('https://raw.githubusercontent.com/nothings/stb/f0569113c93ad095470c54bf34a17b36646bbbb5/stb_truetype.h', 'stb_truetype.h'),
    'font': ('https://raw.githubusercontent.com/google/fonts/main/ofl/notosans/NotoSans%5Bwdth,wght%5D.ttf', 'font.ttf'),
    'font_license': ('https://raw.githubusercontent.com/google/fonts/main/ofl/notosans/OFL.txt', 'FONT-LICENSE.txt'),
    'miniaudio': ('https://raw.githubusercontent.com/mackron/miniaudio/0.11.23/miniaudio.h', 'miniaudio.h'),
}
def main():
    vendor = ROOT / 'vendor'; vendor.mkdir(exist_ok=True)
    lock_path = vendor / 'dependencies.lock.json'
    lock = json.loads(lock_path.read_text()) if lock_path.exists() else {}
    for name, (url, filename) in PACKAGES.items():
        target = vendor / (name if filename == 'archive' else filename)
        if target.exists(): continue
        # The distributable project already contains the exact default font snapshot.
        # Reuse it instead of contacting a moving branch when bootstrapping a checkout.
        bundled = ROOT / 'graphics' / filename
        if filename in ('font.ttf', 'FONT-LICENSE.txt') and bundled.exists() and name in lock:
            if hashlib.sha256(bundled.read_bytes()).hexdigest() == lock[name]['sha256']:
                shutil.copy2(bundled, target)
                continue
        print(f'Downloading {name}: {url}', flush=True)
        context = ssl.create_default_context(cafile='/etc/ssl/cert.pem') if Path('/etc/ssl/cert.pem').exists() else ssl.create_default_context()
        for attempt in range(4):
            try:
                data = urllib.request.urlopen(url, timeout=60, context=context).read()
                break
            except Exception:
                if attempt == 3: raise
                time.sleep(2)
        digest = hashlib.sha256(data).hexdigest()
        if name in lock and digest != lock[name]['sha256']:
            raise RuntimeError(f'Checksum mismatch: {name}')
        if filename == 'archive':
            temp = vendor / (name + '.tmp'); temp.mkdir(exist_ok=True)
            with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
                archive.extractall(temp, filter='data')
            children = list(temp.iterdir())
            if len(children) != 1: raise RuntimeError('Unexpected archive layout')
            shutil.move(str(children[0]), target); temp.rmdir()
        else: target.write_bytes(data)
        lock[name] = {'url': url, 'sha256': digest}
        lock_path.write_text(json.dumps(lock, indent=2) + '\n')
    graphics = ROOT / 'graphics'
    graphics.mkdir(exist_ok=True)
    for name in ('font.ttf', 'FONT-LICENSE.txt'):
        if not (graphics / name).exists(): shutil.copy2(vendor / name, graphics / name)
if __name__ == '__main__': main()
