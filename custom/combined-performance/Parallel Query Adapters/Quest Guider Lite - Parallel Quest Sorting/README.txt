Quest Guider Lite - Parallel Quest Sorting

Add the directory containing scripts after Quest Guider Lite, its Indexed Data and its native-job adapter. Keep all of those installed. This directory contains the complete global main script and its new sorting module. Remove this directory to revert.

Large quest-detail requests can sort copied distance keys on an independent Lua state. Smaller lists, older engines and comparison mode use the existing synchronous sort. The quest database and existing menu files come from the installed mod.
