#!/bin/bash
set -euo pipefail
id bo2relay >/dev/null 2>&1 || useradd --system --home /opt/bo2vr-relay --shell /usr/sbin/nologin bo2relay
install -d -o root -g root -m 755 /opt/bo2vr-relay
install -o root -g root -m 644 /tmp/bo2vr-relay.py /opt/bo2vr-relay/relay.py
cat > /etc/systemd/system/bo2vr-relay.service <<'EOF'
[Unit]
Description=BO2 Quest private room relay
After=network-online.target
[Service]
User=bo2relay
WorkingDirectory=/opt/bo2vr-relay
ExecStart=/usr/bin/python3 /opt/bo2vr-relay/relay.py --host 159.223.145.188
Restart=on-failure
RestartSec=3
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
RestrictAddressFamilies=AF_INET AF_UNIX
MemoryMax=128M
CPUQuota=40%
TasksMax=64
LimitNOFILE=256
[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now bo2vr-relay
systemctl restart bo2vr-relay
for n in 1 2 3 4 5; do
    if curl --fail --silent http://127.0.0.1:18950/health; then break; fi
    sleep 1
done
curl --fail --silent http://127.0.0.1:18950/health >/dev/null
config=/opt/aistagram-relay/relay.Caddyfile
backup="${config}.before-bo2vr-$(date -u +%Y%m%d-%H%M%S)"
cp -p "$config" "$backup"
python3 - <<'PY'
from pathlib import Path
import re
p=Path('/opt/aistagram-relay/relay.Caddyfile')
s=p.read_text()
block='''        # BEGIN BO2VR
        handle_path /bo2vr/* {
            request_body {
                max_size 1KB
            }
            reverse_proxy 127.0.0.1:18950 {
                header_up X-BO2-Client-IP {remote_host}
                transport http {
                    dial_timeout 2s
                    response_header_timeout 6s
                }
            }
        }
        # END BO2VR
'''
if '# BEGIN BO2VR' in s:
    s=re.sub(r'        # BEGIN BO2VR\n.*?        # END BO2VR\n',block,s,count=1,flags=re.S)
else:
    assert '    route {\n' in s
    s=s.replace('    route {\n','    route {\n'+block,1)
# Write in place: Docker has this file bind-mounted.
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
ufw allow 27850/udp comment 'BO2 Quest room relay'
systemctl is-active bo2vr-relay
curl --fail --silent https://159-223-145-188.sslip.io/bo2vr/health
