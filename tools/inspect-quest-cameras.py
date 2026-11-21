"""Inspect the opt-in backend capture without modifying the running game."""
import math
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
def floats(offset, count):
    return struct.unpack_from('<' + 'f' * count, data, offset)
def dot(a, b):
    return sum(x*y for x, y in zip(a, b))
for eye in range(2):
    start = eye * 0xc50
    print(f'Eye {eye}')
    for offset, name in ((0, 'world'), (0x980, 'alternate1'), (0xae0, 'alternate2')):
        base = start + offset
        print(name)
        axes = [floats(base + 0x110 + 12*i, 3) for i in range(3)]
        print('origin', floats(base + 0x100, 3))
        print('axes', axes)
        print('lengths', [math.sqrt(dot(a,a)) for a in axes], 'dots', [dot(axes[a],axes[b]) for a,b in ((0,1),(0,2),(1,2))])
        for matrix, moff in (('view',0), ('projection',0x40), ('viewprojection',0x80)):
            print(matrix, [floats(base+moff+16*i,4) for i in range(4)])
    for offset in (0x150, 0x160, 0x170):
        print(f'viewport {offset:x}', struct.unpack_from('<4i',data,start+offset))
    # LP64 XrView: type + padding + next + pose(7 floats) + fov(4 floats), size64.
    xrv = 2*0xc50 + eye*64
    print('xr orientation', floats(xrv+16,4), 'position', floats(xrv+32,3), 'fov', floats(xrv+44,4))
