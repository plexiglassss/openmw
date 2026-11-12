"""Reconstruct the preceding load function without the reviewed saved-map handoff."""
import hashlib


def before_map_restore(source):
    # Each edit is checked in context. This is a proof of equivalence on a string:
    # the actual StateManager file is never modified by the build-time lookup patch.
    edits = (
        (
            "        auto& windowManager = *MWBase::Environment::get().getWindowManager();\n"
            "        Loading::Listener& listener = *windowManager.getLoadingScreen();",
            "        Loading::Listener& listener = *MWBase::Environment::get().getWindowManager()->getLoadingScreen();",
        ),
        (
            "                windowManager.advanceSaveMapRestore(false);\n"
            "                const auto progressStart = LoadClock::now();",
            "                const auto progressStart = LoadClock::now();",
        ),
        (
            '        phase("player setup, render and mechanics");\n        windowManager.advanceSaveMapRestore(false);',
            '        phase("player setup, render and mechanics");',
        ),
        (
            '        phase("projectiles and startup scripts");\n'
            '        // Finish while the loading screen owns the frame, before scripts or the player can open/save the map.\n'
            '        windowManager.advanceSaveMapRestore(true);\n        phase("finish saved world map");',
            '        phase("projectiles and startup scripts");',
        ),
    )
    for addition, previous in edits:
        if source.count(addition) != 1:
            raise ValueError("Saved-map load integration differs from its reviewed source")
        source = source.replace(addition, previous, 1)

    data = source.encode("utf-8")
    digest = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
    if digest != "102b9e0c99229d0e096f47671ff4da350381cc2a":
        raise ValueError("Unrelated StateManager change outside saved world-map restoration")
    return source
