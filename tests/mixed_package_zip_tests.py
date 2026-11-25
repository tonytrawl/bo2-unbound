from pathlib import Path
import subprocess,tempfile,zipfile
root=Path('r2-test');root.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=root) as scratch:
    scratch=Path(scratch).resolve();assert scratch.is_relative_to(root.resolve())
    for method in [zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED]:
        path=scratch/f'mixed-{method}.zip'
        with zipfile.ZipFile(path,'w',method) as z:
            z.writestr('update/code/game.rpl',b'CODE\0\1'*1200)
            z.writestr('update/content/@language/payload.ff',b'localized file'*300)
            z.writestr('sd/mods/demo/data.bin',bytes(range(256))*40)
            z.writestr('sd/textures/payload.iwi',b'SD texture'*300)
        subprocess.run([str(Path('mixed_package_zip_tests').resolve()),str(path),str(scratch/f'target-{method}')],check=True)
    bad=scratch/'bad.zip'
    with zipfile.ZipFile(bad,'w') as z:
        z.writestr('update/content/good.ff',b'good')
        z.writestr('sd/../../wiiu/apps/other.wuhb',b'bad')
    subprocess.run([str(Path('mixed_package_zip_tests').resolve()),str(bad),str(scratch/'rejected'),'reject'],check=True)
