import secrets
import unittest
from relay import Rooms, HEADER, MAGIC, RequestError

class RelayTests(unittest.TestCase):
    def setUp(self): self.rooms=Rooms('127.0.0.1',27850)
    def request(self, action, code='ROOM-123', mode='zm', ip='test'):
        return self.rooms.request(action,{'code':code,'mode':mode},ip)
    def packet(self, peer, kind, target=1, source=3074, dest=3074, payload=b''):
        return HEADER.pack(MAGIC,1,kind,bytes.fromhex(peer['token']),target,source,dest)+payload
    def test_bidirectional_packets_and_distinct_peers(self):
        host=self.request('create'); guest=self.request('join')
        self.rooms.datagram(self.packet(host,0),('host',4000))
        self.rooms.datagram(self.packet(guest,0),('guest',5000))
        incoming=self.rooms.datagram(self.packet(guest,1,payload=b'challenge'),('guest',5000))
        self.assertEqual(incoming[1],('host',4000))
        decoded=HEADER.unpack_from(incoming[0]); self.assertEqual(decoded[3],bytes.fromhex(host['token']))
        self.assertEqual(decoded[4],2); self.assertEqual(incoming[0][HEADER.size:],b'challenge')
        outgoing=self.rooms.datagram(self.packet(host,1,2,payload=b'reply'),('host',4000))
        self.assertEqual(outgoing[1],('guest',5000)); self.assertEqual(outgoing[0][HEADER.size:],b'reply')
    def test_room_isolation_and_forged_credentials(self):
        a=self.request('create'); b=self.request('create','ROOM-456')
        self.rooms.datagram(self.packet(a,0),('a',1)); self.rooms.datagram(self.packet(b,0),('b',2))
        self.assertIsNone(self.rooms.datagram(self.packet(a,1,2,payload=b'bad'),('a',1)))
        forged=dict(a,token=secrets.token_hex(16))
        self.assertIsNone(self.rooms.datagram(self.packet(forged,1),('a',1)))
    def test_codes_modes_capacity_and_expiry(self):
        self.request('create','room-123')
        for action,code,mode,status in [('create','ROOM-123','zm',409),('join','ROOM-123','mp',409),('join','MISSING','zm',404),('create','short','zm',400)]:
            with self.assertRaises(RequestError) as e: self.request(action,code,mode)
            self.assertEqual(e.exception.status,status)
        for _ in range(3): self.request('join')
        with self.assertRaises(RequestError): self.request('join')
        room=next(iter(self.rooms.rooms.values())); room.peers[1].seen-=181
        self.rooms.expire(); self.assertEqual(len(self.rooms.rooms),0); self.assertEqual(len(self.rooms.tokens),0)
    def test_leave_revokes_tokens(self):
        host=self.request('create'); guest=self.request('join')
        self.rooms.request('leave',{'token':guest['token']},'test')
        self.assertNotIn(bytes.fromhex(guest['token']),self.rooms.tokens)
        self.rooms.request('leave',{'token':host['token']},'test')
        self.assertFalse(self.rooms.rooms)
    def test_malformed_and_limits(self):
        host=self.request('create')
        for data in [b'',b'X'*27,self.packet(host,1,payload=b'x'*16385)]:
            self.assertIsNone(self.rooms.datagram(data,('a',1)))
        for _ in range(19):
            try: self.request('join','MISSING')
            except RequestError: pass
        with self.assertRaises(RequestError) as e: self.request('join')
        self.assertEqual(e.exception.status,429)
    def test_guest_cannot_send_to_other_guest(self):
        host=self.request('create'); a=self.request('join'); b=self.request('join')
        for peer in [host,a,b]: self.rooms.datagram(self.packet(peer,0),('test',peer['peer']))
        self.assertIsNone(self.rooms.datagram(self.packet(a,1,3,payload=b'bad'),('test',2)))

if __name__=='__main__': unittest.main()
