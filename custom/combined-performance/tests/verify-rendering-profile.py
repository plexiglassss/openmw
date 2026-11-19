"""Bind the packaged editable profile, controls and shader resources to the executed Windows tests."""
import base64
import hashlib
import json
from pathlib import Path
import sys
from xml.etree import ElementTree

repo = Path(__file__).resolve().parents[3]
install = Path(sys.argv[1])
vfs = install / 'resources/vfs'
profile = 'shader_profiles/horror_mod.yaml'
source_hash = 'a7a165e47f471ccf383ba5b896e512c02f1d532e201663a44825a1f7eb1c1251'


def require(value, message):
    if not value:
        raise ValueError(message)


def cases(path, suite_name):
    tree = ElementTree.parse(path)
    cases = {c.get('name'): c for c in tree.iter('testcase') if c.get('classname') == suite_name}
    require(cases and all(c.get('status') == 'run' and c.get('result') == 'completed'
                          and all(c.find(tag) is None for tag in ('failure', 'error', 'skipped')) for c in cases.values()),
            f'Missing or incomplete {suite_name} in {path}')
    return cases


data = (vfs / profile).read_bytes()
require(hashlib.sha256(data).hexdigest() == source_hash and data == (repo / 'files/data' / profile).read_bytes(),
        'The installed profile differs from the reviewed Horror Mod configuration')
layout = ElementTree.parse(vfs / 'mygui/openmw_settings_window.layout')
require(any({entry.get('key'): entry.get('value') for entry in widget.findall('UserString')}.items()
            >= {'SettingName': 'location rendering profiles', 'SettingCategory': 'Shaders', 'SettingType': 'CheckButton'}.items()
            for widget in layout.iter('Widget')), 'The Lights option is missing')
require(b'location rendering profiles = true' in base64.b64decode((install / 'defaults.bin').read_bytes()),
        'The compiled default is missing')
shaders = ('lib/light/locationrenderingprofile.glsl', 'lib/light/clamp.glsl', 'lib/light/objectlighting.glsl',
           'compatibility/objects.vert', 'compatibility/objects.frag', 'compatibility/bs/default.vert', 'compatibility/bs/default.frag')
for shader in shaders:
    require((install / 'resources/shaders' / shader).read_bytes() == (repo / 'files/shaders' / shader).read_bytes(), shader)

suite = 'LocationRenderingProfileTest'
installed = cases('location-rendering-profile-tests.xml', suite)
require(installed.keys() == cases('openmw-tests.xml', suite).keys(), 'Source and installed profile tests differ')
fixture = installed['bundledProfileIsDiscoveredAndCanBeReplacedThroughTheVfs']
count = int(fixture.find("./properties/property[@name='selected_interiors']").get('value'))
require(count == 63 and 'higherPriorityDoesNotHideConflictsAtLowerPriorities' in installed, 'Incomplete VFS profile proof')
require('get_program_with_null_vertex_shader_does_not_dereference' in cases('components-tests.xml','ShaderManagerTest'),
        'Missing shader program regression')

result = dict(result='PASS', profile='resources/vfs/' + profile, profile_sha256=source_hash,
              selected_interiors=count, default_enabled=True, installed_native_tests=sorted(installed),
              shaders_verified=list(shaders))
out = install / 'verification/location-rendering-profile.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(f'Verified the editable {count}-interior profile and {len(installed)} native cases on the installed resources.')
