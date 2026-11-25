from pathlib import Path
import hashlib, shutil, zipfile
root=Path.cwd(); out=root/'release-v1.0.0-r51'; out.mkdir(exist_ok=True)
source=root/'BO2-Unbound-HQ.wuhb'; target=out/source.name
shutil.copy2(source,target)
blob=target.read_bytes(); digest=hashlib.sha256(blob).hexdigest(); size=len(blob)
record=f'#HQ|51|1.0.0|https://github.com/tonytrawl/bo2-unbound/releases/download/hq-v1.0.0/BO2-Unbound-HQ.wuhb|{digest}|{size}'
(out/'hq-release-record.txt').write_text(record+'\n',encoding='utf-8')
(out/'legacy-github-hq-record.txt').write_text(record+'\n',encoding='utf-8')
(out/'release-manifest.DRAFT.txt').write_text('# UNBOUND_RELEASE_MANIFEST_V1\n# DRAFT: add the verified Core and plugin/map records before publishing.\n'+record+'\n#CORE_SD|1.0.0\n',encoding='utf-8')
archive=out/'BO2-Unbound-HQ-v1.0.0-sd.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    z.write(target,'wiiu/apps/BO2-Unbound-HQ/BO2-Unbound-HQ.wuhb')
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert z.namelist()==['wiiu/apps/BO2-Unbound-HQ/BO2-Unbound-HQ.wuhb']
    assert hashlib.sha256(z.read(z.namelist()[0])).hexdigest()==digest
(out/'SHA256SUMS.txt').write_text(f'{digest}  {target.name}\n{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n',encoding='utf-8')
(out/'BUILD_INFO.txt').write_text(f'''Unbound HQ V1.0.0 / revision 51
Build: native Wii U, existing Docker devkitPro/WUT toolchain, make BUILD=build-network -j4
WUHB bytes: {size}
WUHB SHA256: {digest}
Validation: complete host regression suite passed; new map catalog/path/version/patch tests passed; targeted touch viewport tests passed; actual SDL UI rendered and touch targets verified; Python publisher helper ZIP/CRC/hash/size/patch/path checks passed; packaged SD ZIP CRC and embedded WUHB hash verified.
Hardware: this V1 component installer has not been tested on a physical Wii U in this session.
No live uploads, commits, pushes, or FTP deployments were performed.
All release checks use https://downloads.unc-bo2.fun/release-manifest.txt. One network fetch is shared for the session. News and Workshop descriptions remain on their existing URLs.
release-manifest.DRAFT.txt is intentionally incomplete. Supply real Core/plugin/map records before publishing. Legacy GitHub feed receives only the new HQ record as the migration bridge.
''',encoding='utf-8')
(out/'RELEASE_NOTES.txt').write_text('''# Unbound HQ V1.0.0

Choose the content you want. Core stays on your game drive, while optional map packs update independently. USB game users can choose SD or their USB update folder; disc and internal-storage users use SD.

- One Cloudflare R2 manifest for HQ, Core, plugins and optional maps.
- New content selection and installation review screens.
- Optional map packs, with smaller direct patches when available.
- Verified installation from predownloaded SD ZIPs.
- Saved pack selections, installed versions and interrupted-install recovery.
- SD and console mods remain together in Mod Manager.
- The old Mod Link plugin is retired after compatible Core is installed. A full console restart is required when this happens.

Publisher: finish the Core and R2 manifest setup in ROLLOUT.txt before publishing these notes as a completed content rollout. Test the new installer on hardware first.
''',encoding='utf-8')
shutil.copy2(root/'tools/publish_map_catalog.py',out/'publish_map_catalog.py')
print(record)
print('SD ZIP bytes:',archive.stat().st_size)
print('Verified packaged WUHB matches native build.')
