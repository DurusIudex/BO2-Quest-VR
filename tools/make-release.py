"""Stage signed APKs and a hash-pinned GitHub update feed; no game data."""
import hashlib,json,shutil,subprocess,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cfg=json.loads((root/'release.json').read_text())
out=root/'build/release';out.mkdir(parents=True,exist_ok=True)
feed={'schema':1}
with zipfile.ZipFile(out/'BO2-PC-Transfer.zip','w',zipfile.ZIP_DEFLATED) as archive:
    for name in ['Start-BO2-Transfer.cmd','tools/serve-game-files.py','PC-TRANSFER.md']:
        archive.write(root/name,name)
for key,source,name,package,version in [
    ('game','BO2QuestVR-dev.apk','BO2-Quest-VR.apk',cfg['gamePackage'],cfg['gameVersionCode']),
    ('launcher','BO2-Quest-Installer.apk','BO2-Quest-Installer.apk',cfg['installerPackage'],cfg['installerVersionCode'])]:
    p=out/name;shutil.copy2(root/'build'/source,p)
    result=subprocess.run([str(root/'toolchain/android/build-tools/android-16/aapt.exe'),'dump','badging',str(p)],capture_output=True,text=True,check=True).stdout.splitlines()[0]
    assert f"name='{package}'" in result and f"versionCode='{version}'" in result,result
    feed[key]={'package':package,'versionCode':version,'versionName':cfg['version'],'size':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'url':f"https://github.com/{cfg['repository']}/releases/download/v{cfg['version']}/{name}"}
(out/'update.json').write_text(json.dumps(feed,indent=2)+'\n')
(out/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(out.iterdir()) if p.name!='SHA256SUMS.txt'))
print(json.dumps(feed,indent=2))
