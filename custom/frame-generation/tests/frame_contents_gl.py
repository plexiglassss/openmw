"""Headless Mesa regression for the production framebuffer handoff helpers.
Requires Linux libEGL, libGL, C++ compiler and GL/gl.h (headers_dir argument).
No OpenMW/NVIDIA runtime claim: simulates loading draws and framebuffer copies.
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
pa = (I*5)(0x3057,7,0x3056,5,0x3038)
surface = egl('eglCreatePbufferSurface',P,[P,P,P])(display,config,pa)
assert surface
assert egl('eglMakeCurrent', U, [P, P, P, P])(display, surface, surface, context)

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
decltype(glBindFramebuffer_) glBindFramebuffer_ = nullptr;
decltype(glBlitFramebuffer_) glBlitFramebuffer_ = nullptr;
extern "C" void init(void* copy, void* bind, void* blit) {
    glCopyImageSubData_ = reinterpret_cast<decltype(glCopyImageSubData_)>(copy);
    glBindFramebuffer_ = reinterpret_cast<decltype(glBindFramebuffer_)>(bind);
    glBlitFramebuffer_ = reinterpret_cast<decltype(glBlitFramebuffer_)>(blit);
}
extern "C" void seed(unsigned src, unsigned buffer, unsigned dst, int w, int h) {
    fg::SeedFrameContents(src,buffer,dst,w,h);
}
extern "C" void redirect(unsigned dst) {
    GLint draw = 0, read = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    fg::RedirectDefaultFramebufferBindings(dst,draw,read);
}
extern "C" void preserve(unsigned src, unsigned dst, int w, int h) { fg::PreserveFrameContents(src,dst,w,h); }
''')
    subprocess.run(['g++', '-std=c++20', '-shared', '-fPIC', '-I'+str(root), '-I'+sys.argv[1], str(p/'bridge.cpp'), '-l:libGL.so.1', '-o', str(p/'bridge.so')], check=True)
    bridge = C.CDLL(str(p/'bridge.so')); bridge.init.argtypes = [P,P,P]
    bridge.init(getproc(b'glCopyImageSubData'),getproc(b'glBindFramebuffer'),getproc(b'glBlitFramebuffer'))
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

    # First redirected frame: copy a real rendered image before a partial draw.
    bridge.seed.argtypes = [U,U,U,I,I]
    fb = (U*3)(); gl('glGenFramebuffers',None,I,P)(3,fb)
    bind = gl('glBindFramebuffer',None,U,U)
    attach = gl('glFramebufferTexture2D',None,U,U,U,U,I)
    enable = gl('glEnable',None,U); disable = gl('glDisable',None,U)
    colorMask = gl('glColorMask',None,C.c_ubyte,C.c_ubyte,C.c_ubyte,C.c_ubyte)
    FBO, READ, DRAW, COLOR = 0x8D40, 0x8CA8, 0x8CA9, 0x8CE0
    bind(FBO,fb[0]);attach(FBO,COLOR,T,textures[0],0)
    bind(FBO,fb[1]);attach(FBO,COLOR,T,textures[1],0)
    rb=U();gl('glGenRenderbuffers',None,I,P)(1,C.byref(rb))
    gl('glBindRenderbuffer',None,U,U)(0x8D41,rb)
    gl('glRenderbufferStorageMultisample',None,U,I,U,I,I)(0x8D41,4,0x8058,W,H)
    bind(FBO,fb[2]);gl('glFramebufferRenderbuffer',None,U,U,U,U)(FBO,COLOR,0x8D41,rb)
    disable(0x0C11);colorMask(1,1,1,1)
    gl('glClearColor',None,C.c_float,C.c_float,C.c_float,C.c_float)(0.25,0.5,0.75,1)
    gl('glClear',None,U)(0x4000)
    fill(textures[0],83)
    for source in (fb[0],fb[2]):
        fill(textures[1],0)
        bind(READ,fb[1]);bind(DRAW,fb[0])
        enable(0x0C11);enable(0x8DB9);colorMask(0,0,0,0)
        before=state()
        bridge.seed(source,COLOR,fb[1],W,H)
        assert state()==before and gl('glIsEnabled',C.c_ubyte,U)(0x8DB9)
        pixels=read(textures[1])
        if source==fb[0]: assert pixels==read(textures[0])
        else:
            for offset in range(0,len(pixels),4):
                assert all(abs(x-y)<=1 for x,y in zip(pixels[offset:offset+4],(64,128,191,255)))
        assert error()==0
    print('PASS: first-frame seeding, full image despite scissor/masks, MSAA resolve, restored GL state.')
    # Exercise an actual EGL default framebuffer as well as FBO/MSAA sources.
    bind(FBO,0);disable(0x0C11);disable(0x8DB9);colorMask(1,1,1,1)
    double=I();query(0x0C32,C.byref(double))
    buffer=0x0405 if double.value else 0x0404  # BACK / FRONT
    gl('glDrawBuffer',None,U)(buffer)
    gl('glClearColor',None,C.c_float,C.c_float,C.c_float,C.c_float)(1,0,0,1)
    gl('glClear',None,U)(0x4000)
    bridge.seed(0,buffer,fb[1],W,H)
    pixels=read(textures[1])
    assert pixels==bytes([255,0,0,255])*(W*H)
    assert error()==0
    print('PASS: native default framebuffer seeds first redirected texture.')

    # Startup GUI cameras can keep drawing without ever rebinding framebuffer 0.
    # Negative control: seeding alone captures the first caption, but the next
    # draw changes the native image while the submitted texture stays stale.
    def integer(name):
        value=I();query(name,C.byref(value));return value.value
    def bindings():
        return integer(0x8CA6), integer(0x8CAA)
    clearColor=gl('glClearColor',None,C.c_float,C.c_float,C.c_float,C.c_float)
    clear=gl('glClear',None,U)
    readPixels=gl('glReadPixels',None,I,I,I,I,U,U,P)
    scissor=gl('glScissor',None,I,I,I,I)
    drawBuffer=gl('glDrawBuffer',None,U)
    readBuffer=gl('glReadBuffer',None,U)
    assert bindings()==(0,0), 'seeding must restore the original bindings'
    readBuffer(buffer)
    clearColor(0,1,0,1);clear(0x4000)
    native=(C.c_ubyte*(W*H*4))()
    readPixels(0,0,W,H,RGBA,BYTE,native)
    assert bytes(native)==bytes([0,255,0,255])*(W*H)
    expected=bytes([255,0,0,255])*(W*H)
    assert read(textures[1])==expected, 'negative control must retain the stale first caption'
    assert error()==0
    print('PASS: seed-only negative control reproduces a stale submitted image while native loading draws advance.')

    bridge.redirect.argtypes=[U]
    bridge.redirect(fb[1])
    assert bindings()==(fb[1],fb[1])
    enable(0x0C11)
    current=1
    # No framebuffer bind occurs in this loop. The game draws a changing GUI
    # region; FG preserves the frame and rotates the attachment at each swap.
    for step,nxt in enumerate([2,3,0,1,2,3,0,1,3,0,2,1]):
        x,y=step%W,(step//W)%H
        pixel=bytes((16+step*13,47+step*7,240-step*11,255))
        scissor(x,y,1,1)
        clearColor(*(component/255 for component in pixel));clear(0x4000)
        offset=(y*W+x)*4
        expected=expected[:offset]+pixel+expected[offset+4:]
        assert read(textures[current])==expected, 'loading draw missed the submitted texture'
        readPixels(0,0,W,H,RGBA,BYTE,native)
        assert bytes(native)==expected, 'loading readback missed the redirected framebuffer'
        bridge.preserve(textures[current],textures[nxt],W,H)
        attach(FBO,COLOR,T,textures[nxt],0)
        assert read(textures[nxt])==expected, 'rotation lost the latest loading draw'
        assert bindings()==(fb[1],fb[1])
        assert error()==0
        current=nxt
    disable(0x0C11)
    clearColor(0,0,1,1);clear(0x4000)
    assert read(textures[current])==bytes([0,0,255,255])*(W*H)
    assert error()==0
    print('PASS: 12 partial loading draws update all four ring slots without framebuffer rebinds; full-frame drawing resumes.')

    # Read and draw bindings are independent. Do not replace an engine-owned
    # FBO or change its buffer selection when only the other binding is zero.
    bind(DRAW,fb[0]);drawBuffer(0)  # NONE on an application-owned draw FBO
    bind(READ,fb[2]);readBuffer(0)  # NONE on a different application read FBO
    for oldDraw,oldRead in ((0,0),(fb[0],0),(0,fb[2]),(fb[0],fb[2])):
        bind(DRAW,oldDraw);bind(READ,oldRead)
        bridge.redirect(fb[1])
        assert bindings()==(oldDraw or fb[1],oldRead or fb[1]), 'nondefault binding was replaced'
        assert integer(0x0C01)==(0 if oldDraw else COLOR), 'draw-buffer selection changed'
        assert integer(0x0C02)==(0 if oldRead else COLOR), 'read-buffer selection changed'
        assert error()==0
    print('PASS: all four default/user read-draw binding combinations and application buffer selections are preserved.')


# Verify the integration orders the destination wait before copy and attachment.
s=(root/'src/fg/framegen.cpp').read_text()
start=s.index('const int completedIdx = S.gameCur;')
a=s.index('glWaitSync_(reuseFence',start)
b=s.index('fg::PreserveFrameContents(',a)
c=s.index('attachGameTexture(S.gameCur)',b)
assert start<a<b<c
print('PASS: integration preserves destination GPU fence -> copy -> attachment ordering.')

arm=s.index('if (!S.redirect.load())')
a=s.index('fg::SeedFrameContents(0, GL_BACK',arm)
b=s.index('real_glGetIntegerv(GL_FRAMEBUFFER_BINDING',a)
c=s.index('real_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING',a)
d=s.index('fg::RedirectDefaultFramebufferBindings(',a)
e=s.index('S.redirect = true',a)
assert arm<a<b<d<e and a<c<d
print('PASS: native back buffer seeded, raw bindings queried and redirected before the redirect flag is enabled.')
