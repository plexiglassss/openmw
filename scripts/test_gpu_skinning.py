#!/usr/bin/env python3
"""Execute the production vertex shaders on Mesa and compare transform-feedback output to CPU skinning.

This exercises real object, shadow and transparent-depth shader bodies, not a reimplementation of GLSL.
It is a correctness test, not an in-game FPS benchmark.
"""
from __future__ import annotations

import argparse
import ctypes as C
import json
import math
import random
from pathlib import Path

import test_location_rendering_shaders as gl

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=Path("gpu-skinning-gl-tests.json"))
args = parser.parse_args()
G, fn = gl.G, gl.fn
use_program = fn(G, "glUseProgram", None, C.c_uint)
get_uniform = fn(G, "glGetUniformLocation", C.c_int, C.c_uint, C.c_char_p)
uniform_int = fn(G, "glUniform1i", None, C.c_int, C.c_int)
uniform_matrix = fn(G, "glUniformMatrix4fv", None, C.c_int, C.c_int, C.c_ubyte, C.POINTER(C.c_float))
gen_buffers = fn(G, "glGenBuffers", None, C.c_int, C.POINTER(C.c_uint))
bind_buffer = fn(G, "glBindBuffer", None, C.c_uint, C.c_uint)
buffer_data = fn(G, "glBufferData", None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint)
bind_base = fn(G, "glBindBufferBase", None, C.c_uint, C.c_uint, C.c_uint)
get_buffer = fn(G, "glGetBufferSubData", None, C.c_uint, C.c_ssize_t, C.c_ssize_t, C.c_void_p)
feedback_varyings = fn(G, "glTransformFeedbackVaryings", None, C.c_uint, C.c_int,
                       C.POINTER(C.c_char_p), C.c_uint)
begin_feedback = fn(G, "glBeginTransformFeedback", None, C.c_uint)
end_feedback = fn(G, "glEndTransformFeedback", None)
enable = fn(G, "glEnable", None, C.c_uint)
disable = fn(G, "glDisable", None, C.c_uint)
client_enable = fn(G, "glEnableClientState", None, C.c_uint)
vertex_pointer = fn(G, "glVertexPointer", None, C.c_int, C.c_uint, C.c_int, C.c_void_p)
normal_pointer = fn(G, "glNormalPointer", None, C.c_uint, C.c_int, C.c_void_p)
texcoord_pointer = fn(G, "glTexCoordPointer", None, C.c_int, C.c_uint, C.c_int, C.c_void_p)
client_active_texture = fn(G, "glClientActiveTexture", None, C.c_uint)
attribute_pointer = fn(G, "glVertexAttribPointer", None, C.c_uint, C.c_int, C.c_uint, C.c_ubyte, C.c_int, C.c_void_p)
enable_attribute = fn(G, "glEnableVertexAttribArray", None, C.c_uint)
draw_arrays = fn(G, "glDrawArrays", None, C.c_uint, C.c_int, C.c_int)
get_error = fn(G, "glGetError", C.c_uint)


def floats(values):
    return (C.c_float * len(values))(*values)


def identity():
    return [1.0 if row == col else 0.0 for row in range(4) for col in range(4)]


def multiply(left, right):
    return [sum(left[row * 4 + k] * right[k * 4 + col] for k in range(4))
            for row in range(4) for col in range(4)]


def transform(value, matrix, direction=False):
    vector = list(value[:3]) + [0.0 if direction else 1.0]
    return [sum(vector[k] * matrix[k * 4 + col] for k in range(4)) for col in range(4)]


def cpu_matrix(weights, indices, palette, root, enabled):
    if not enabled or indices[0] < 0:
        return identity()
    result = [0.0] * 16
    # Independent transcription of the pre-existing CPU affine blend, including non-unit weight sums.
    for weight, index in zip(weights, indices):
        for component in range(16):
            if component % 4 != 3:
                result[component] += palette[int(index)][component] * weight
    result[15] = 1.0
    return multiply(result, root)


def program_for(prefix, defines, varying_names):
    shaders = gl.compile_stage(prefix + ".vert", 0x8B31, defines)
    shaders += gl.compile_stage(prefix + ".frag", 0x8B30, defines)
    program = gl.create_program()
    for shader in shaders:
        gl.attach_shader(program, shader)
    for slot, name in ((4, b"openmwRigWeights"), (5, b"openmwRigIndices"), (6, b"fallbackTangent")):
        gl.bind_attribute(program, slot, name)
    names = (C.c_char_p * len(varying_names))(*(name.encode() for name in varying_names))
    feedback_varyings(program, len(names), names, 0x8C8C)  # GL_INTERLEAVED_ATTRIBS
    gl.link_program(program)
    status = C.c_int()
    gl.program_iv(program, 0x8B82, C.byref(status))
    if not status.value:
        log = C.create_string_buffer(16384)
        gl.program_log(program, len(log), None, log)
        raise AssertionError(f"{prefix}: {log.value.decode()}")
    for shader in shaders:
        gl.delete_shader(shader)
    return program


def execute(prefix, defines, palette, root, vertices, normals, tangents, fallback_tangents,
            weights, indices, enabled=True, fallback=False):
    has_normals = prefix.endswith("objects")
    varying_names = ["gl_Position"] + (["passNormal", "passTangent"] if has_normals else [])
    program = program_for(prefix, defines, varying_names)
    use_program(program)
    for name, value in ((b"openmwRigEnabled", enabled), (b"locationRenderingProfileActive", fallback),
                        (b"ordinaryAutoUseObjectNormalMaps", True),
                        (b"locationProfileAutoUseObjectNormalMaps", False)):
        uniform_int(get_uniform(program, name), int(value))
    for name, matrices in ((b"openmwRigPalette", palette), (b"openmwRigTransform", [root]),
                           (b"projectionMatrix", [identity()])):
        uniform_matrix(get_uniform(program, name), len(matrices), False,
                       floats([v for matrix in matrices for v in matrix]))

    arrays = [floats([v for item in values for v in item])
              for values in (vertices, normals, tangents, fallback_tangents, weights, indices)]
    bind_buffer(0x8892, 0)  # compatibility client arrays; keep every ctypes allocation alive until readback
    client_enable(0x8074)
    vertex_pointer(3, 0x1406, 0, arrays[0])
    client_enable(0x8075)
    normal_pointer(0x1406, 0, arrays[1])
    client_active_texture(0x84C0 + 7)
    client_enable(0x8078)
    texcoord_pointer(4, 0x1406, 0, arrays[2])
    for slot, values in ((6, arrays[3]), (4, arrays[4]), (5, arrays[5])):
        enable_attribute(slot)
        attribute_pointer(slot, 4, 0x1406, False, 0, values)

    components = 11 if has_normals else 4
    output = (C.c_float * (components * len(vertices)))()
    buffer = C.c_uint()
    gen_buffers(1, C.byref(buffer))
    bind_buffer(0x8C8E, buffer.value)
    buffer_data(0x8C8E, C.sizeof(output), None, 0x88E1)
    bind_base(0x8C8E, 0, buffer.value)
    enable(0x8C89)  # GL_RASTERIZER_DISCARD
    begin_feedback(0)  # GL_POINTS
    draw_arrays(0, 0, len(vertices))
    end_feedback()
    disable(0x8C89)
    get_buffer(0x8C8E, 0, C.sizeof(output), output)
    error = get_error()
    if error:
        raise AssertionError(f"{prefix}: OpenGL error {hex(error)}")

    expected = []
    for vertex, normal, tangent, fallback_tangent, weight, index in zip(
            vertices, normals, tangents, fallback_tangents, weights, indices):
        matrix = cpu_matrix(weight, index, palette, root, enabled)
        expected += transform(vertex, matrix)
        if has_normals:
            expected += transform(normal, matrix, True)[:3]
            selected = fallback_tangent if fallback else tangent
            expected += transform(selected, matrix, True)[:3] + [selected[3]]
    max_error = max(abs(a - b) for a, b in zip(output, expected))
    for component, (actual, wanted) in enumerate(zip(output, expected)):
        if not math.isfinite(actual) or abs(actual - wanted) > 2e-4 * max(1.0, abs(wanted)):
            raise AssertionError(f"{prefix} component {component}: GPU={actual}, CPU={wanted}")
    fn(G, "glDeleteBuffers", None, C.c_int, C.POINTER(C.c_uint))(1, C.byref(buffer))
    use_program(0)
    gl.delete_program(program)
    return {"prefix": prefix, "vertices": len(vertices), "enabled": enabled,
            "fallback_tangent": fallback, "max_absolute_error": max_error, "result": "PASS"}


rng = random.Random(0x534B494E)
count = 257
vertices = [[rng.uniform(-12, 12) for _ in range(3)] for _ in range(count)]
normals = [[rng.uniform(-1, 1) for _ in range(3)] for _ in range(count)]
tangents = [[rng.uniform(-1, 1) for _ in range(3)] + [1.0 if i % 2 else -1.0] for i in range(count)]
fallback_tangents = [[rng.uniform(-1, 1) for _ in range(3)] + [-item[3]] for item in tangents]
indices = [[float(rng.randrange(64)) for _ in range(4)] for _ in range(count)]
weights = [[rng.uniform(-0.25, 1.25) for _ in range(4)] for _ in range(count)]
indices[0], weights[0] = [-1.0, 0.0, 0.0, 0.0], [0.0] * 4  # entirely uninfluenced
indices[1], weights[1] = [63.0] * 4, [0.25] * 4  # all influences reference a missing bone
weights[2] = [0.0] * 4  # explicit zero weights still receive root translation
weights[3] = [0.2, 0.1, 0.0, 0.0]  # deliberately non-normalized
weights[4] = [1.0, 0.0, 0.0, 0.0]

report = {"result": "RUNNING", "renderer": gl.get_string(0x1F01).decode(), "cases": [], "linked_variants": []}
for clustered in ("0", "1"):
    for name, overrides in gl.cases:
        defines = gl.base_defines()
        defines.update(overrides)
        defines.update(gpuSkinning="1", lightingMethodClustered=clustered, useGPUShader4=clustered)
        result = gl.link_case("compatibility/objects", defines)
        result.update(name=name, clustered=clustered)
        report["linked_variants"].append(result)

for pose in range(3):
    palette = []
    for _ in range(64):
        matrix = identity()
        for row in range(4):
            for col in range(3):
                matrix[row * 4 + col] = C.c_float(rng.uniform(-2, 2)).value
        palette.append(matrix)
    palette[63] = [0.0] * 16  # missing bones are skipped without renormalization
    root = [C.c_float(v).value for v in (1.1, .2, -.1, 0, -.3, .8, .1, 0, .2, -.1, .7, 0, 2, -3, 1, 1)]
    for prefix in ("compatibility/objects", "compatibility/shadowcasting", "compatibility/depthclipped"):
        defines = gl.base_defines()
        defines.update(gpuSkinning="1", normalMap="1", normalMapUV="0", autoNormalMap="1",
                       fallbackNormalMap="1", fallbackNormalMapUV="1", fallbackTangent="1",
                       alphaFunc="519", alphaToCoverage="0", adjustCoverage="0")
        for enabled, fallback in ((True, False), (False, False), (True, True)):
            result = execute(prefix, defines, palette, root, vertices, normals, tangents, fallback_tangents,
                             weights, indices, enabled, fallback)
            result["pose"] = pose
            report["cases"].append(result)
            print(prefix, "pose", pose, "GPU", enabled, "fallback tangent", fallback, "PASS", flush=True)

report["result"] = "PASS"
report["executed"] = len(report["cases"])
args.output.write_text(json.dumps(report, indent=2) + "\n")
print(args.output.resolve())
