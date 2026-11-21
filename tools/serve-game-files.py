"""Paired, read-only LAN transfer of the owner's installed BO2 data.

No Internet upload, account credential, saves or game executables are served.
UDP discovery authenticates the temporary TLS certificate using the pairing code.
"""
import argparse,hashlib,hmac,http.server,ipaddress,json,os,secrets,socket,ssl,subprocess,threading,time
from pathlib import Path
from urllib.parse import unquote,urlsplit

ROOTS=('zone','main','sound','video')
EXTENSIONS={'.ff','.ipak','.xpak','.iwd','.sabs','.sabl','.bik','.webm','.clump'}
LOCALIZATIONS=('localization.txt','localization_zm.txt','localization_mp.txt')
def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def inventory(root):
    root=root.resolve();entries=[]
    paths=[p for name in ROOTS for p in (root/name).rglob('*') if p.is_file() and p.suffix.lower() in EXTENSIONS]
    paths += [root/name for name in LOCALIZATIONS if (root/name).is_file()]
    for p in sorted(paths):
        if not p.resolve().is_relative_to(root) or p.is_symlink():raise ValueError('Source links are not allowed')
        rel=p.relative_to(root).as_posix();stat=p.stat()
        entries.append({'path':rel,'size':stat.st_size,'sha256':digest(p)})
    names={x['path'] for x in entries}
    zm='zone/all/zm_transit.ff' in names;mp='zone/all/mp_nuketown_2020.ff' in names
    return {'schema':1,'modes':{'zombies':zm,'multiplayer':mp},'totalBytes':sum(e['size'] for e in entries),'files':entries}
def local(address):
    try:return ipaddress.ip_address(address).is_private or ipaddress.ip_address(address).is_loopback
    except ValueError:return False
def proof(code,nonce,certificate,port):
    message=f'BO2TRANSFER1\n{nonce}\n{certificate}\n{port}'.encode()
    return hmac.new(code.encode(),message,hashlib.sha256).hexdigest()
def make_handler(root,manifest,code):
    records={e['path']:e for e in manifest['files']};encoded=json.dumps(manifest,separators=(',',':')).encode()
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def do_GET(self):
            if not local(self.client_address[0]):self.send_error(403);return
            authorization=self.headers.get('Authorization','')
            if not hmac.compare_digest(authorization,'Bearer '+code):self.send_error(401);return
            path=unquote(urlsplit(self.path).path)
            if path=='/manifest':
                self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(encoded)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(encoded);return
            if not path.startswith('/files/'):self.send_error(404);return
            name=path[len('/files/'):];entry=records.get(name)
            if entry is None:self.send_error(404);return
            p=(root/name).resolve()
            if not p.is_relative_to(root) or not p.is_file() or p.stat().st_size!=entry['size']:self.send_error(409);return
            start=0;size=entry['size'];requested=self.headers.get('Range')
            if requested:
                import re
                match=re.fullmatch(r'bytes=(\d+)-',requested)
                if not match or int(match[1])>=size:self.send_error(416);return
                start=int(match[1])
            try:
                with p.open('rb') as f:
                    f.seek(start);self.send_response(206 if requested else 200)
                    self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Length',str(size-start))
                    self.send_header('Accept-Ranges','bytes');self.send_header('Cache-Control','no-store')
                    if requested:self.send_header('Content-Range',f'bytes {start}-{size-1}/{size}')
                    self.end_headers();remaining=size-start
                    while remaining:
                        data=f.read(min(256*1024,remaining))
                        if not data:break
                        self.wfile.write(data);remaining-=len(data)
            except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass
    return Handler
def discovery(code,certificate,port,stop,bind='0.0.0.0',discovery_port=27852):
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
        s.bind((bind,discovery_port));s.settimeout(1)
        while not stop.is_set():
            try:
                data,peer=s.recvfrom(512)
                if not local(peer[0]):continue
                request=json.loads(data);nonce=request.get('nonce','')
                if request.get('service')!='BO2TRANSFER1' or len(nonce)!=32 or any(c not in '0123456789abcdef' for c in nonce):continue
                response={'service':'BO2TRANSFER1','nonce':nonce,'certificate':certificate,'port':port,'proof':proof(code,nonce,certificate,port)}
                s.sendto(json.dumps(response,separators=(',',':')).encode(),peer)
            except (TimeoutError,socket.timeout,ValueError,KeyError,TypeError):continue
def certificate_files(state):
    state.mkdir(parents=True,exist_ok=True);cert=state/'transfer.crt';key=state/'transfer.key'
    openssl=Path(r'C:\Program Files\Git\usr\bin\openssl.exe')
    command=str(openssl) if openssl.exists() else 'openssl'
    subprocess.run([command,'req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(key),'-out',str(cert),'-days','2','-subj','/CN=BO2 personal LAN transfer'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    der=ssl.PEM_cert_to_DER_cert(cert.read_text())
    return cert,key,hashlib.sha256(der).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Black Ops II'))
    parser.add_argument('--state',type=Path,default=Path(__file__).resolve().parents[1]/'build/pc-transfer')
    parser.add_argument('--bind',default='0.0.0.0');parser.add_argument('--port',type=int,default=27851)
    parser.add_argument('--hours',type=float,default=6)
    args=parser.parse_args();root=args.game.resolve()
    print('Checking Zombies and Multiplayer files; please wait…',flush=True)
    manifest=inventory(root)
    if not all(manifest['modes'].values()):raise SystemExit('Both Zombies and Multiplayer must be installed in Steam first.')
    code=''.join(secrets.choice('ABCDEFGHJKLMNPQRSTUVWXYZ23456789') for _ in range(12))
    cert,key,fingerprint=certificate_files(args.state)
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=ssl.TLSVersion.TLSv1_2;context.load_cert_chain(cert,key)
    httpd=http.server.ThreadingHTTPServer((args.bind,args.port),make_handler(root,manifest,code));httpd.daemon_threads=True
    httpd.socket=context.wrap_socket(httpd.socket,server_side=True)
    stop=threading.Event();threading.Thread(target=discovery,args=(code,fingerprint,args.port,stop),daemon=True).start()
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
        s.connect(('192.0.2.1',9));address=s.getsockname()[0]
    state={'address':address,'port':args.port,'code':code,'bytes':manifest['totalBytes'],'files':len(manifest['files']),'expires':int(time.time()+args.hours*3600)}
    (args.state/'session.json').write_text(json.dumps(state,indent=2))
    print(f"PC address: {address}\nPairing code: {'-'.join(code[i:i+4] for i in range(0,12,4))}\nData: {manifest['totalBytes']/1e9:.2f} GB, {len(manifest['files'])} files\nOpen BO2 Quest Launcher > Install game files from PC.\nKeep this window open. Transfer expires after {args.hours:g} hours. Ctrl+C stops it.",flush=True)
    timer=threading.Timer(args.hours*3600,httpd.shutdown);timer.daemon=True;timer.start()
    try:httpd.serve_forever()
    except KeyboardInterrupt:pass
    finally:stop.set();timer.cancel();httpd.server_close();key.unlink(missing_ok=True)
if __name__=='__main__':main()
