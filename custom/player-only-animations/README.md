# Generic player-only additional animation sources

OpenMW normally discovers extra KF files under `Animations/<actor-model>/`. Those directories are deliberately shared by all actors using that model family. This feature adds an opt-in parallel convention:

```text
PlayerAnimations/<actor-model>/
```

Only the real third-person player animation instance loads files from this tree. It does not replace the player record, skeleton, body parts, equipment, camera, collision, or Lua/MWScript movement.

For a normal humanoid player, for example:

```text
PlayerAnimations/xbase_anim/my_mod/example.kf
```

Female, beast and custom player representations use their corresponding model-derived directories. The ordinary `Animations/` loader remains unchanged for every mod that needs shared actor animation sources.

The loader is generic. It contains no Sails and Sales identifiers or cell/record lists.
