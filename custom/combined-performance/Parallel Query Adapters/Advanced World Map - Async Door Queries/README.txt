Advanced World Map - Async Door Queries

Add the directory containing scripts as a higher-priority data directory than Advanced World Map and its native-job adapter. Keep the original mod enabled. This folder replaces the player script only. Remove this higher-priority directory to revert.

Large periodic door checks can run on the new engine worker service. The same file also runs the previous synchronous paths on older engines and in comparison mode.
