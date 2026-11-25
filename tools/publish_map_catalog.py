#!/usr/bin/env python3
"""Write verified R2 map records locally. Never uploads or commits anything."""
import argparse
import hashlib
import re
import zipfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--zip', type=Path, required=True)
p.add_argument('--manifest', type=Path, required=True)
p.add_argument('--id', required=True)
p.add_argument('--name', required=True)
p.add_argument('--description', required=True)
p.add_argument('--order', type=int, default=10)
p.add_argument('--version', required=True)
p.add_argument('--from-version', default='')
p.add_argument('--url', required=True)
category=p.add_mutually_exclusive_group()
category.add_argument('--custom',action='store_true',help='Write a separate Custom Maps catalog; IDs must start with custom-.')
category.add_argument('--mods',action='store_true',help='Write a separate Mods catalog; IDs must start with mod-.')
a = p.parse_args()
def fail(message): p.error(message)
if not re.fullmatch(r'[a-z0-9-]{1,64}', a.id): fail('Use a stable lowercase pack ID with letters, digits and hyphens.')
if a.custom and (not a.id.startswith('custom-') or len(a.id)==7): fail('Custom map IDs must start with custom- and include a unique name.')
if a.mods and (not a.id.startswith('mod-') or len(a.id)==4): fail('Mod IDs must start with mod- and include a unique name.')
if not a.mods and a.id.startswith('mod-'): fail('Use --mods for mod- IDs and a separate Mods manifest.')
for value in (a.name, a.description, a.url):
    if any(c in value for c in '\r\n|'): fail('Names, descriptions and URLs must be single lines without | characters.')
if not a.name or not a.description or len(a.name)>100 or len(a.description)>1800 or not 0<=a.order<=99999: fail('Metadata exceeds HQ limits.')
for version in (a.version, a.from_version):
    if version and not re.fullmatch(r'\d+(?:\.\d+)*(?:[Pp]\d+(?:\.\d+)*)?',version): fail('Use a dotted numeric content version, optionally with P patches.')
if not a.url.startswith('https://'): fail('A public HTTPS URL is required.')
size = a.zip.stat().st_size
if not 0<size<=4*1024**3-1: fail('Map ZIP must be at most 4 GiB minus one byte (the FAT32 limit).')
with zipfile.ZipFile(a.zip) as z:
    if not 0<len(z.infolist())<=4096: fail('ZIP entry count must be between 1 and 4096.')
    expanded=0; seen=set(); files=0
    for entry in z.infolist():
        if entry.flag_bits&1 or entry.compress_type not in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED): fail('Use unencrypted ZIP Store or Deflate compression.')
        name=entry.filename.rstrip('/')
        if len(name)>768 or any(ord(c)<32 for c in name) or '\\' in name or ':' in name or any(c in name for c in '<>"?*'): fail('Unsafe ZIP path: '+name)
        if any(part in ('','.','..') or part.endswith(('.',' ')) for part in name.split('/')): fail('Unsafe ZIP path: '+name)
        if name in ('update','sd') and entry.is_dir(): continue
        sd_payload=name.startswith('sd/')
        code_payload=a.mods and (name=='update/code' or name.startswith('update/code/'))
        if sd_payload: name='content/'+name[3:]
        if name.startswith('update/'): name=name[7:]
        if (name=='content' or (code_payload and name=='code')) and entry.is_dir(): continue
        if not name.startswith('content/') and not (code_payload and name.startswith('code/')): fail('Unsupported package path: '+name)
        relative=name[5 if code_payload else 8:].lower()
        if (not sd_payload and not a.mods and (relative=='mods' or relative.startswith('mods/'))) or 'update.txt' in relative or '.unbound-' in relative: fail('Reserved payload path: '+name)
        if entry.file_size>2*1024**3: fail('A file exceeds 2 GiB: '+name)
        expanded+=entry.file_size
        if not entry.is_dir():
            files+=1
            key=('sd:' if sd_payload else 'code:' if code_payload else 'update:')+relative
            if key in seen: fail('Duplicate output path: '+name)
            seen.add(key)
    if not files or expanded>5*1024**3: fail('Empty payload or expanded size exceeds 5 GiB.')
    bad=z.testzip()
    if bad: fail('Corrupt ZIP entry: '+bad)
with a.zip.open('rb') as stream: digest=hashlib.file_digest(stream,'sha256').hexdigest()
lines=a.manifest.read_text(encoding='utf-8-sig').splitlines() if a.manifest.exists() else ['# UNBOUND_MODS_V1' if a.mods else '# UNBOUND_CUSTOM_MAPS_V1' if a.custom else '# UNBOUND_RELEASE_MANIFEST_V1']
if a.custom and '# UNBOUND_CUSTOM_MAPS_V1' not in lines: fail('Use a separate manifest beginning with # UNBOUND_CUSTOM_MAPS_V1.')
if a.mods and '# UNBOUND_MODS_V1' not in lines: fail('Use a separate manifest beginning with # UNBOUND_MODS_V1.')
if not a.mods and '# UNBOUND_MODS_V1' in lines: fail('Use --mods to edit a Mods manifest.')
if a.mods and any(line and not line.startswith('#') and not re.match(r'(?:PACK|FILE)\|mod-[a-z0-9-]+\|',line) for line in lines): fail('Mods manifests contain only mod- PACK and FILE records.')
if a.from_version:
    if not any(line.startswith('FILE|'+a.id+'|FULL|-|'+a.version+'|') for line in lines): fail('Publish/add the matching current FULL record before adding a patch.')
    if (tuple(map(int,re.findall(r'\d+',a.from_version)))+(0,)*32)[:32]>=(tuple(map(int,re.findall(r'\d+',a.version)))+(0,)*32)[:32]: fail('Patch FROM must be older than TO.')
    lines=[line for line in lines if not line.startswith('FILE|'+a.id+'|PATCH|'+a.from_version+'|')]
else:
    lines=[line for line in lines if not line.startswith('FILE|'+a.id+'|')]
lines=[line for line in lines if not line.startswith('PACK|'+a.id+'|')]
lines.append(f'PACK|{a.id}|{a.order}|{a.name}|{a.description}')
kind='PATCH' if a.from_version else 'FULL'
lines.append(f'FILE|{a.id}|{kind}|{a.from_version or "-"}|{a.version}|{a.url}|{digest}|{size}')
a.manifest.parent.mkdir(parents=True,exist_ok=True)
temp=a.manifest.with_name(a.manifest.name+'.tmp')
temp.write_text('\n'.join(lines)+'\n',encoding='utf-8')
temp.replace(a.manifest)
print('Wrote',a.manifest)
print(lines[-1])
