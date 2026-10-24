# OpenMW Bridge v1.2.7 — equipment identity and plant eligibility

ACTION: Replace the separate OpenMW Bridge folder and update the cumulative engine to the matching build. Keep the restored Inventory Extender base and the successful Frame Generation DLL from commit 1756e384. No new FG DLL is required. Keep bridge Data_Files after the base; do not merge it into the base.

## What changed

- The supplied 24 October log confirms that paper-doll clicks reach equipment slot 16 but the ownership guard rejects them. Compare parent/player object IDs, not equality between the local object wrapper and openmw.self. Bound-item, provider-veto and pending-trade guards remain. Cursor pickup and subsequent equip still use IE's normal handling.
- The plant tooltip no longer requires native graphical herbalism or a harvestable mesh animation. Organic containers with one directly present ingredient stack qualify. Non-organic, empty, mixed-content, locked and trapped containers fall back to the original tooltip. This intentionally also supports organic ingredient containers that are not plants; it does not alter activation or harvest behavior. Unresolved leveled-list-only contents and script-only activators are not guessed or rolled on hover.
- Plant selection writes a short diagnostic when the focused record changes, showing the resolved ingredient or fallback reason. Ingredient effects still pass through the base Inventory Extender alchemy-skill filtering.
- The engine hides its window as the game loop ends, before saving and destruction. This prevents a stale frame from becoming visible during lengthy shutdown. It does not forcibly kill the process or skip saves.
- Shutdown now logs completed storage saving and elapsed time for each major destruction stage. The supplied log shows about 18.5 seconds before Quitting peacefully but cannot attribute the delay. Actual cleanup speed is not claimed fixed yet.

## Validation and limits

The new regression uses separate player wrappers with identical IDs, unlike the earlier mock that reused one table and missed this defect. Cursor pickup/equip, bound-item/provider rejection and trade reservations pass locally. Full Windows compilation is checked separately by Actions. Neither the UI nor shutdown behavior has been tested live by the author. Loading continuity is user-confirmed fixed and unchanged.

## Focused check

1. Click equipped gear on the doll: it should attach to the cursor; click again to equip. Check a bound item remains protected.
2. Look at unharvested hypha facia, draggle-tail and gold kanet. Compare displayed effects with the harvested ingredient at the same Alchemy skill.
3. Quit. The window should disappear immediately when cleanup begins; check how long openmw.exe remains. Send openmw.log if it remains slow: Shutdown lines now identify the expensive stage. If the window reappears, include openmw_fg.log too.
