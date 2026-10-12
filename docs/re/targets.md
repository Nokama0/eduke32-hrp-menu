# Target selection: dying, choking, hittable and deferred damage (targets.h)

A target is scoped by picnum, with sitters excluded, frozen enemies excluded and a suffering action required for the
choking case. This note holds the per-enemy facts. The liveness rule itself: [target-liveness.md](target-liveness.md).

## Choking and dying troopers
- Stock GAME.CON `checktroophit` (:4415-4421) gives a choking trooper `strength 0` and leaves cstat. Every real death
  state clears cstat first: `troopdying` (:4341-4349, clears at ifactioncount 5 = settle), checktroophit's suffering
  branch (:4362-4368), `troopsufferingstate` (:4490-4502). So for a trooper a cleared cstat is a death signal. Not for
  the Enforcer (:5097), Octabrain (:5618) or Protector Drone (:8352): they hold `strength 0` on a corpse whose cstat
  still reads 257.
- A killing hit landing on exactly zero reads alive for the ~1 s ATROOPDYING fall.
- Sitting picnums 1741/1742 carry the stock hittable cstat. They cannot be choking, because ATROOPSUFFERING is reached
  only from `troopcode`, which a sitter does not run until `cactor LIZTROOP` has changed the picnum to 1680
  (:4750-4777).
- A frozen trooper (freeze branch :4373-4381: spritepal 1, ATROOPFROZEN, strength 0, cstat kept) is picnum 1680, extra
  0, cstat 257, pal 1: the choking pair bit for bit. They are mutually exclusive in the CON (freeze `break`s before
  suffering; a suffering trooper has cstat cleared at the top of checktroophit; thaw restores pal via getlastpal
  :4531-4535).
- ATROOPSUFFERING (choking) and ATROOPDYING (falling, extra 0 pal 0 cstat 257) are different action records;
  ekm_action_is_suffering tells them apart. EKM_SUFFER_UNKNOWN (the decoder has no answer: actor[] / apScript not
  located, or t_data[4] zero) keeps the unscoped behaviour.
- The sticky deferral flag is only SET by a sitting observation, unlike the "discount every actor" rule World Tour
  warns off.

## Deferred damage
A_DamageObject accumulates in actor[].htextra; health changes on the victim's own next CON tic (actors.cpp:1322), so
~33 ms of lingering is unavoidable without reading actor[].

## Hittable cstat
ekm_target_hittable uses bit 8 alone, as the engine does. World Tour's mask 0x8101 / value 0x101 also demanded bit 0 and
cost 7162 refusals.

Related: [actorstate.md](actorstate.md).

## Method
GAME.CON from DUKE3D.GRP and the engine source; refusals counted in the mod's log.
