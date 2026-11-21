"""Exercise the real Android socket adapter on Quest, without the game or XR."""
import json, pathlib, secrets, socket, struct, subprocess, time, urllib.request

ROOT=pathlib.Path(__file__).resolve().parent.parent
ADB=ROOT/'toolchain/android/platform-tools/adb.exe'
API='https://159-223-145-188.sslip.io/bo2vr/'
REMOTE='/data/local/tmp/bo2vr-relay-test'
HEADER=struct.Struct('!4sBB16sBHH')

def adb(*args):
    return subprocess.check_output([str(ADB),*args],stderr=subprocess.STDOUT,text=True)

def post(action,body):
    request=urllib.request.Request(API+action,json.dumps(body).encode(),{'Content-Type':'application/json'})
    with urllib.request.urlopen(request,timeout=10) as response:return json.load(response)

room='QUEST-'+secrets.token_hex(6).upper()
host=post('create',{'code':room,'mode':'zm'})
process=None
config=ROOT/'build/relay-test-credentials.ini'
try:
    guest=post('join',{'code':room,'mode':'zm'})
    config.write_text(''.join(f'{k}={host[k]}\n' for k in ('host','port','peer','token','mode')),encoding='utf8')
    adb('shell','mkdir','-p',REMOTE)
    adb('push',str(config),REMOTE+'/quest_online.ini')
    adb('push',str(ROOT/'build/quest-native/relay_transport_test'),REMOTE+'/test')
    adb('push',str(ROOT/'build/quest-native/relay-test/libt6host.so'),REMOTE+'/libt6host.so')
    adb('shell','chmod','700',REMOTE+'/test')
    process=subprocess.Popen([str(ADB),'shell',REMOTE+'/test',REMOTE,REMOTE+'/libt6host.so'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as peer:
        peer.settimeout(1)
        token=bytes.fromhex(guest['token'])
        address=(host['host'],host['port'])
        peer.sendto(HEADER.pack(b'B2VR',1,0,token,0,3074,0),address)
        for attempt in range(15):
            peer.sendto(HEADER.pack(b'B2VR',1,1,token,1,3074,4977)+b'quest-adapter',address)
            try:
                data,_=peer.recvfrom(2000)
                assert HEADER.unpack_from(data)[3:]==(token,1,4977,3074)
                assert data[HEADER.size:]==b'quest-reply'
                break
            except socket.timeout:
                if process.poll() is not None:raise RuntimeError(process.stdout.read())
        else:
            output=process.communicate(timeout=10)[0]
            raise RuntimeError('Quest native adapter did not reply. '+output)
    output=process.communicate(timeout=10)[0]
    if process.returncode:raise RuntimeError(output)
    print(output.strip())
    print('PASS: Quest ARM64 adapter <-> public droplet <-> separate UDP client.')
finally:
    if process and process.poll() is None:
        process.terminate()
        # Only this test binary, never the game or a shared system process.
        subprocess.run([str(ADB),'shell','pkill','-f',REMOTE+'/test'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    post('leave',{'token':host['token']})
    config.unlink(missing_ok=True)
    for name in ('test','libt6host.so','quest_online.ini'):
        adb('shell','rm','-f',REMOTE+'/'+name)
    adb('shell','rmdir',REMOTE)
