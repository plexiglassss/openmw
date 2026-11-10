#!/usr/bin/env python3
"""Compile/link actual OpenMW shaders and compare profile rendering with original global settings.

Uses software Mesa through surfaceless EGL, generated geometry/textures, and no game assets.
The template expansion below supports the actual includes, links, and foreach directives used here;
this is independent of OpenMW's C++ ShaderManager, so its C++ tests remain a separate check.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
import re
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument("--baseline", default="8bd0d90b2727b8d654288d5c42e45a3edefc7146")
parser.add_argument("--output", type=Path, default=Path("horror-shader-render-tests.json"))
args = parser.parse_args()
ROOT = args.repository.resolve()
SHADERS = ROOT / "files/shaders"
BASE = args.baseline
CHANGED = {"compatibility/objects.vert", "compatibility/objects.frag", "compatibility/bs/default.frag", "compatibility/bs/default.vert", "lib/light/clamp.glsl"}
OUT = args.output.resolve()
OUT.parent.mkdir(parents=True, exist_ok=True)
COUNTS = {"programs_linked": 0, "shaders_compiled": 0, "render_passes": 0}
E = C.CDLL("libEGL.so.1")
G = C.CDLL("libGL.so.1")


def function(lib, name, result, *args):
    f = getattr(lib, name)
    f.restype = result
    f.argtypes = args
    return f


get_proc = function(E, "eglGetProcAddress", C.c_void_p, C.c_char_p)
get_display = C.CFUNCTYPE(C.c_void_p, C.c_uint, C.c_void_p, C.POINTER(C.c_int))(
    get_proc(b"eglGetPlatformDisplayEXT"))
display = get_display(0x31DD, None, None)
major, minor = C.c_int(), C.c_int()
assert function(E, "eglInitialize", C.c_uint, C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int))(
    display, C.byref(major), C.byref(minor))
assert function(E, "eglBindAPI", C.c_uint, C.c_uint)(0x30A2)
config, count = C.c_void_p(), C.c_int()
assert function(E, "eglChooseConfig", C.c_uint, C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_void_p), C.c_int,
                C.POINTER(C.c_int))(display, (C.c_int * 11)(0x3033, 1, 0x3040, 8, 0x3024, 8, 0x3023, 8,
                0x3022, 8, 0x3038), C.byref(config), 1, C.byref(count)) and count.value
surface = function(E, "eglCreatePbufferSurface", C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_int))(
    display, config, (C.c_int * 5)(0x3057, 16, 0x3056, 16, 0x3038))
context = function(E, "eglCreateContext", C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_int))(
    display, config, None, (C.c_int * 7)(0x3098, 4, 0x30FB, 5, 0x30FD, 2, 0x3038))
assert function(E, "eglMakeCurrent", C.c_uint, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)(
    display, surface, surface, context)

get_string = function(G, "glGetString", C.c_char_p, C.c_uint)
create_shader = function(G, "glCreateShader", C.c_uint, C.c_uint)
shader_source = function(G, "glShaderSource", None, C.c_uint, C.c_int, C.POINTER(C.c_char_p), C.POINTER(C.c_int))
compile_shader = function(G, "glCompileShader", None, C.c_uint)
shader_iv = function(G, "glGetShaderiv", None, C.c_uint, C.c_uint, C.POINTER(C.c_int))
shader_log = function(G, "glGetShaderInfoLog", None, C.c_uint, C.c_int, C.POINTER(C.c_int), C.c_char_p)
create_program = function(G, "glCreateProgram", C.c_uint)
attach_shader = function(G, "glAttachShader", None, C.c_uint, C.c_uint)
bind_attribute = function(G, "glBindAttribLocation", None, C.c_uint, C.c_uint, C.c_char_p)
vertex_attribute = function(G, "glVertexAttrib4f", None, C.c_uint, C.c_float, C.c_float, C.c_float, C.c_float)
link_program = function(G, "glLinkProgram", None, C.c_uint)
program_iv = function(G, "glGetProgramiv", None, C.c_uint, C.c_uint, C.POINTER(C.c_int))
program_log = function(G, "glGetProgramInfoLog", None, C.c_uint, C.c_int, C.POINTER(C.c_int), C.c_char_p)
use_program = function(G, "glUseProgram", None, C.c_uint)
delete_program = function(G, "glDeleteProgram", None, C.c_uint)
delete_shader = function(G, "glDeleteShader", None, C.c_uint)
location = function(G, "glGetUniformLocation", C.c_int, C.c_uint, C.c_char_p)
uniform_i = function(G, "glUniform1i", None, C.c_int, C.c_int)
uniform_1 = function(G, "glUniform1f", None, C.c_int, C.c_float)
uniform_2 = function(G, "glUniform2f", None, C.c_int, C.c_float, C.c_float)
uniform_3 = function(G, "glUniform3f", None, C.c_int, C.c_float, C.c_float, C.c_float)
uniform_4 = function(G, "glUniform4f", None, C.c_int, C.c_float, C.c_float, C.c_float, C.c_float)
uniform_matrix = function(G, "glUniformMatrix4fv", None, C.c_int, C.c_int, C.c_ubyte, C.POINTER(C.c_float))
gen_textures = function(G, "glGenTextures", None, C.c_int, C.POINTER(C.c_uint))
bind_texture = function(G, "glBindTexture", None, C.c_uint, C.c_uint)
active_texture = function(G, "glActiveTexture", None, C.c_uint)
texture_image = function(G, "glTexImage2D", None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int,
                         C.c_uint, C.c_uint, C.c_void_p)
texture_parameter = function(G, "glTexParameteri", None, C.c_uint, C.c_uint, C.c_int)
gen_fbo = function(G, "glGenFramebuffers", None, C.c_int, C.POINTER(C.c_uint))
bind_fbo = function(G, "glBindFramebuffer", None, C.c_uint, C.c_uint)
attach_texture = function(G, "glFramebufferTexture2D", None, C.c_uint, C.c_uint, C.c_uint, C.c_uint, C.c_int)
check_fbo = function(G, "glCheckFramebufferStatus", C.c_uint, C.c_uint)
draw_buffers = function(G, "glDrawBuffers", None, C.c_int, C.POINTER(C.c_uint))
read_pixels = function(G, "glReadPixels", None, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p)
read_buffer = function(G, "glReadBuffer", None, C.c_uint)
gen_buffers = function(G, "glGenBuffers", None, C.c_int, C.POINTER(C.c_uint))
bind_buffer = function(G, "glBindBuffer", None, C.c_uint, C.c_uint)
buffer_data = function(G, "glBufferData", None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint)
bind_buffer_base = function(G, "glBindBufferBase", None, C.c_uint, C.c_uint, C.c_uint)
begin = function(G, "glBegin", None, C.c_uint)
end = function(G, "glEnd", None)
normal = function(G, "glNormal3f", None, C.c_float, C.c_float, C.c_float)
vertex = function(G, "glVertex3f", None, C.c_float, C.c_float, C.c_float)
texcoord = function(G, "glMultiTexCoord2f", None, C.c_uint, C.c_float, C.c_float)
tangent = function(G, "glMultiTexCoord4f", None, C.c_uint, C.c_float, C.c_float, C.c_float, C.c_float)
viewport = function(G, "glViewport", None, C.c_int, C.c_int, C.c_int, C.c_int)
clear = function(G, "glClear", None, C.c_uint)
gl_error = function(G, "glGetError", C.c_uint)

defaults = dict(forcePPL=1, autoNormalMap=0, autoSpecularMap=0, clamp=0, preLightEnv=0,
                radialFog=0, exponentialFog=0, reverseZ=0, waterRefraction=0, classicFalloff=0,
                skyBlending=0, disableNormals=1, useGPUShader4=0, useOVR_multiview=0,
                distorionRTRatio=1, numViews=1, particle=0, particlePointLighting=1,
                parallax=0, diffuseParallax=0, reconstructNormalZ=0, alphaFunc=519,
                alphaToCoverage=0, adjustCoverage=0, additiveBlending=0, softParticles=0,
                particleOcclusion=0, lightingMethodClustered=0, maxLights=8, simpleLighting=0,
                shadows_enabled=0, shadow_texture_unit_list="", useShadowDebugOverlay=0,
                perspectiveShadowMaps=0, disableNormalOffsetShadows=0, shadowNormalOffset=0.0,
                limitShadowMapDistance=0, classicNormalMap=0, classicNormalMapUV=0, classicNormalMapUnit=0,
                classicSpecularMap=0, classicSpecularMapUV=0, classicSpecularMapUnit=0, classicParallax=0,
                classicReconstructNormalZ=0, classicTangent=0, classicInteriorAvailable=1)
for name in ("diffuseMap", "normalMap", "specularMap", "emissiveMap", "darkMap", "detailMap", "envMap",
             "decalMap", "bumpMap", "glossMap"):
    defaults[name] = int(name == "diffuseMap")
    defaults[name + "UV"] = {"normalMap": 1, "specularMap": 2, "envMap": 3}.get(name, 0)

baseline = {name: subprocess.check_output(["git", "show", f"{BASE}:files/shaders/{name}"], cwd=ROOT).decode()
            for name in CHANGED}
sources_used = {}


def expand_file(name, defines, original=False):
    path = SHADERS / name
    text = baseline[name] if original and name in baseline else path.read_text()
    sources_used[("base/" if original else "candidate/") + name] = hashlib.sha256(text.encode()).hexdigest()
    def include(match):
        child = match.group(1)
        if not (SHADERS / child).is_file():
            child = (Path(name).parent / child).as_posix()
        return expand_file(child, defines, original)
    text = re.sub(r'^\s*#include "([^"]+)".*$', include, text, flags=re.M)
    def foreach(match):
        variable, values, body = match.groups()
        values = str(defines[values]) if values.startswith("shadow_") else values
        return "".join(body.replace("@" + variable, item) for item in values.split(",") if item)
    text = re.sub(r'@foreach\s+(\w+)\s+@(\w+)\s*\n(.*?)@endforeach', foreach, text, flags=re.S)
    return re.sub(r'@(\w+)', lambda m: str(defines[m.group(1)]) if m.group(1) in defines else m.group(), text)


def make_program(defines, original=False, prefix="compatibility/objects"):
    program = create_program()
    attached = []
    for extension, stage in (("vert", 0x8B31), ("frag", 0x8B30)):
        pending = [prefix + "." + extension]
        seen = set()
        while pending:
            name = pending.pop()
            if name in seen:
                continue
            seen.add(name)
            source = expand_file(name, defines, original)
            def link(match):
                child, condition = match.groups()
                if condition is None or eval(condition.replace("!", " not ").replace("&&", " and ")
                                             .replace("||", " or "), {"__builtins__": {}}):
                    pending.append(child)
                return ""
            source = re.sub(r'^\s*@link "([^"]+)"(?: if (.*))?$', link, source, flags=re.M)
            if "@" in source:
                raise RuntimeError((name, sorted(set(re.findall(r'@\w+', source)))))
            shader = create_shader(stage)
            value = C.c_char_p(source.encode())
            shader_source(shader, 1, C.byref(value), None)
            compile_shader(shader)
            status = C.c_int()
            shader_iv(shader, 0x8B81, C.byref(status))
            if not status.value:
                log = C.create_string_buffer(16384)
                shader_log(shader, len(log), None, log)
                (OUT.parent / "failed_shader.glsl").write_text(source)
                raise RuntimeError(f"{name} original={original}: {log.value.decode()}")
            COUNTS["shaders_compiled"] += 1
            attach_shader(program, shader)
            attached.append(shader)
    bind_attribute(program, 6, b"classicTangent")
    link_program(program)
    status = C.c_int()
    program_iv(program, 0x8B82, C.byref(status))
    for shader in attached:
        delete_shader(shader)
    if not status.value:
        log = C.create_string_buffer(16384)
        program_log(program, len(log), None, log)
        raise RuntimeError(f"link original={original}: {log.value.decode()}")
    COUNTS["programs_linked"] += 1
    return program


textures = (C.c_uint * 7)()
gen_textures(7, textures)
for unit in range(5):
    active_texture(0x84C0 + unit)
    bind_texture(0x0DE1, textures[unit])
    pixels = []
    for y in range(4):
        for x in range(4):
            q = ((x + y) % 2) * 0.25
            pixels.extend(([0.15 + q, 0.25 + q, 0.35 + q, 0.8] if unit == 0 else
                           [0.7 + q * 0.3, 0.3, 0.9, 0.1 + q] if unit == 1 else
                           [0.15 + q, 0.2, 0.25, 0.1 + q] if unit == 2 else
                           [0.35, 0.7 + q * 0.2, 0.85, 0.8 - q] if unit == 3 else
                           [0.45, 0.15 + q, 0.1, 0.3 + q]))
    texture_image(0x0DE1, 0, 0x8814, 4, 4, 0, 0x1908, 0x1406, (C.c_float * len(pixels))(*pixels))
    texture_parameter(0x0DE1, 0x2801, 0x2601)
    texture_parameter(0x0DE1, 0x2800, 0x2601)
fbo = C.c_uint()
gen_fbo(1, C.byref(fbo))
bind_fbo(0x8D40, fbo)
active_texture(0x84CF)
for attachment in range(2):
    bind_texture(0x0DE1, textures[5 + attachment])
    texture_image(0x0DE1, 0, 0x8814, 16, 16, 0, 0x1908, 0x1406, None)
    attach_texture(0x8D40, 0x8CE0 + attachment, 0x0DE1, textures[5 + attachment], 0)
assert check_fbo(0x8D40) == 0x8CD5
draw_buffers(2, (C.c_uint * 2)(0x8CE0, 0x8CE1))
viewport(0, 0, 16, 16)
identity = (C.c_float * 16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
light_rows = [[-.3, -.2, .7, 1, .05, .02, .01, .0, 2, 1.8, 1.6, 0, .4, .3, .2, 10],
              [.2, .2, .5, 1, 0, 0, 0, 0, -1.3, -1.2, -1.1, 0, 0, 0, 0, 10]]
buffers = (C.c_uint * 3)()
gen_buffers(3, buffers)
points = []
for row in light_rows:
    points.extend(row[:3] + [1] + row[8:11] + [0] + row[4:7] + [0] + row[12:15] + [0] + [1, 0, 0, 10])
for index, binding, data in ((0, 2, (C.c_float * len(points))(*points)),
                            (1, 3, (C.c_uint * 2)(0, 2)), (2, 4, (C.c_uint * 2)(0, 1))):
    bind_buffer(0x90D2, buffers[index])
    buffer_data(0x90D2, C.sizeof(data), data, 0x88E4)
    bind_buffer_base(0x90D2, binding, buffers[index])


def render(program, profile, defines, layout=None, reference=False):
    COUNTS["render_passes"] += 1
    layout = layout or defines
    normal_fallback = layout["classicNormalMap"] and (layout["autoNormalMap"]
        or layout["classicNormalMapUnit"] != layout["normalMapUV"])
    specular_fallback = layout["classicSpecularMap"] and (layout["autoSpecularMap"]
        or layout["classicSpecularMapUnit"] != layout["specularMapUV"])
    bindings = {0: 0}
    if layout["normalMap"]:
        bindings[layout["normalMapUV"]] = 1
    if layout["specularMap"]:
        bindings[layout["specularMapUV"]] = 2
    if layout["classicNormalMap"]:
        bindings[layout["classicNormalMapUnit"]] = 3 if normal_fallback else 1
    if layout["classicSpecularMap"]:
        bindings[layout["classicSpecularMapUnit"]] = 4 if specular_fallback else 2
    for unit, texture in bindings.items():
        active_texture(0x84C0 + unit)
        bind_texture(0x0DE1, textures[texture])
    use_program(program)
    def loc(name): return location(program, name.encode())
    for name, value in dict(classicInteriorLighting=profile, PointLightCount=2, diffuseMap=0,
                           normalMap=layout["classicNormalMapUnit"] if reference else layout["normalMapUV"],
                           specularMap=layout["classicSpecularMapUnit"] if reference else layout["specularMapUV"],
                           classicNormalMap=layout["classicNormalMapUnit"],
                           classicSpecularMap=layout["classicSpecularMapUnit"], envMap=0, **{"material.vertexColorMode": 0}).items():
        uniform_i(loc(name), value)
    for name, value in {"alpha": 1, "actorFade": 1, "near": .1, "far": 10, "fog.start": 100,
                        "fog.end": 200, "fog.depth": -1, "material.shininess": 20,
                        "material.emissiveMult": 1, "material.specStrength": .3, "clusterFar": 100}.items():
        uniform_1(loc(name), value)
    uniform_2(loc("screenRes"), 16, 16)
    uniform_3(loc("gridSize"), 1, 1, 1)
    for name, values in {"material.diffuse": [.7, .6, .5, 1], "material.ambient": [.3, .25, .2, 1],
                         "material.specular": [.2, .3, .4, 1], "material.emission": [.01, .02, .03, 1],
                         "sun.position": [.2, -.3, 1, 0], "sun.diffuse": [.5, .4, .3, 1],
                         "sun.ambient": [.1, .1, .1, 1], "sun.specular": [.2, .2, .2, 1],
                         "envMapColor": [.2, .3, .4, 1]}.items():
        uniform_4(loc(name), *values)
    for name in ("projectionMatrix", "osg_ViewMatrixInverse", *("texMat" + str(i) for i in range(7))):
        uniform_matrix(loc(name), 1, 0, identity)
    uniform_matrix(loc("LightBuffer[0]"), 2, 0, (C.c_float * 32)(*sum(light_rows, [])))
    clear(0x00004000)
    vertex_attribute(6, 0, 1, 0, -1)
    begin(0x0004)
    for (x, y), (u, v), (nx, ny, nz) in [((-.9, -.9), (0, 0), (.1, 0, .995)),
                                         ((.9, -.9), (1, 0), (-.1, .2, .975)),
                                         ((0, .9), (.5, 1), (0, -.2, .98))]:
        for unit, uv in enumerate(((u, v), (u, v), (v, 1-u), (1-u, 1-v), (v, u), (1-v, u), (u, 1-v))):
            texcoord(0x84C0 + unit, *uv)
        tangent(0x84C7, *( (0, 1, 0, -1) if reference and layout["classicTangent"] else (1, 0, 0, 1)))
        normal(nx, ny, nz)
        vertex(x, y, -.5)
    end()
    result = []
    for attachment in range(1 + int(not defines["disableNormals"])):
        read_buffer(0x8CE0 + attachment)
        pixels = (C.c_float * (16 * 16 * 4))()
        read_pixels(0, 0, 16, 16, 0x1908, 0x1406, pixels)
        # Normals RT only writes RGB; its alpha channel is intentionally unspecified.
        result.extend(value for i, value in enumerate(pixels) if attachment == 0 or i % 4 != 3)
    error = gl_error()
    assert not error, hex(error)
    assert all(math.isfinite(v) for v in result)
    return result


cases = [
    ("force_pixel_only", {}),
    ("base_vertex", dict(forcePPL=0)),
    ("automatic_normal", dict(normalMap=1, autoNormalMap=1)),
    ("automatic_specular", dict(specularMap=1, autoSpecularMap=1)),
    ("both_automatic", dict(normalMap=1, autoNormalMap=1, specularMap=1, autoSpecularMap=1)),
    ("automatic_height", dict(normalMap=1, autoNormalMap=1, parallax=1)),
    ("automatic_height_diffuse_fallback", dict(normalMap=1, autoNormalMap=1, parallax=1, diffuseParallax=1)),
    ("explicit_normal", dict(normalMap=1)),
    ("explicit_specular", dict(specularMap=1)),
    ("explicit_height", dict(normalMap=1, parallax=1)),
    ("explicit_normal_auto_specular", dict(normalMap=1, specularMap=1, autoSpecularMap=1)),
    ("auto_normal_explicit_specular", dict(normalMap=1, autoNormalMap=1, specularMap=1)),
    ("auto_normal_env", dict(normalMap=1, autoNormalMap=1, envMap=1)),
    ("auto_normal_normals_target", dict(normalMap=1, autoNormalMap=1, disableNormals=0)),
    ("base_already_clamped", dict(normalMap=1, autoNormalMap=1, clamp=1)),
    ("particle_vertex", dict(forcePPL=0, particle=1, particlePointLighting=0)),
]
cases = [("compatibility/objects", name, values) for name, values in cases] + [
    ("compatibility/bs/default", "bs_automatic_normal", dict(normalMap=1, autoNormalMap=1)),
    ("compatibility/bs/default", "bs_explicit_normal", dict(normalMap=1)),
    ("compatibility/bs/default", "bs_no_normal", {}),
]
cases += [
    ("compatibility/objects", "higher_auto_normal_explicit_lower_uv", dict(normalMap=1, autoNormalMap=1,
        normalMapUV=4, parallax=1, classicNormalMap=1, classicNormalMapUV=1, classicNormalMapUnit=1,
        classicParallax=0, classicTangent=1)),
    ("compatibility/objects", "higher_auto_specular_explicit_lower_uv", dict(specularMap=1, autoSpecularMap=1,
        specularMapUV=4, classicSpecularMap=1, classicSpecularMapUV=2, classicSpecularMapUnit=2)),
    ("compatibility/objects", "physical_normal_shadow_same_uv", dict(normalMap=1, autoNormalMap=1,
        classicNormalMap=1, classicNormalMapUV=1, classicNormalMapUnit=3)),
    ("compatibility/objects", "physical_specular_shadow_same_uv", dict(specularMap=1, autoSpecularMap=1,
        classicSpecularMap=1, classicSpecularMapUV=2, classicSpecularMapUnit=4)),
    ("compatibility/objects", "explicit_parent_tangent_with_auto_lower_uv", dict(normalMap=1, normalMapUV=4,
        classicTangent=1)),
    ("compatibility/objects", "both_inherited_explicit_maps", dict(normalMap=1, autoNormalMap=1,
        normalMapUV=4, specularMap=1, autoSpecularMap=1, specularMapUV=5, classicNormalMap=1,
        classicNormalMapUV=1, classicNormalMapUnit=1, classicSpecularMap=1, classicSpecularMapUV=2,
        classicSpecularMapUnit=2, classicTangent=1)),
    ("compatibility/objects", "inherited_explicit_red_green_normal", dict(normalMap=1, autoNormalMap=1,
        normalMapUV=4, classicNormalMap=1, classicNormalMapUV=1, classicNormalMapUnit=1,
        classicReconstructNormalZ=1, classicTangent=1)),
    ("compatibility/bs/default", "bs_inherited_explicit_normal", dict(normalMap=1, autoNormalMap=1,
        normalMapUV=4, classicNormalMap=1, classicNormalMapUV=1, classicNormalMapUnit=1,
        classicTangent=1)),
]
def material_defines(values, clustered):
    defines = defaults | values | {"lightingMethodClustered": clustered, "useGPUShader4": clustered}
    classic_defaults = dict(
        classicNormalMap=int(bool(defines["normalMap"] and not defines["autoNormalMap"])),
        classicNormalMapUV=defines["normalMapUV"], classicNormalMapUnit=defines["normalMapUV"],
        classicSpecularMap=int(bool(defines["specularMap"] and not defines["autoSpecularMap"])),
        classicSpecularMapUV=defines["specularMapUV"], classicSpecularMapUnit=defines["specularMapUV"],
        classicParallax=defines["parallax"] if not defines["autoNormalMap"] else 0,
        classicReconstructNormalZ=defines["reconstructNormalZ"] if not defines["autoNormalMap"] else 0)
    defines.update(classic_defaults | {k: v for k, v in values.items() if k.startswith("classic")})
    return defines


report = {"base_commit": BASE,
          "git_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT).decode().strip(), "renderer": get_string(0x1F01).decode(), "opengl": get_string(0x1F02).decode(),
          "glsl": get_string(0x8B8C).decode(), "scope": "Actual shader compilation, linking, and synthetic geometry render comparisons. No game installation or Nvidia runtime test.",
          "cases": [], "status": "RUNNING"}
for clustered in (0, 1):
    for prefix, name, values in cases:
        defines = material_defines(values, clustered)
        candidate = make_program(defines, prefix=prefix)
        original = make_program(defines, original=True, prefix=prefix)
        classic = dict(defines, clamp=1, forcePPL=0)
        classic.update(normalMap=defines["classicNormalMap"], normalMapUV=defines["classicNormalMapUV"],
                       specularMap=defines["classicSpecularMap"], specularMapUV=defines["classicSpecularMapUV"],
                       parallax=defines["classicParallax"], reconstructNormalZ=defines["classicReconstructNormalZ"])
        reference = make_program(classic, original=True, prefix=prefix)
        ordinary = render(candidate, 0, defines)
        original_pixels = render(original, 0, defines)
        interior = render(candidate, 1, defines)
        reference_pixels = render(reference, 0, classic, layout=defines, reference=True)
        restored = render(candidate, 0, defines)
        differences = {"outside_vs_original": max(abs(a - b) for a, b in zip(ordinary, original_pixels)),
                       "inside_vs_four_global_settings": max(abs(a - b) for a, b in zip(interior, reference_pixels)),
                       "after_exit_vs_before_entry": max(abs(a - b) for a, b in zip(ordinary, restored))}
        assert max(differences.values()) < 0.00005, (name, clustered, differences)
        ordinary_rgb = [v for i, v in enumerate(ordinary[:1024]) if i % 4 != 3]
        assert max(ordinary_rgb) > 0.01, (name, "empty RGB render")
        profile_delta = max(abs(a - b) for a, b in zip(ordinary[:1024], interior[:1024]))
        if name in ("force_pixel_only", "automatic_normal", "automatic_specular", "both_automatic", "bs_automatic_normal"):
            assert profile_delta > 0.0001, (name, "profile branch had no observable effect")
        report["cases"].append({"name": name, "shader_prefix": prefix, "clustered": bool(clustered),
                                "max_absolute_differences": differences, "profile_activation_max_delta": profile_delta})
        for program in (candidate, original, reference):
            delete_program(program)
        print(name, clustered, differences, flush=True)
report["shadow_compile_cases"] = []
shadow_cases = [
    ("compatibility/objects", "automatic_maps_env", dict(normalMap=1, autoNormalMap=1, specularMap=1,
        autoSpecularMap=1, envMap=1)),
    ("compatibility/objects", "inherited_explicit_maps", dict(normalMap=1, autoNormalMap=1,
        normalMapUV=4, specularMap=1, autoSpecularMap=1, specularMapUV=5, classicNormalMap=1,
        classicNormalMapUV=1, classicNormalMapUnit=1, classicSpecularMap=1, classicSpecularMapUV=2,
        classicSpecularMapUnit=2, classicTangent=1, envMap=1)),
    ("compatibility/bs/default", "inherited_normal", dict(normalMap=1, autoNormalMap=1,
        normalMapUV=4, classicNormalMap=1, classicNormalMapUV=1, classicNormalMapUnit=1,
        classicTangent=1)),
]
for clustered in (0, 1):
    for prefix, name, values in shadow_cases:
        defines = material_defines(values, clustered) | dict(shadows_enabled=1,
            shadow_texture_unit_list="8,9,10", perspectiveShadowMaps=1, shadowNormalOffset=0.1,
            limitShadowMapDistance=1, disableNormals=0)
        classic = dict(defines, clamp=1, forcePPL=0, normalMap=defines["classicNormalMap"],
            normalMapUV=defines["classicNormalMapUV"], specularMap=defines["classicSpecularMap"],
            specularMapUV=defines["classicSpecularMapUV"], parallax=defines["classicParallax"],
            reconstructNormalZ=defines["classicReconstructNormalZ"])
        for variant, original in ((defines, False), (defines, True), (classic, True)):
            delete_program(make_program(variant, original=original, prefix=prefix))
        report["shadow_compile_cases"].append({"name": name, "shader_prefix": prefix,
            "clustered": bool(clustered), "cascade_count": 3, "perspective": True,
            "normals_target": True, "candidate_original_classic_linked": True})
limits = {}
get_integer = function(G, "glGetIntegerv", None, C.c_uint, C.POINTER(C.c_int))
for name, enum in (("varying_floats", 0x8B4B), ("vertex_attributes", 0x8869), ("fragment_texture_units", 0x8872)):
    value = C.c_int()
    get_integer(enum, C.byref(value))
    limits[name] = value.value
report["gl_limits"] = limits
report["status"] = "PASS"
report["executed_total"] = len(report["cases"])
report["material_case_count"] = len(cases)
report["lighting_method_count"] = 2
report["counts"] = COUNTS
report["source_sha256"] = {"files/shaders/" + key.removeprefix("candidate/"): value
                            for key, value in sources_used.items() if key.startswith("candidate/")}
report["baseline_source_sha256"] = {"files/shaders/" + key.removeprefix("base/"): value
                                     for key, value in sources_used.items() if key.startswith("base/")}
OUT.write_text(json.dumps(report, indent=2) + "\n")
print(OUT)
