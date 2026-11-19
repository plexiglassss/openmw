# Frame Generation loading-background continuity repair

Only replace version.dll next to openmw.exe; keep openmw_fg.ini and lua51.dll.
No plugin changes, engine replacement, new game or save conversion are needed.

## Evidence and cause
The user's phone video shows repeated viewpoint reversals during cell-loading
pauses, strongest at about 13–15 seconds, also visible at 21–22 seconds. The user
confirmed the same behavior in the original Nexus distribution before our changes.
Recovered original archive: Frame Generation (Windows) 59693 0.2.0
2026-07-27T18-05Z 63rw3vH3 (1).zip. Opened its contents and README: it supplies a
DLL, INI and license, but no source. The modified source used here is the complete
custom/frame-generation tree retained in the custom engine repository at
24b9e9170ce8909fbffc220f520be9154f9f928b. We do not claim a fresh byte-for-byte
comparison against unmodified upstream source or an exact original DLL build.

OpenMW apps/openmw/mwgui/loadingscreen.cpp installs
CopyFramebufferToTextureCallback as an initial draw callback. It copies the
framebuffer into the loading scene texture before rendering; its mOneshot field
is set false but does not guard subsequent copies. The FG OnSwapShared method
rotates four game textures. Before this fix, attachGameTexture simply selected
a free slot, retaining the image from the last time that slot was used. Thus a
loading traversal could copy a different historical viewpoint each time. The
presenter then received these old backgrounds as newly submitted native frames.
Interpolation can make transitions between them more conspicuous.

The presenter waits for new submissions; it does not run interpolation endlessly
without input. Our earlier >=250 ms history reset therefore did not address this
case: loading renders may keep submitting at short intervals. That reset, the
save/readback guard, UI guard, F8 controls and GPU reuse fences are all retained.
No changes to OpenMW's loading-screen implementation are included.

## Repair and ordering
Record the completed game slot before publishing it. After choosing the next
writable slot and queuing its presenter-to-game reuse-fence dependency, copy
all RGBA8 pixels from the completed image to that destination using
GL 4.3 glCopyImageSubData. Then attach it for subsequent game rendering.
Both contexts only read the source during this operation. Later producer
writes are ordered after the copy on the same game context. The destination
is never a pending/current/previous presenter slot. No new CPU GPU wait or
CPU pixel readback is added. Existing fence-allocation failure fallback remains.
The GL loader requires the new core 4.3 function; if unavailable, initialization
falls back to the existing passthrough path instead of using a null pointer.

This adds one full image copy per native frame, including normal gameplay.
At 2560x1440 RGBA8 that is 14,745,600 bytes of image data per copy; actual memory
traffic includes both reading and writing. NVIDIA timing cost is unmeasured.
An unconditional copy was chosen to keep partial draws and copy-before-draw
consumers correct without guessing loading state from image contents.

## Validation
Executed tests/frame_contents_gl.py against the production C++ helper, compiled
as a small bridge, using a real headless OpenGL 4.5 Mesa llvmpipe context:
- Negative control cycles [20,50,80,110] repeatedly through four old frame images.
- Fixed copy-before-draw sequence retains the latest background across rotations.
- Presenter-advancing and producer-overtaking slot patterns pass.
- Partial GUI pixel updates persist without resurrecting an older background.
- Full-frame rendering resumes and replaces the retained image correctly.
- Copy preserves texture/FBO bindings and works with restrictive scissor and
  disabled colour writes; no GL errors.
- Integration order verified: destination GPU wait, image copy, attachment.
The test models loading draws; it is not a full hooked OpenMW run or an NVIDIA
driver test. Windows CI separately builds the entire DLL and runs the existing
DLL load/export-forwarding smoke test before publishing its artifact.

## In-game acceptance checklist
1. Close OpenMW, back up version.dll, install only the new version.dll.
2. Keep the existing settings and run the same filmed Seyda Neen route.
3. At both boundaries, expect a stable background while loading. The actual
   loading duration is outside this repair. Movement should resume normally.
4. Check quick-save/load, F12 screenshot, F8 controls, FG toggle, resizing,
   alt-tab, normal camera turns and native frame timing.
5. If the background still rocks, retain the video and openmw_fg.log; this
   source-level reproduction does not certify every runtime cause eliminated.
6. Roll back by restoring the old version.dll. No settings changes are needed.
