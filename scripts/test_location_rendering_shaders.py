#!/usr/bin/env python3
"""Compile and link profile-capable OpenMW object shaders with surfaceless Mesa."""
from __future__ import annotations

import argparse
import ctypes as C
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument("--output", type=Path, default=Path("location-rendering-shader-tests.json"))
args = parser.parse_args()
root = args.repository.resolve()
shader_root = root / "files" / "shaders"

# EGL setup (surfaceless Mesa).
E = C.CDLL("libEGL.so.1")
G = C.CDLL("libGL.so.1")

def fn(lib, name, result, *arguments):
    value = getattr(lib, name)
    value.restype = result
    value.argtypes = arguments
    return value

get_proc = fn(E, "eglGetProcAddress", C.c_void_p, C.c_char_p)
get_platform_type = C.CFUNCTYPE(C.c_void_p, C.c_uint, C.c_void_p, C.POINTER(C.c_int))
get_platform = get_platform_type(get_proc(b"eglGetPlatformDisplayEXT"))
if not get_platform:
    raise RuntimeError("EGL_EXT_platform_base is unavailable")
display = get_platform(0x31DD, None, None)  # EGL_PLATFORM_SURFACELESS_MESA
major, minor = C.c_int(), C.c_int()
if not fn(E, "eglInitialize", C.c_uint, C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int))(
    display, C.byref(major), C.byref(minor)
):
    raise RuntimeError("eglInitialize failed")
if not fn(E, "eglBindAPI", C.c_uint, C.c_uint)(0x30A2):  # EGL_OPENGL_API
    raise RuntimeError("eglBindAPI failed")
config, count = C.c_void_p(), C.c_int()
attrs = (C.c_int * 13)(0x3033, 1, 0x3040, 8, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038)
if not fn(E, "eglChooseConfig", C.c_uint, C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_void_p), C.c_int,
          C.POINTER(C.c_int))(display, attrs, C.byref(config), 1, C.byref(count)) or not count.value:
    raise RuntimeError("eglChooseConfig failed")
surface_attrs = (C.c_int * 5)(0x3057, 8, 0x3056, 8, 0x3038)
surface = fn(E, "eglCreatePbufferSurface", C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_int))(
    display, config, surface_attrs)
context_attrs = (C.c_int * 1)(0x3038)
context = fn(E, "eglCreateContext", C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_int))(
    display, config, None, context_attrs)
if not surface or not context:
    raise RuntimeError("EGL surface/context creation failed")
if not fn(E, "eglMakeCurrent", C.c_uint, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)(
    display, surface, surface, context
):
    raise RuntimeError("eglMakeCurrent failed")

create_shader = fn(G, "glCreateShader", C.c_uint, C.c_uint)
shader_source = fn(G, "glShaderSource", None, C.c_uint, C.c_int, C.POINTER(C.c_char_p), C.POINTER(C.c_int))
compile_shader = fn(G, "glCompileShader", None, C.c_uint)
shader_iv = fn(G, "glGetShaderiv", None, C.c_uint, C.c_uint, C.POINTER(C.c_int))
shader_log = fn(G, "glGetShaderInfoLog", None, C.c_uint, C.c_int, C.POINTER(C.c_int), C.c_char_p)
create_program = fn(G, "glCreateProgram", C.c_uint)
attach_shader = fn(G, "glAttachShader", None, C.c_uint, C.c_uint)
bind_attribute = fn(G, "glBindAttribLocation", None, C.c_uint, C.c_uint, C.c_char_p)
link_program = fn(G, "glLinkProgram", None, C.c_uint)
program_iv = fn(G, "glGetProgramiv", None, C.c_uint, C.c_uint, C.POINTER(C.c_int))
program_log = fn(G, "glGetProgramInfoLog", None, C.c_uint, C.c_int, C.POINTER(C.c_int), C.c_char_p)
delete_shader = fn(G, "glDeleteShader", None, C.c_uint)
delete_program = fn(G, "glDeleteProgram", None, C.c_uint)
get_string = fn(G, "glGetString", C.c_char_p, C.c_uint)

ROOT_PREFIXES = ("lib/", "compatibility/", "core/")
include_re = re.compile(r'^\s*#include\s+"([^"]+)"\s*$', re.MULTILINE)
token_re = re.compile(r"@([A-Za-z_][A-Za-z0-9_]*)")
link_re = re.compile(r'^\s*\$link\s+"([^"]+)"(?:\s+if\s+(!)?\s*([^\s]+))?\s*$', re.MULTILINE)
raw_link_re = re.compile(r'^\s*@link\s+"([^"]+)"', re.MULTILINE)
foreach_start_re = re.compile(r'^\s*\$foreach\s+([A-Za-z_][A-Za-z0-9_]*)\s*([^\r\n]*)$', re.MULTILINE)
foreach_end_re = re.compile(r'^\s*\$endforeach\s*$', re.MULTILINE)


def resolve_include(current: str, include: str) -> str:
    if include.startswith(ROOT_PREFIXES):
        return include
    return str((Path(current).parent / include).as_posix())


def expand_includes(name: str, stack: tuple[str, ...] = ()) -> str:
    if name in stack:
        raise RuntimeError("cyclic shader include: " + " -> ".join(stack + (name,)))
    path = shader_root / name
    source = path.read_text(encoding="utf-8").replace("\r\n", "\n")

    def replace(match: re.Match[str]) -> str:
        child = resolve_include(name, match.group(1))
        return expand_includes(child, stack + (name,))

    return include_re.sub(replace, source)


def replace_defines(source: str, defines: dict[str, str]) -> str:
    iterators: list[str] = []
    output: list[str] = []
    for line in source.splitlines(keepends=True):
        stripped = line.lstrip()
        if stripped.startswith("@foreach"):
            match = re.match(r'(\s*)@foreach\s+([A-Za-z_][A-Za-z0-9_]*)\s+@([A-Za-z_][A-Za-z0-9_]*)', line)
            if not match:
                raise RuntimeError("invalid @foreach: " + line.rstrip())
            iterator, values = match.group(2), match.group(3)
            iterators.append(iterator)
            line = re.sub(r'@foreach', '$foreach', line, count=1)
            line = line.replace("@" + values, defines.get(values, ""), 1)
        elif stripped.startswith("@endforeach"):
            if not iterators:
                raise RuntimeError("@endforeach without @foreach")
            iterators.pop()
            line = line.replace("@endforeach", "$endforeach", 1)
        elif stripped.startswith("@link"):
            line = line.replace("@link", "$link", 1)

        def token(match: re.Match[str]) -> str:
            name = match.group(1)
            if name in iterators:
                return "$" + name
            if name in ("foreach", "endforeach", "link"):
                return "$" + name
            if name not in defines:
                raise RuntimeError("undefined shader define: " + name)
            return str(defines[name])

        output.append(token_re.sub(token, line))
    if iterators:
        raise RuntimeError("unterminated @foreach")
    return "".join(output)


def expand_foreach(source: str) -> str:
    while True:
        start = foreach_start_re.search(source)
        if not start:
            return source
        depth = 1
        position = start.end()
        end = None
        while depth:
            next_start = foreach_start_re.search(source, position)
            next_end = foreach_end_re.search(source, position)
            if not next_end:
                raise RuntimeError("unterminated $foreach")
            if next_start and next_start.start() < next_end.start():
                depth += 1
                position = next_start.end()
            else:
                depth -= 1
                if depth == 0:
                    end = next_end
                    break
                position = next_end.end()
        assert end is not None
        iterator = start.group(1)
        values = [value.strip() for value in start.group(2).split(",") if value.strip()]
        body = source[start.end():end.start()]
        replacement = "".join(body.replace("$" + iterator, value) for value in values)
        source = source[:start.start()] + replacement + source[end.end():]


def truthy(value: str) -> bool:
    return value not in ("", "0")


def preprocess(name: str, defines: dict[str, str]) -> tuple[str, list[str]]:
    source = expand_includes(name)
    source = replace_defines(source, defines)
    source = expand_foreach(source)
    links: list[str] = []

    def remove_link(match: re.Match[str]) -> str:
        target, negate, condition = match.group(1), match.group(2), match.group(3)
        enabled = True
        if condition is not None:
            enabled = truthy(condition)
            if negate:
                enabled = not enabled
        if enabled and target != name:
            links.append(target)
        return ""

    source = link_re.sub(remove_link, source)
    if "@" in source or "$foreach" in source or "$link" in source:
        raise RuntimeError(f"unexpanded directive in {name}")
    return source, links


def compile_one(shader_type: int, source: str, label: str) -> int:
    shader = create_shader(shader_type)
    encoded = source.encode("utf-8")
    pointer = C.c_char_p(encoded)
    length = C.c_int(len(encoded))
    shader_source(shader, 1, C.byref(pointer), C.byref(length))
    compile_shader(shader)
    status = C.c_int()
    shader_iv(shader, 0x8B81, C.byref(status))  # GL_COMPILE_STATUS
    if not status.value:
        size = C.c_int()
        shader_iv(shader, 0x8B84, C.byref(size))
        buffer = C.create_string_buffer(max(1, size.value))
        shader_log(shader, len(buffer), None, buffer)
        raise RuntimeError(f"{label} compile failed:\n{buffer.value.decode(errors='replace')}\n--- source ---\n{source}")
    return shader


def compile_stage(name: str, shader_type: int, defines: dict[str, str]) -> list[int]:
    sources: list[tuple[str, str]] = []
    pending = [name]
    seen: set[str] = set()
    while pending:
        current = pending.pop(0)
        if current in seen:
            continue
        seen.add(current)
        source, links = preprocess(current, defines)
        sources.append((current, source))
        pending.extend(links)
    return [compile_one(shader_type, source, current) for current, source in sources]


def link_case(prefix: str, defines: dict[str, str]) -> dict[str, object]:
    shaders = compile_stage(prefix + ".vert", 0x8B31, defines)
    shaders += compile_stage(prefix + ".frag", 0x8B30, defines)
    program = create_program()
    try:
        for shader in shaders:
            attach_shader(program, shader)
        bind_attribute(program, 6, b"fallbackTangent")
        link_program(program)
        status = C.c_int()
        program_iv(program, 0x8B82, C.byref(status))
        if not status.value:
            size = C.c_int()
            program_iv(program, 0x8B84, C.byref(size))
            buffer = C.create_string_buffer(max(1, size.value))
            program_log(program, len(buffer), None, buffer)
            raise RuntimeError(buffer.value.decode(errors="replace"))
        return {"prefix": prefix, "shader_objects": len(shaders)}
    finally:
        delete_program(program)
        for shader in shaders:
            delete_shader(shader)


def base_defines() -> dict[str, str]:
    tokens: set[str] = set()
    pending = ["compatibility/objects.vert", "compatibility/objects.frag",
               "compatibility/bs/default.vert", "compatibility/bs/default.frag"]
    seen: set[str] = set()
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        source = (shader_root / name).read_text(encoding="utf-8")
        tokens.update(token_re.findall(source))
        for include in include_re.findall(source):
            pending.append(resolve_include(name, include))
        # Linked shader bodies are compiled as separate objects but use the same
        # define map. Follow every possible target while collecting tokens; the
        # actual conditional links are still selected later by preprocess().
        pending.extend(raw_link_re.findall(source))
    values = {token: "0" for token in tokens if token not in ("foreach", "endforeach", "link", "shadow_texture_unit_index")}
    values.update({
        "shadow_texture_unit_list": "8,9,10",
        "shadowNormalOffset": "0.1",
        "maxLights": "8",
        "distorionRTRatio": "1",
        "locationProfileAvailable": "1",
        "disableNormals": "1",
        "numViews": "1",
    })
    return values


cases: list[tuple[str, dict[str, str]]] = [
    ("base-vertex", {}),
    ("forced-pixel", {"forcePPL": "1"}),
    ("automatic-normal", {"normalMap": "1", "normalMapUV": "1", "autoNormalMap": "1"}),
    ("automatic-specular", {"specularMap": "1", "specularMapUV": "2", "autoSpecularMap": "1"}),
    ("both-automatic", {"normalMap": "1", "normalMapUV": "1", "autoNormalMap": "1",
                         "specularMap": "1", "specularMapUV": "2", "autoSpecularMap": "1"}),
    ("automatic-with-explicit-fallback", {"normalMap": "1", "normalMapUV": "4", "autoNormalMap": "1",
                                           "fallbackNormalMap": "1", "fallbackNormalMapUV": "1",
                                           "fallbackNormalMapUnit": "1", "fallbackTangent": "1",
                                           "specularMap": "1", "specularMapUV": "5", "autoSpecularMap": "1",
                                           "fallbackSpecularMap": "1", "fallbackSpecularMapUV": "2",
                                           "fallbackSpecularMapUnit": "2"}),
    ("parallax-fallback", {"normalMap": "1", "normalMapUV": "4", "autoNormalMap": "1", "parallax": "1",
                            "fallbackNormalMap": "1", "fallbackNormalMapUV": "1", "fallbackNormalMapUnit": "1",
                            "fallbackParallax": "1", "fallbackTangent": "1"}),
    ("shadows", {"shadows_enabled": "1", "perspectiveShadowMaps": "1",
                  "limitShadowMapDistance": "1"}),
]

report: dict[str, object] = {
    "result": "RUNNING",
    "renderer": (get_string(0x1F01) or b"").decode(errors="replace"),
    "opengl": (get_string(0x1F02) or b"").decode(errors="replace"),
    "cases": [],
}
for clustered in ("0", "1"):
    for name, overrides in cases:
        for prefix in ("compatibility/objects", "compatibility/bs/default"):
            defines = base_defines()
            defines.update(overrides)
            defines["lightingMethodClustered"] = clustered
            defines["useGPUShader4"] = clustered
            result = link_case(prefix, defines)
            result.update({"name": name, "clustered": clustered == "1"})
            report["cases"].append(result)
            print(prefix, name, "clustered=" + clustered, "PASS", flush=True)
report["result"] = "PASS"
report["executed"] = len(report["cases"])
args.output.resolve().write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(args.output.resolve())
