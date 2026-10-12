# Documentation

## docs/re -- reverse-engineering reference notes

- [cheats.md](re/cheats.md) -- every EDuke32 cheat code, how it runs, and why Alien Armageddon's table is the stock one.
- [polymer-sprite-crash.md](re/polymer-sprite-crash.md) -- the Polymer crash on explosive barrels: proven facts, refuted theories, two latent engine bugs.
- [actorstate.md](re/actorstate.md) -- actor[] and apScript: how they are located and what the action records answer.
- [autosave.md](re/autosave.md) -- which engine path counts as an autosave, the path reset, and how its globals are located.
- [autoswitch.md](re/autoswitch.md) -- the cl_weaponswitch bit meanings and why bit 4 alone means "never switch".
- [cansee.md](re/cansee.md) -- locating the engine cansee by seven decoded facts and a 53-byte pattern.
- [engine-facts.md](re/engine-facts.md) -- short findings behind single modules: muzzle aim, cvar saving, damage numbers, mouse binds, tiles, call redirects.
- [freecam.md](re/freecam.md) -- free camera: draw path, tic clock, transports, sky band, fog, teleport, and the Polymer sweep array locates.
- [frustum-cull.md](re/frustum-cull.md) -- why the menu culls with its own frustum test and when it never rejects.
- [gamehooks.md](re/gamehooks.md) -- P_PreFireHitscan, A_ShootWithZvel and A_IncurDamage: rvas, decoded facts and signatures.
- [gamestate.md](re/gamestate.md) -- locating sprite[] and the other engine arrays, scan cost and budget.
- [hitscan.md](re/hitscan.md) -- locating and calling the engine hitscan: side effects, updatesector witnesses, self-tests.
- [image-scan.md](re/image-scan.md) -- which parts of the game image the locators may search; the .bss problem.
- [modelbox.md](re/modelbox.md) -- raw MD3 numbers to Build coordinates for model bounding boxes.
- [picnums.md](re/picnums.md) -- picnum tables: doors, switches, pickups, breakable walls, one-shot cover.
- [playerstate.md](re/playerstate.md) -- player structure offsets, shot geometry, effective pitch and the level-loaded rule.
- [polymer-projection.md](re/polymer-projection.md) -- how Polymer forms pitch and why viewingrange must be derived.
- [polymer-sweep.md](re/polymer-sweep.md) -- whole-map sweep: sky arm, array bounds, culling state, masked floors.
- [secret-path.md](re/secret-path.md) -- door set and lotag rules for the secret path search.
- [secret-triggers.md](re/secret-triggers.md) -- activators and master switches that open secrets.
- [secretvol.md](re/secretvol.md) -- secret volume cache and its map-change window.
- [shot.md](re/shot.md) -- screenshot mode: HUD cvars, settings.cfg hazard, status bar and scuba redirects.
- [target-liveness.md](re/target-liveness.md) -- the rule for a living enemy sprite and the three rules that were wrong.
- [targets.md](re/targets.md) -- dying, choking, frozen and hittable rules per enemy; deferred damage.
- [tsprite-sweep.md](re/tsprite-sweep.md) -- building the tsprite list the engine inlined away.
- [ud-offsets.md](re/ud-offsets.md) -- the ud structure offsets read off the image.
