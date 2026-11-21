"""Create an ephemeral room and check public HTTPS + bidirectional UDP."""
import json, secrets, socket, struct, urllib.request
API='https://159-223-145-188.sslip.io/bo2vr/'
HEADER=struct.Struct('!4sBB16sBHH')
def post(action,body):
    request=urllib.request.Request(API+action,json.dumps(body).encode(),{'Content-Type':'application/json'})
    with urllib.request.urlopen(request,timeout=10) as response: return json.load(response)
room='TEST-'+secrets.token_hex(6).upper()
host=post('create',{'code':room,'mode':'zm'})
try:
    guest=post('join',{'code':room,'mode':'zm'})
    address=(host['host'],host['port'])
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as hs, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as gs:
        hs.settimeout(5);gs.settimeout(5)
        def packet(p,kind,target,payload=b''):
            return HEADER.pack(b'B2VR',1,kind,bytes.fromhex(p['token']),target,3074,3074)+payload
        hs.sendto(packet(host,0,0),address);gs.sendto(packet(guest,0,0),address)
        # Retry handles registration racing the first datagram.
        for attempt in range(3):
            gs.sendto(packet(guest,1,1,b'guest challenge'),address)
            try: data,_=hs.recvfrom(2000);break
            except socket.timeout:
                if attempt==2:raise
        assert HEADER.unpack_from(data)[3]==bytes.fromhex(host['token'])
        assert HEADER.unpack_from(data)[4]==guest['peer'] and data[HEADER.size:]==b'guest challenge'
        hs.sendto(packet(host,1,guest['peer'],b'host challenge response'),address)
        data,_=gs.recvfrom(2000)
        assert HEADER.unpack_from(data)[3]==bytes.fromhex(guest['token'])
        assert HEADER.unpack_from(data)[4]==1 and data[HEADER.size:]==b'host challenge response'
        print('PASS: public HTTPS room create/join and authenticated UDP in both directions.')
finally:
    post('leave',{'token':host['token']})
