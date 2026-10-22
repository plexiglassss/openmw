"""Headless Mesa regression for the production frame-preservation helper.
Requires Linux libEGL, libGL, C++ compiler and GL/gl.h (headers_dir argument).
No OpenMW/NVIDIA runtime claim: simulates copy-before-draw loading traversals.
"""
import ctypes as C
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
I, U, P = C.c_int, C.c_uint, C.c_void_p
E = C.CDLL('libEGL.so.1')

def egl(name, result, args):
    f = getattr(E, name); f.restype = result; f.argtypes = args; return f

getproc = egl('eglGetProcAddress', P, [C.c_char_p])
platform = C.CFUNCTYPE(P, U, P, C.POINTER(I))(getproc(b'eglGetPlatformDisplayEXT'))
display = platform(0x31DD, None, None)  # surfaceless Mesa
assert egl('eglInitialize', U, [P, P, P])(display, None, None)
assert egl('eglBindAPI', U, [U])(0x30A2)  # OpenGL
attrs = (I * 9)(0x3033, 1, 0x3040, 8, 0x3024, 8, 0x3023, 8, 0x3038)
config, count = P(), I()
assert egl('eglChooseConfig', U, [P, P, P, I, P])(display, attrs, C.byref(config), 1, C.byref(count)) and count.value
ca = (I * 7)(0x3098, 4, 0x30FB, 3, 0x30FD, 1, 0x3038)
context = egl('eglCreateContext', P, [P, P, P, P])(display, config, None, ca)
assert context
assert egl('eglMakeCurrent', U, [P, P, P, P])(display, None, None, context)

def gl(name, result, *args):
    return C.CFUNCTYPE(result, *args)(getproc(name.encode()))

print('GL:', gl('glGetString', C.c_char_p, U)(0x1F02).decode())
print('Renderer:', gl('glGetString', C.c_char_p, U)(0x1F01).decode())
error = gl('glGetError', U)
tex = gl('glBindTexture', None, U, U)
store = gl('glTexStorage2D', None, U, I, U, I, I)
upload = gl('glTexSubImage2D', None, U, I, I, I, I, I, U, U, P)
get = gl('glGetTexImage', None, U, I, U, U, P)
textures = (U * 4)(); gl('glGenTextures', None, I, P)(4, textures)
W, H, T, RGBA, BYTE = 7, 5, 0x0DE1, 0x1908, 0x1401
for t in textures:
    tex(T, t); store(T, 1, 0x8058, W, H)

def fill(t, value):
    tex(T, t)
    data = (C.c_ubyte * (W * H * 4))(*([value] * (W * H * 4)))
    upload(T, 0, 0, 0, W, H, RGBA, BYTE, data)

def read(t):
    tex(T, t); data = (C.c_ubyte * (W * H * 4))()
    get(T, 0, RGBA, BYTE, data); return bytes(data)

# Compile the exact helper used by the DLL, not a Python reimplementation.
with tempfile.TemporaryDirectory() as d:
    p = pathlib.Path(d)
    (p/'bridge.cpp').write_text('''#include "src/fg/frame_contents.hpp"
decltype(glCopyImageSubData_) glCopyImageSubData_ = nullptr;
extern "C" void init(void* fn) { glCopyImageSubData_ = reinterpret_cast<decltype(glCopyImageSubData_)>(fn); }
extern "C" void preserve(unsigned src, unsigned dst, int w, int h) { fg::PreserveFrameContents(src,dst,w,h); }
''')
    subprocess.run(['g++', '-std=c++20', '-shared', '-fPIC', '-I'+str(root), '-I'+sys.argv[1], str(p/'bridge.cpp'), '-o', str(p/'bridge.so')], check=True)
    bridge = C.CDLL(str(p/'bridge.so')); bridge.init.argtypes = [P]
    bridge.init(getproc(b'glCopyImageSubData'))
    bridge.preserve.argtypes = [U,U,I,I]
    # Prior slots contain different viewpoints. Loading reads BEFORE redraw.
    for i,t in enumerate(textures): fill(t, 20+i*30)
    old_sequence = [read(textures[i%4])[0] for i in range(12)]
    assert len(set(old_sequence)) == 4
    print('Unfixed loading backgrounds:', old_sequence)
    # Try both presenter advancing and producer overtaking/dropping pending frames.
    for slots in ([0,1,2,3]*4, [0,1,2,1,2,3,0,3,0,1]):
        for i,t in enumerate(textures): fill(t,20+i*30)
        current = slots[0]; expected = read(textures[current])
        for nxt in slots[1:]:
            bridge.preserve(textures[current],textures[nxt],W,H)
            assert read(textures[nxt]) == expected, 'stale loading background'
            # Loading GUI writes only one pixel; rest must remain the latest image.
            pixel = (C.c_ubyte*4)(240,241,242,255)
            upload(T,0,0,0,1,1,RGBA,BYTE,pixel)
            expected = bytes(pixel)+expected[4:]
            assert read(textures[nxt]) == expected
            current = nxt
        # Full gameplay render replaces the retained frame normally.
        fill(textures[current],199)
        nxt=(current+1)%4
        bridge.preserve(textures[current],textures[nxt],W,H)
        assert read(textures[nxt]) == bytes([199])*(W*H*4)
    # Copy must not disturb framebuffer, texture, scissor or colour-mask state.
    tex(T,textures[3])
    gl('glEnable',None,U)(0x0C11)
    gl('glScissor',None,I,I,I,I)(0,0,1,1)
    gl('glColorMask',None,C.c_ubyte,C.c_ubyte,C.c_ubyte,C.c_ubyte)(0,0,0,0)
    query = gl('glGetIntegerv',None,U,P)
    def state():
        result=[]
        for name in (0x8069,0x8CA6,0x8CAA,0x0C11):
            x=I();query(name,C.byref(x));result.append(x.value)
        return result
    before=state()
    bridge.preserve(textures[0],textures[1],W,H)
    assert state()==before
    assert read(textures[0])==read(textures[1]), 'scissor/mask affected copy'
    assert error()==0
    print('PASS: retained loading backgrounds, partial GUI draws, mailbox slot patterns, full-render resume, GL state and errors.')

# Verify the integration orders the destination wait before copy and attachment.
s=(root/'src/fg/framegen.cpp').read_text()
start=s.index('const int completedIdx = S.gameCur;')
a=s.index('glWaitSync_(reuseFence',start)
b=s.index('fg::PreserveFrameContents(',a)
c=s.index('attachGameTexture(S.gameCur)',b)
assert start<a<b<c
print('PASS: integration preserves destination GPU fence -> copy -> attachment ordering.')
