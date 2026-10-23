# Windows Frame Generation shutdown repair

Replace version.dll beside openmw.exe with the artifact from this build. Preserve openmw_fg.ini and other installed DLLs. The corresponding engine also fixes repeated loading-scene capture. This DLL retains the earlier framebuffer-continuity and screenshot guards.

Observed: OpenMW d204c15520 logged Quitting peacefully, yet the user reports its process remains after the window closes. Windows hooks had no caller for ShutdownIfActive; the presenter could remain alive into runtime/DLL teardown. The new hook recognizes only the captured game context, acquires it if necessary, stops and joins the presenter, releases its GPU resources, restores unrelated context state and finally calls the original deletion function. Presenter-context deletion does not recurse into cleanup because it is not the game context. Cleanup does not run under DllMain's loader lock. A failed context acquisition is logged and normal deletion is attempted; that exceptional path is not certified as fixed.

Production-hook tests with context doubles passed. Real Windows driver lifecycle and the reported persistent process still require in-game confirmation. The installed user's DLL version cannot be inferred from openmw.log; retain openmw_fg.log if the issue persists.
