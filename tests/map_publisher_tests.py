from pathlib import Path
import tempfile,zipfile,subprocess,hashlib,sys
Path("r2-test").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir="r2-test") as temp:
    root=Path(temp);assert root.resolve().is_relative_to(Path('r2-test').resolve());archive=root/'pack.zip';manifest=root/'manifest.txt'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:z.writestr('content/@language/test.ff',b'test map bytes')
    manifest.write_text('# UNBOUND_RELEASE_MANIFEST_V1\n#HQ|51|1.0.0|example\nUNIVERSAL|FULL|-|1.0.0|example\n')
    args=[sys.executable,'tools/publish_map_catalog.py','--zip',str(archive),'--manifest',str(manifest),'--id','pack-1','--name','Test Pack','--description','Test description','--version','1.1.0','--url','https://downloads.unc-bo2.fun/test.zip']
    subprocess.run(args,check=True,capture_output=True)
    text=manifest.read_text();assert hashlib.sha256(archive.read_bytes()).hexdigest() in text
    assert str(archive.stat().st_size) in text
    assert '#HQ|51|' in text and 'UNIVERSAL|FULL|' in text
    subprocess.run(args+['--from-version','1.0.0'],check=True,capture_output=True)
    assert 'PATCH|1.0.0|1.1.0' in manifest.read_text()
    with zipfile.ZipFile(archive,'w') as z:z.writestr('update/code/game.rpl',b'not a map')
    assert subprocess.run(args,capture_output=True).returncode!=0
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:z.writestr('content/@language/custom.ff',b'custom map bytes')
    custom=list(args);custom[custom.index('--manifest')+1]=str(root/'custom-maps.txt')
    custom[custom.index('--id')+1]='custom-island';custom.append('--custom')
    subprocess.run(custom,check=True,capture_output=True)
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('sd/mods/survival/modload.txt',b'Survival Rules')
        z.writestr('sd/mods/survival/script.gsc',b'mod script')
        z.writestr('update/content/mods/survival/data.ff',b'console mod data')
        z.writestr('update/code/mod-engine.rpl',b'optional console engine support')
    mods=list(args);mods[mods.index('--manifest')+1]=str(root/'mods.txt')
    mods[mods.index('--id')+1]='mod-survival';mods.append('--mods')
    subprocess.run(mods,check=True,capture_output=True)
    mods_text=(root/'mods.txt').read_text()
    assert mods_text.startswith('# UNBOUND_MODS_V1\n') and 'PACK|mod-survival|' in mods_text
    assert hashlib.sha256(archive.read_bytes()).hexdigest() in mods_text and str(archive.stat().st_size) in mods_text
    subprocess.run(mods+['--from-version','1.0.0'],check=True,capture_output=True)
    assert 'PATCH|1.0.0|1.1.0' in (root/'mods.txt').read_text()
    assert subprocess.run(custom,capture_output=True).returncode!=0
    mods[mods.index('--manifest')+1]=str(manifest)
    assert subprocess.run(mods,capture_output=True).returncode!=0
    mods[mods.index('--manifest')+1]=str(root/'mods.txt')
    mods[mods.index('--id')+1]='custom-survival'
    assert subprocess.run(mods,capture_output=True).returncode!=0
    custom_text=(root/'custom-maps.txt').read_text()
    assert custom_text.startswith('# UNBOUND_CUSTOM_MAPS_V1\n') and 'PACK|custom-island|' in custom_text
    custom[custom.index('--id')+1]='pack-1'
    assert subprocess.run(custom,capture_output=True).returncode!=0
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('update/content/@language/custom.ff',b'console map')
        z.writestr('sd/mods/custom/script.gsc',b'SD mod')
    custom[custom.index('--id')+1]='custom-island'
    subprocess.run(custom,check=True,capture_output=True)
print('Publisher helper: real ZIP CRC/hash/size, patch record and forbidden code path checks passed.')

