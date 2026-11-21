"""BO2 private-room rendezvous and authenticated UDP relay. No game assets."""
import argparse
import hashlib
import json
import re
import secrets
import socket
import struct
import threading
import time
from collections import defaultdict, deque
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HEADER = struct.Struct('!4sBB16sBHH')
MAGIC = b'B2VR'
MAX_PACKET = 16384
CODE = re.compile(r'[A-Z0-9-]{6,24}\Z')

class RequestError(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message

@dataclass
class Peer:
    number: int
    token: bytes = field(default_factory=lambda: secrets.token_bytes(16))
    endpoints: dict = field(default_factory=dict)
    seen: float = field(default_factory=time.monotonic)
    window: float = field(default_factory=time.monotonic)
    packets: int = 0
    bytes: int = 0

@dataclass
class Room:
    code: str
    mode: str
    peers: dict = field(default_factory=dict)
    created: float = field(default_factory=time.monotonic)

class Rooms:
    def __init__(self, public_host, udp_port):
        self.host, self.port = public_host, udp_port
        self.rooms, self.tokens = {}, {}
        self.lock = threading.RLock()
        self.attempts = defaultdict(deque)

    def _drop(self, room):
        self.rooms.pop(room.code, None)
        for p in room.peers.values():
            self.tokens.pop(p.token, None)

    def expire(self):
        now = time.monotonic()
        with self.lock:
            for room in list(self.rooms.values()):
                if now-room.peers[1].seen > 180 or now-room.created > 86400:
                    self._drop(room)
                    continue
                for n, p in list(room.peers.items()):
                    if n != 1 and now-p.seen > 180:
                        room.peers.pop(n); self.tokens.pop(p.token, None)
            for ip, times in list(self.attempts.items()):
                if not times or now-times[-1] > 60:
                    del self.attempts[ip]

    def request(self, action, body, ip):
        with self.lock:
            self.expire()
            now = time.monotonic()
            attempts = self.attempts[ip]
            while attempts and now-attempts[0] > 60: attempts.popleft()
            if len(attempts) >= 20 or len(self.attempts) > 4096:
                raise RequestError(429, 'Too many attempts. Wait one minute.')
            attempts.append(now)
            if action == 'leave':
                try: token = bytes.fromhex(body.get('token', ''))
                except (ValueError, TypeError): token = b''
                found = self.tokens.get(token)
                if found:
                    room, peer = found
                    if peer.number == 1: self._drop(room)
                    else: room.peers.pop(peer.number); self.tokens.pop(token)
                return {'ok': True}
            if action not in ('create', 'join'):
                raise RequestError(404, 'Unknown action.')
            code, mode = body.get('code', ''), body.get('mode', '')
            if not isinstance(code, str): raise RequestError(400, 'Invalid code.')
            code = code.strip().upper()
            if not CODE.fullmatch(code):
                raise RequestError(400, 'Use 6–24 letters, numbers or hyphens for the room code.')
            if mode not in ('zm', 'mp'): raise RequestError(400, 'Select Zombies or Multiplayer.')
            key = hashlib.sha256(code.encode()).hexdigest()
            room = self.rooms.get(key)
            if action == 'create':
                if room: raise RequestError(409, 'That code is already in use.')
                if len(self.rooms) >= 32: raise RequestError(503, 'Server is full. Try later.')
                room = Room(key, mode); self.rooms[key] = room
                number = 1
            else:
                if not room: raise RequestError(404, 'Room not found. Ask the host to open it first.')
                if room.mode != mode: raise RequestError(409, 'The host selected a different game mode.')
                limit = 4 if mode == 'zm' else 18
                number = next((i for i in range(2,limit+1) if i not in room.peers), None)
                if number is None: raise RequestError(409, 'Room is full.')
            peer = Peer(number); room.peers[number] = peer; self.tokens[peer.token] = (room, peer)
            return {'version': 1, 'host': self.host, 'port': self.port,
                    'peer': number, 'token': peer.token.hex(), 'mode': mode,
                    'game_address': '10.253.0.1', 'game_port': 4976}

    def datagram(self, data, address):
        if not HEADER.size <= len(data) <= MAX_PACKET+HEADER.size: return None
        magic, version, kind, token, target, source_port, target_port = HEADER.unpack_from(data)
        if magic != MAGIC or version != 1 or kind not in (0,1) or not source_port: return None
        with self.lock:
            found = self.tokens.get(token)
            if not found: return None
            room, sender = found
            now = time.monotonic()
            if now-room.peers[1].seen > 180: return None
            if now-sender.window >= 1:
                sender.window=now; sender.packets=sender.bytes=0
            sender.packets += 1; sender.bytes += len(data)
            if sender.packets > 2000 or sender.bytes > 2_000_000: return None
            if source_port not in sender.endpoints and len(sender.endpoints) >= 8: return None
            sender.endpoints[source_port] = address; sender.seen = now
            if kind == 0 or not target_port: return None
            if sender.number != 1 and target != 1: return None
            receiver = room.peers.get(target)
            if not receiver or receiver is sender: return None
            endpoint = receiver.endpoints.get(target_port)
            if not endpoint: return None
            packet = HEADER.pack(MAGIC,1,1,receiver.token,sender.number,source_port,target_port)+data[HEADER.size:]
            return packet, endpoint

class Handler(BaseHTTPRequestHandler):
    server_version = 'BO2Relay/1'
    def log_message(self, *_): pass  # Room codes and bearer tokens never enter access logs.
    def reply(self, status, body):
        data=json.dumps(body).encode()
        self.send_response(status); self.send_header('Content-Type','application/json')
        self.send_header('Cache-Control','no-store'); self.send_header('Content-Length',str(len(data)))
        self.end_headers(); self.wfile.write(data)
    def do_GET(self):
        self.reply(200, {'ok':True,'protocol':1}) if self.path == '/health' else self.reply(404, {'error':'Not found'})
    def do_POST(self):
        self.connection.settimeout(5)
        try:
            size=int(self.headers.get('Content-Length','0'))
            if not 0 < size <= 1024: raise RequestError(413,'Invalid request size.')
            body=json.loads(self.rfile.read(size))
            if not isinstance(body,dict): raise RequestError(400,'Expected an object.')
            ip=self.headers.get('X-BO2-Client-IP', self.client_address[0])[:64]
            self.reply(200,self.server.rooms.request(self.path.removeprefix('/'),body,ip))
        except RequestError as e: self.reply(e.status,{'error':e.message})
        except (ValueError,TypeError,TimeoutError): self.reply(400,{'error':'Invalid request.'})

def serve(host, http_port, udp_port):
    rooms=Rooms(host,udp_port)
    server=ThreadingHTTPServer(('127.0.0.1',http_port),Handler); server.rooms=rooms
    threading.Thread(target=server.serve_forever,daemon=True).start()
    udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); udp.bind(('0.0.0.0',udp_port)); udp.settimeout(1)
    print('BO2 relay ready',flush=True)
    while True:
        try:
            data,address=udp.recvfrom(MAX_PACKET+HEADER.size+1)
            result=rooms.datagram(data,address)
            if result: udp.sendto(*result)
        except socket.timeout: rooms.expire()
        except OSError: pass

if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--host',required=True)
    parser.add_argument('--http-port',type=int,default=18950); parser.add_argument('--udp-port',type=int,default=27850)
    args=parser.parse_args(); serve(args.host,args.http_port,args.udp_port)
