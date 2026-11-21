#!/bin/bash
# Run on the existing download server after copying the signed APK to /tmp.
set -euo pipefail
source_apk=${1:?Pass the uploaded installer APK path}
destination=/opt/aistagram-relay/downloads/bo2-quest
install -d -m 755 "$destination"
install -m 644 "$source_apk" "$destination/BO2-Quest-Installer.apk.part"
mv "$destination/BO2-Quest-Installer.apk.part" "$destination/BO2-Quest-Installer.apk"
config=/opt/aistagram-relay/relay.Caddyfile
backup="${config}.before-bo2-installer-$(date -u +%Y%m%d-%H%M%S)"
cp -p "$config" "$backup"
python3 - <<'PY'
from pathlib import Path
import re
p=Path('/opt/aistagram-relay/relay.Caddyfile')
s=p.read_text()
block='''        # BEGIN BO2 INSTALLER
        handle_path /bo2-install/* {
            @unsupported not method GET HEAD
            respond @unsupported "Method not allowed" 405
            root * /srv/downloads/bo2-quest
            header Cache-Control "no-cache"
            header Content-Type "application/vnd.android.package-archive"
            header Content-Disposition "attachment; filename=BO2-Quest-Installer.apk"
            file_server
        }
        # END BO2 INSTALLER
'''
if '# BEGIN BO2 INSTALLER' in s:
    s=re.sub(r'        # BEGIN BO2 INSTALLER\n.*?        # END BO2 INSTALLER\n',block,s,count=1,flags=re.S)
else:
    assert '        # BEGIN BO2VR\n' in s
    s=s.replace('        # BEGIN BO2VR\n',block+'        # BEGIN BO2VR\n',1)
p.write_text(s)
PY
if ! docker exec aistagram-relay-caddy-1 caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile; then
    cat "$backup" > "$config"
    exit 1
fi
if ! docker exec aistagram-relay-caddy-1 caddy reload --config /etc/caddy/Caddyfile --adapter caddyfile; then
    cat "$backup" > "$config"
    docker exec aistagram-relay-caddy-1 caddy reload --config /etc/caddy/Caddyfile --adapter caddyfile
    exit 1
fi
sha256sum "$destination/BO2-Quest-Installer.apk"
