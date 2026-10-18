# Lucky Kiss — Static IL2CPP inspection

Inspected user-provided binary files:

- `GameAssembly(1).dll`: Windows x64 PE, ~47 MB; SHA-256 `a7d806770ec833e6b65b8df71f2629a085e6db53a1ea642fbd9b9f41c1b5f439`.
- `global-metadata.dat`: IL2CPP sanity `0xFAB11BAF`, metadata format version **39**, ~9.6 MB.
- The previously provided `GameAssembly.dll` had an identical SHA-256 to `GameAssembly(1).dll`.
- The earlier `Assembly-CSharp.dll` refers to a different game and should **not** be substituted.

## Confirmed game symbols

`DICE`:

`get_IsStopped`, `get_TopFaceNumber`, `get_FinalValue`, `ForceSetValueInstant`, `ForceSetValueRoutine`, `RotateFaceToTop`, `GetRotationWithFaceOnTop`, `SetStoppedResult`, `ForceEvaluateResult`, `GetTopFaceNumber`, `GetFaceValue`, `finalValue`, `topFaceNumber`, `isStopped`.

`LR_DicesController`:

`get_DiceValues`, `get_TotalValue`, `SpawnDices`, `RefreshResultsFromSpawnedDices`, `CountValue`, `GetCurrentDiceValues`, `MagicRerollDicesWithValue`, `CheckDicesResults`, `CollectResults`, `FinishResult`, `diceValues`, `spawnedDices`.

`LR_GameController`:

`get_LuraBank`, `get_PlayerBank`, `get_CurrentBet`, `get_ActiveSelectedValue`, `TrackCheatsDuringRoll`, `TrackValueSelectorCheat`, `TrackDiceClickCheat`, `ChoosePunishment`, `ApplyFinalResultRoutine`, `GetPayout`, `GetPlayerBankFromData`, `SetPlayerBankToData`, `RefreshAllTextsInstant`, `playerBank`, `activeSelectedValue`, `activeInstance`.

`LR_ValueSelector`: `SelectValue`, `SelectValueInstant`, `SelectRandomValue`, `SelectRandomValueInstant`.

`MegaData`: `get_GameData`, `saveData`, `GetSavePath`, `SaveExists`, `LoadOrCreate`, `CurrentSaveName`.

`GameData`: includes `inventory` and `LuraDiceColor1` through `LuraDiceColor5` fields. **Those `LuraDiceColor*` fields are not necessarily the rolled face values**; changing them without checking their use is unsafe.

## Findings vs hypotheses

1. The listed functions and fields have been **observed in the uploaded metadata**.
2. Native IL2CPP exports necessary for runtime reflection were observed in the uploaded `GameAssembly` PE export directory.
3. No claim has been made that the prototype's getter hook is called during every roll or that `ForceSetValueInstant` uses exactly the signature anticipated. It verifies signature types at runtime before touching the getter.
4. A save file was not supplied. Therefore the serialization format and persistence of bank writes **have not** been established.
5. Unity code may run on main thread only. The prototype avoids invoking `UnityEngine` gameplay functions from the worker thread, relying instead on a getter-called-on-game-thread hypothesis.

## What to verify during a local test

- Does module initialization report dice hook = established and bank hook = established?
- With a backed-up save, do all 5 dice VISUALLY show the intended number when the result is determined? Does the game's payout match?
- Do `骰子调用` and `强制设置` counters change when a roll completes? If neither changes, the result getter path may be inlined/bypassed.
- Does setting coins change the *real bank balance*, and does that amount persist after a full game restart?
- Does the game react by rerolling or punishing the player? If so, do not assume the winning condition is met.

## Additional game modification candidates (from the actual metadata)

The following are **symbol discoveries**, not implemented or tested cheats.

| Category | Observed classes/fields/methods | Potential use | Caveats |
|---|---|---|---|
| Betting payout | `LR_GameController.GetPayout`, `winMultiplier1`…`winMultiplier5` | Study win multipliers and bet payout | Final payout can include debt and other branching routines; changing a multiplier doesn't ensure a win. |
| Player and opponent banks | `LR_GameController.playerBank`, `luraBank`, `get_LuraBank`, `GetPlayerBankFromData`, `SetPlayerBankToData` | Edit either balance, inspect money persistence | `luraBank` may be recalculated each round. |
| Stakes and difficulty | `currentBet`, `minBet`, `startBet`, `hardLevel`, `GetAttentionByDifficulty` | Study bet limits, attention or difficulty response | Some of these are serialized settings, others are scene fields. |
| Cheat detection and penalties | `TrackCheatsDuringRoll`, `TrackDiceClickCheat`, `TrackValueSelectorCheat`, `ChoosePunishment`, `RerollWinningDicesPunishmentRoutine`, `ForceWorstValueSelectorPunishment`, `ChaosPunishmentRoutine` | Diagnose whether the game's punishment changes a forced outcome | There is no verified bypass in v0.0.2. |
| Relationship/dialogue | `LR_Operations.AddLuraRelation`, `GameData.get_relationsLura`, `ResetTalkCounters`, `ResetOperationsState` | Inspect relationship progression, dialogue counters | Relationship progression and story availability may have separate gates. |
| Outfit and visuals | `LR_DressController.IsDressAvailable`, `SetDicesController`, `ShowUpper`, `ShowLower`, `ShowAccessories`, `LR_LuraController.SetNextStartDressIndex` | Study outfit selection and visible parts | Not necessarily permanent save/unlock state. |
| Dice features | `LR_DicesController.MagicRerollDicesWithValue`, `SetAllowedClicksPerDice`, `GetCurrentDiceValues`, `CountValue` | Rerolls, per-die clicks, diagnostics | Some calls are expected to occur during scripted punishment. |
| Save file | `MegaData.GetSavePath`, `MegaData.saveData`, `GameData.save`, `MegaData.LoadOrCreate` | Offline save inspection/editor once a save is supplied | The current files do not establish the exact bank serialization path. |

`GameData` also includes experience, stats, inventory and day-count fields. They may originate in a shared game system and are **not proof** those features are actively used by Lucky Kiss in the current gameplay scene.

## 0.0.2 diagnostics (IPC ABI v3)

- `initialized` is a bitmask for native entrypoint patch installation: `1` finalValue, `2` playerBank getter, `4` game selected value getter, `8` isStopped, `16` topFaceNumber.
- `ready` indicates IL2CPP bootstrap has finished; `ready=1` alone is not a successful method hook.
- `diceIntercepts` counts callbacks that attempted to evaluate die changes; `diceForced` counts calls to `ForceSetValueInstant` that also read back the requested `finalValue` immediately. Neither counter verifies the game's *payout*. `forceMismatch` counts calls that did not immediately update the field.
- `goldRuntimeSequence` means numeric runtime field writing was attempted and read back; `goldSavedSequence` means game data synchronization was invoked without a caught IL2CPP exception. Neither verifies restart persistence.
- ABI 3 added `controllerObserved`, `selectedReads`, `bankReads`, `bankWrites`, `finalCalls`, `stoppedCalls`, `topCalls` and `forceMismatch`. Some values may still be zero when a getter is inlined or bypassed.
- For best results, copy the full diagnostics pane after selecting a face in the game and rolling once.

## v0.0.3 follow-up from actual v0.0.2 diagnostics

User test log:

- `Hook 11111`, but `finalCalls=0`, `stoppedCalls=0`, `topCalls=0`: patch installation was **not** proof the gameplay called the target getter methods.
- `selectedReads=445` with `gameFace=0`: each loop iteration invoked `SelectedFace` and bumped the counter even when the value was invalid. This was a misleading metric and unnecessary 120 ms polling.
- `bankType=0x08`, controller found, and runtime field readback matched 99999999. This only proves that **a field in a live object** was written; it did not prove the game displayed it or loaded it next session.
- `saveMethod=0` under `class_get_method_from_name(..., "SetPlayerBankToData", 0)` was a concrete blocker to the old persistence path. The native metadata contains the method name, but its **arity and argument types remain to be read at runtime**. v0.0.3 uses `-1` arity lookup plus parameter introspection and separate `GetPlayerBankFromData` readback.

Changes: event-driven selector and collection handlers, safe-fail trampoline hook; no periodic dice reads; explicit data readback and signatures in the diagnostic report. All changes remain unverified against an executing Windows build.

## v0.0.4: GameData & exit lifecycle

- Relationship fields in `GameData`: `_relationsLura`, `_relationsRiya`. Property accessors: `get_relationsLura`, `set_relationsLura`, `get_relationsRiya`, `set_relationsRiya`. A character-specific in-game relationship may also be gated by other story flags or dialogue progression.
- Core RPG storage fields: `lvl`, `points_up`, `hp`, `hp_point`, `hp_reg_point`, `mana`, `mana_reg_point`, `speed_point`, `def_point`, `kin_damage_point`, `sword_damage_point`, `bow_damage_point`, `bow_speed_point`, `crossBow_damage_point`, `crossBow_speed_point`. The experience property is exposed through `get_exp`/`set_exp` and may not have a field named `exp`.
- Computed RPG getters: `get_exp_nextLvl`, `get_hp_max`, `get_hp_reg`, `get_mana_reg`, `get_speed`, `get_def`, `get_kin_damage`, `get_sword_damage`, `get_bow_damage`, `get_bow_speed`, `get_crossBow_damage`, `get_crossBow_speed`.
- `GameData` also contains relationship/story flags such as `LuraTalkId`, `RiyaTalkID`, `RiyaWorkID`; these are **not** changed by the relationship editor.
- Expected limitations: 32-bit numeric property/field types only. Effective stats are not assigned directly; changing `*_point` changes progression, not a guaranteed final stat. Values are not automatically claimed as persisted to disk.
- Lifecycle: removed 500ms polling from injected managed worker, detach IL2CPP after each explicit work item, and provide pass-through shutdown mode. This is a risk reduction based on source inspection; the origin of the observed shutdown freeze has not been proven with a crash dump.

- Experimental exact-value solver for the 11 calculated RPG getters: bounded, monotonic binary search over the corresponding int32 upgrade-point field; any unverifiable/unreachable target is rolled back. Do not assume progression or save persistence without live testing.
