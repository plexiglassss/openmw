# Iyani dialogue capture

User action: launch this diagnostic build with Start-Profiling.ps1 and reproduce Iyani's egg-quest acceptance once. Send the resulting Run folder as a ZIP or 7z archive.

This instrumentation extends the diagnostic build at 09298febacd460cc7a63deb9b41ed5edc3ed79b6. It preserves the previous dialogue iterator, launcher, general timing and creature-stat diagnostics. It changes no dialogue conditions, quest stages, scripts, event order, or topic caching. It is a measurement build, not a performance fix. Custom Rubic0n and Frame Generation DLLs are separate files.

## Install and capture

1. Close OpenMW and the launcher. Keep the working program folder. Extract the new Windows runtime archive into a separate program folder.
2. Preserve the same DLL configuration used in the affected run: copy your installed Rubic0n lua51.dll and Frame Generation version.dll / INI if you were using them. The engine archive contains the standard lua51.dll and no custom version.dll.
3. Keep the same mods and settings. No ESP or omwscripts entry is needed. Keep the Lua instruction profiler and Lua debug at their existing settings; neither needs enabling for this recorder.
4. Open PowerShell in the new program folder and run:

   ```powershell
   .\Start-Profiling.ps1
   ```

   If a downloaded script is blocked, review the script and unblock this file with `Unblock-File -LiteralPath .\Start-Profiling.ps1`, then run it. This does not change the machine's execution policy. If the script is prohibited by a managed execution policy, use the manual launch below instead.

5. Load the save from before accepting Iyani's quest. Do not reset its journal stage with SetJournalIndex: that would not recreate the same journal history or mod state.
6. Speak to Iyani (`den_iyani`), choose "What's wrong?", then "I could get some for you". Let the freeze finish. Wait approximately five seconds and quit normally. There is no need to repeat Black Queen's interaction for this capture.
7. The script opens a new Documents/OpenMW Profiling/Run-* folder. Archive the entire folder, particularly script-timing.csv, openmw-stats.log, openmw.log, capture.txt, and the copied configuration/hashes. Do not send only openmw.log: it contains event markers, not the complete timings.

Iyani's reported position is exterior Seyda Neen (-2, -9), approximately (-11392.1, -71570.7, 169.68). The expected acceptance INFO is 2048130832721222166 and its journal update is den_kwama stage 10. The trace records the actual winning INFO, so a later override or a different response will be visible.

## What the capture separates

| Measurement | What it includes |
|---|---|
| dialogue-ui / onChoiceActivated | Synchronous handling of the choice click and the subsequent topic refresh |
| dialogue / selectChoiceResponse | Selecting the matching dialogue response |
| dialogue / addTopicsFromText | Rebuilding known topics and discovering topics mentioned by the answer |
| dialogue / updateActorKnownTopics | Full topic eligibility rebuild, with updateGlobals and text parsing recorded as nested work |
| dialogue / compileResultScript | Compiling the selected INFO's result script |
| dialogue / runResultScript | Running that result script, with journal and Lua children separated |
| journal / addEntry | Journal insertion, quest state updates and synchronous callbacks |
| journal / dispatchQuestUpdate | Dispatching onQuestUpdate to player scripts; each Lua handler retains its own row |
| dialogue-ui / addResponse, updateHistory, updateTopicFormat | Dialogue text layout and UI refresh |
| lua / DialogueResponse | Deferred Lua reactions to the selected response, attributed to their script paths |
| action, gc, phase, wait | Existing delayed-action, collection and worker timings |

The ordinary log adds [DialogueTrace] markers for the choice, selected actor/topic/INFO, and journal stage. CSV values are elapsed microseconds, not CPU samples. Inclusive parent and child values overlap and must not be added together. self_us subtracts only instrumented children. Multiple topic rebuilds in one frame aggregate as calls/total_us/max_us; their individual parents and INFO markers identify the interaction. UI completion and frame timing do not directly timestamp the display's presentation.

## Manual launch if PowerShell script execution is restricted

Open PowerShell in the new engine folder and paste the following commands. They set variables only for this PowerShell process and its child game. After the game exits, the fresh log and configuration are collected. This assumes the normal Documents/My Games/OpenMW user directory.

```powershell
$runDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) ('OpenMW Profiling\Iyani-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $runDir | Out-Null
$env:OPENMW_TIMING_FILE = Join-Path $runDir 'script-timing.csv'
$env:OPENMW_OSG_STATS_FILE = Join-Path $runDir 'openmw-stats.log'
$env:OPENMW_OSG_STATS_LIST = 'times;resource'
$gameExe = (Resolve-Path -LiteralPath '.\openmw.exe').Path
$captureStart = Get-Date
"Executable: $gameExe`nStart: $($captureStart.ToString('o'))" | Set-Content (Join-Path $runDir 'capture.txt')
Start-Process -FilePath $gameExe -WorkingDirectory (Split-Path $gameExe) -Wait
$gameConfigDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\OpenMW'
foreach ($name in @('openmw.log','openmw.cfg','settings.cfg')) {
    $sourceFile = Join-Path $gameConfigDir $name
    if (Test-Path -LiteralPath $sourceFile) { Copy-Item -LiteralPath $sourceFile -Destination $runDir }
}
explorer.exe $runDir
```

## Evidence before this capture

The 16:57-17:01 log from openmw(20261020-160152).zip reports revision 6c4d952f04 and has a 20.569-second gap between a preceding voice message and the den_kwama journal callback message. This is an event gap, not a measured execution duration. The separate 16:21 capture contains Black Queen's interaction but no Iyani/den_kwama event, so its timings cannot establish Iyani's cause. In that earlier capture the three onQuestUpdate callbacks total about 3.55 ms; this does not prove all quest updates are fast.

Source inspection shows that Iyani's acceptance script changes journal stage, disposition and three dialogue-tracking globals. It contains no Disable call or loop. The native choice path can rebuild available topics three times. Those findings guide measurement; they are not yet attribution of the reported stall.
