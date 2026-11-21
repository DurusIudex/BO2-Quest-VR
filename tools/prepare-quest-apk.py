"""Prepare a separate local development APK from the user's downloaded port.

No game binary/assets are published by this script. The baseline package stays installed.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parent.parent
source = root / "research/quest-apk"
target = root / "build/quest-apk"
shutil.copytree(source, target, dirs_exist_ok=True)
release=json.loads((root/'release.json').read_text())
config=target/'apktool.yml'
config_text=config.read_text()
config_text=re.sub(r'(?m)^  versionCode: .*$', '  versionCode: '+str(release['gameVersionCode']),config_text)
config_text=re.sub(r'(?m)^  versionName: .*$', '  versionName: '+release['version'],config_text)
config.write_text(config_text)
new_package = "com.bo2vr.questdev"
fingerprint = hashlib.sha256((root / "build/quest-dev.der").read_bytes()).hexdigest()
for path in target.glob("smali*/**/*.smali"):
    text = path.read_text(encoding="utf-8")
    updated = text.replace("com.t6requiem.bo2arm", new_package).replace(
        "2b4f103bf8dd7852738478add1399ade5fc115b20bf4314f101c466e404b0262", fingerprint)
    if updated != text:
        path.write_text(updated, encoding="utf-8")

smali = target / "smali_classes3/org/wiiucoop/bo2android"
main = smali / "MainActivity.smali"
text = main.read_text(encoding="utf-8")
anchor = "    invoke-static {p0}, Lorg/wiiucoop/bo2android/T6Ui;->applyTheme(Landroid/app/Activity;)V"
start = text.index(".method protected onCreate(Landroid/os/Bundle;)V")
end = text.index(".end method", start)
body = text[start:end]
if body.count(anchor) != 1:
    raise RuntimeError("MainActivity initialization anchor changed")
body = body.replace(anchor, anchor + "\n\n    invoke-static {p0}, Lcom/bo2vr/quest/QuestBridge;->start(Landroid/app/Activity;)V")
main.write_text(text[:start] + body + text[end:], encoding="utf-8")
for name in ("SettingsActivity", "MenuActivity"):
    path = smali / (name + ".smali")
    text = path.read_text(encoding="utf-8")
    start = text.index(".method protected onCreate(Landroid/os/Bundle;)V")
    end = text.index(".end method", start)
    body = text[start:end]
    # Add the entry only on the normal final return, after the content view exists.
    pos = body.rindex("    return-void")
    body = body[:pos] + "    invoke-static {p0}, Lcom/bo2vr/quest/QuestBridge;->addSettings(Landroid/app/Activity;)V\n\n" + body[pos:]
    path.write_text(text[:start]+body+text[end:], encoding="utf-8")
updater = smali / "T6Updater.smali"
text = updater.read_text(encoding="utf-8")
text, count = re.subn(r"\.method static configured\(\)Z.*?\.end method", ".method static configured()Z\n    .locals 1\n    const/4 v0, 0x0\n    return v0\n.end method", text, count=1, flags=re.S)
assert count == 1
updater.write_text(text, encoding="utf-8")

# The hidden phone overlay publishes a neutral pad every UI tick. Quest owns
# this input slot, so stop the phone publisher from releasing held VR buttons.
overlay = smali / "T6TouchOverlay.smali"
text, count = re.subn(r"\.method private publish\(\)V.*?\.end method",
    ".method private publish()V\n    .locals 0\n    return-void\n.end method",
    overlay.read_text(encoding="utf-8"), count=1, flags=re.S)
assert count == 1
overlay.write_text(text, encoding="utf-8")

# Both launcher and host verify their APK signer. Retain the verification,
# updating the expected public certificate for this locally signed clone.
old_cert = b"2b4f103bf8dd7852738478add1399ade5fc115b20bf4314f101c466e404b0262"
host = target / "lib/arm64-v8a/libt6host.so"
data = host.read_bytes()
assert data.count(old_cert) == 1
host.write_bytes(data.replace(old_cert, fingerprint.encode("ascii")))

android = "http://schemas.android.com/apk/res/android"
ET.register_namespace("android", android)
def a(key): return "{"+android+"}"+key
manifest = ET.parse(target / "AndroidManifest.xml")
top = manifest.getroot(); top.set("package", new_package)
application = top.find("application")
application.set(a("label"), "BO2 Quest VR Dev")
for provider in application.findall("provider"):
    provider.set(a("authorities"), provider.get(a("authorities")).replace("com.t6requiem.bo2arm",new_package))
for permission in ("org.khronos.openxr.permission.OPENXR", "org.khronos.openxr.permission.OPENXR_SYSTEM"):
    ET.SubElement(top,"uses-permission",{a("name"):permission})
ET.SubElement(top,"uses-feature",{a("name"):"android.hardware.vr.headtracking",a("required"):"true",a("version"):"1"})
queries = ET.SubElement(top,"queries")
ET.SubElement(queries,"provider",{a("authorities"):"org.khronos.openxr.runtime_broker;org.khronos.openxr.system_runtime_broker"})
for action in ("org.khronos.openxr.OpenXRRuntimeService","org.khronos.openxr.OpenXRApiLayerService"):
    intent=ET.SubElement(queries,"intent"); ET.SubElement(intent,"action",{a("name"):action})
ET.SubElement(application,"meta-data",{a("name"):"com.oculus.supportedDevices",a("value"):"quest2|questpro|quest3|quest3s"})
ET.SubElement(application,"meta-data",{a("name"):"com.oculus.vr.focusaware",a("value"):"true"})
ET.SubElement(application,"activity",{a("name"):"com.bo2vr.quest.QuestSettingsActivity",a("exported"):"true",a("screenOrientation"):"landscape",a("theme"):"@android:style/Theme.Material.NoActionBar"})
ET.SubElement(application,"activity",{a("name"):"com.bo2vr.quest.QuestOnlineActivity",a("exported"):"false",a("screenOrientation"):"landscape",a("theme"):"@android:style/Theme.Material.NoActionBar"})
game = next(x for x in application.findall("activity") if x.get(a("name")) == "org.wiiucoop.bo2android.MainActivity")
game.set(a("resizeableActivity"),"false");game.set(a("screenOrientation"),"landscape");game.set(a("theme"),"@android:style/Theme.Black.NoTitleBar.Fullscreen")
intent=ET.SubElement(game,"intent-filter")
ET.SubElement(intent,"action",{a("name"):"android.intent.action.MAIN"})
ET.SubElement(intent,"category",{a("name"):"com.oculus.intent.category.VR"})
manifest.write(target / "AndroidManifest.xml", encoding="utf-8", xml_declaration=True)
shutil.copy2(root/"build/quest-native/libquestvr.so",target/"lib/arm64-v8a/libquestvr.so")
shutil.copy2(root/"toolchain/android/openxr-loader/jni/arm64-v8a/libopenxr_loader.so",target/"lib/arm64-v8a/libopenxr_loader.so")
print("Prepared separate package",new_package)
