"""End-to-end desktop test of the same Java client used by the Android APK."""
import importlib.util,json,ssl,subprocess,tempfile,threading,urllib.request,urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pc_transfer',root/'tools/serve-game-files.py');server=importlib.util.module_from_spec(spec);spec.loader.exec_module(server)
out=root/'build/transfer-test';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['javac','-d',str(out),str(root/'src/installer/java/com/bo2vr/installer/ReleasePolicy.java'),str(root/'src/installer/java/com/bo2vr/installer/TransferClient.java'),str(root/'src/installer/test/TransferClientTest.java')],check=True)
with tempfile.TemporaryDirectory(prefix='bo2-transfer-test-') as temp:
    base=Path(temp);game=base/'game';data=game/'zone/all/zm_transit.ff';data.parent.mkdir(parents=True);data.write_bytes(bytes(range(256))*4097)
    (game/'zone/all/mp_nuketown_2020.ff').write_bytes(b'multiplayer test')
    (game/'players').mkdir();(game/'players/config.cfg').write_text('must never be shared')
    (game/'zone/all/private.log').write_text('must never be shared')
    manifest=server.inventory(game);assert len(manifest['files'])==2 and all(manifest['modes'].values())
    cert,key,pin=server.certificate_files(base/'state');code='ABCDEFGHJKLM'
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert,key)
    httpd=server.http.server.ThreadingHTTPServer(('127.0.0.1',0),server.make_handler(game.resolve(),manifest,code));httpd.daemon_threads=True
    port=httpd.server_port;httpd.socket=context.wrap_socket(httpd.socket,server_side=True)
    stop=threading.Event();discovery=threading.Thread(target=server.discovery,args=(code,pin,port,stop,'127.0.0.1'),daemon=True);discovery.start()
    thread=threading.Thread(target=httpd.serve_forever,daemon=True);thread.start()
    try:
        tls=ssl.create_default_context(cafile=str(cert));tls.check_hostname=False
        for path,headers,expected in [('/manifest',{},401),('/files/../players/config.cfg',{'Authorization':'Bearer '+code},404),('/files/zone/all/private.log',{'Authorization':'Bearer '+code},404),('/files/zone/all/zm_transit.ff',{'Authorization':'Bearer '+code,'Range':'bytes=999999999-'},416)]:
            req=urllib.request.Request(f'https://127.0.0.1:{port}'+path,headers=headers)
            try:urllib.request.urlopen(req,context=tls);raise AssertionError('Request should fail')
            except urllib.error.HTTPError as e:assert e.code==expected,(e.code,expected)
        subprocess.run(['java','-cp',str(out),'TransferClientTest',str(base/'quest'),str(data),pin,code,str(port)],check=True,timeout=60)
        print('PASS: PC helper excludes saves/logs; unauthorized, unknown and invalid-range requests rejected')
    finally:stop.set();httpd.shutdown();httpd.server_close();discovery.join(timeout=2)
