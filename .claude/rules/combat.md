---
paths:
    - "src/features/Combat.*"
---

# Real-time combat

- `GFActionRealtimeCombatSMG026`: every target dies from one hit, and the flow judges a hit (sometimes per target), a miss after N shots, or a timeout. Story fights last 1.6 to 8 seconds, the chapter 2 shooting range 200. `CombatAimSetting`: 0 Off, 1 On (assist), 2 Auto (the game plays the fight; the mod says only "Aiming, automatic").
- There is no crosshair (`bEnableReticle` is never set): the aim is the weapon's torch beam. Aim input is `AimingX/Y`, the mouse or the right stick only.
- The shot, from the game's code: it starts at the muzzle socket (`Barrel_socket`) along the torch's rotation; 35 pellets become a uniform 5 by 5 grid 0.54 degrees apart (`bUniformSpread`); each pellet is a line trace on `TargetCollisionChannel` over `WeaponEffectiveRange`, and a blocking hit on a `PossibleTargets` actor is a hit. Trace every `PrimitiveComponent` of a target: a bottle's collision is a capsule and its mesh has none. Recoil kicks the torch after a shot, so only the line before it counts.
- Aim assist: a 60 cm sphere swept along the torch over `AimTargetDistance`; the nearest target it touches pulls the aim. `MinLockDistance` and `MaxLockDistance` (2 and 7) are a band in a 2D screen unit, not metres. The Off setting zeroes all of it. `TargetPos` on the replicator is the aim direction 80 m out, never a hit point.
- Targets: `PossibleTargets[].Target.ActorName`, resolved through `ActorRegister`, taking the nearest of same-named actors (the persistent level holds copies from other scenes). Never speak a target's name: the game hides identities in several fights. Say creature (the class or name holds "wolf"), object (not a pawn) or target, with distance and side.
- A new `ARealtimeCombatSMG026_Replicator` is spawned for each fight; its `Path` ends with the fight's state label, which picks the action, and the fight ends when the replicator is destroyed. The combat camera modifier is not a usable signal.
- The double ping is the moment's truth (a pellet meets a body now). "Fire!" needs the grid to meet the body from the line swayed 0.15 degrees each way, held 0.2 seconds; with the stick still, aim sway moves the line by 0.1 to 0.2 degrees.
- The setup reads `FActorReference` strings, which can be garbage right after a death rewind, so it runs under `SafeInvokeLogged`.
