"""Bind the shipping test binaries to the map artwork selection and zoom checks."""
from pathlib import Path
import json
import xml.etree.ElementTree as ET


def executed(path, suite):
    tree = ET.parse(path)
    rows = [row for row in tree.iter('testcase') if row.get('classname') == suite]
    if not rows or any(row.get('status') != 'run' or row.get('result') != 'completed'
                       or any(row.find(tag) is not None for tag in ('failure','error','skipped')) for row in rows):
        raise ValueError('Missing or failed ' + suite + ' in ' + str(path))
    return sorted(row.get('name') for row in rows)


components = executed('components-tests.xml', 'MapIconSkinTest')
controls = executed('openmw-tests.xml', 'MapIconControlsTest')
assert {'OrderedDestinationAndDoorRecordFallbacks', 'MissingTypedArtKeepsOriginalAndMissingOriginalKeepsFallback',
        'MaximumAppliesAfterZoomAndClusteringAndPreservesVisibility'} <= set(components)
assert {'IndependentLocalBaseAndMaximumSlidersRetainTheirValues',
        'RecycledMarkerKeepsOneCompleteTileWhenArtworkSizeChanges'} <= set(controls)
result = {'result': 'PASS', 'skin_and_size': components, 'native_options': controls,
          'scope': 'Windows shipping engine and component tests, with synthetic test textures and rules.'}
Path('map-icon-skin-tests.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('PASS: ordered local rules, missing texture fallbacks, capped map zoom, and independent native controls')
