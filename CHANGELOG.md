# Changelog

## Unreleased (since v0.5.0-beta)

### New: native split screen (64-bit build, one game window, one process)

The **Split screen** button of the campaign menu no longer starts a second game; both players are in the one game, the
picture cut top and bottom like the Xbox 360 version: each half has its own view built for its shape (field of view of
the console's split screen), its own weapon, ammo and compass, and text that keeps its proportions. The second player
presses any button on his gamepad in the lobby; the first one keeps keyboard and mouse. The mission starts as a normal
level in this process, the session ends when you are back in the menus. Pause, the objectives, the briefing and the
console show one full-size picture. Texts that belong to both (the objective, subtitles, the mission title lines)
appear once, in the top half.

How: the game's own two-player machinery runs with two client states swapped per frame; the scene's 2D is drawn into
a layer per half and laid over the squeezed view; the game's 2D placement is made for a half. Details and the research:
`docs/native-splitscreen-plan.md`. Settings under `[native]` in `settings.ini` are for testing (QA only).
Known gaps: the second player cannot switch weapons by pad yet (use the d-pad slots), one sound listener, no second
keyboard, the 32-bit build has the two-process split screen only.

## Unreleased (since v0.4.7-beta)

Everything here is in `main` and tested with bots on two game copies on one PC (host + client); none of it has
been played through by two people yet.

### Fixed: the Museum, missions in the lobby, a hang

- **Museum for two.** The credits camera carries both players and each sees the museum, not the inside of the other's head (both are hidden from each other during the credits). The museum's civilians play their idles again: the game's animation tables for them were incomplete in the credits mode and the vanilla game raised ten million script errors there (and left civilians in a T-pose), in single player too.
- **All missions can be picked in the co-op lobby** (issue #3): the host sees every mission of the campaign list as open, whatever his save says. Nothing is written to the profile, and his own Mission Select keeps its rules.
- **A rare hang of both PCs after a mission start** (seen once in the 32-bit build): the game's asset database lookup `DB_IsXAssetDefault` did not give back the reader count it took when a name was missing, so the next writer waited for ever. Fixed in both builds.
- QA: the checkpoint bots (`cptour`) now run on every campaign mission in 64-bit and 32-bit builds and count the level's own checkpoint remaps.

### New: fonts closer to the English original (Russian game)

The Russian game draws every text in one generic condensed sans, while the English original uses Bank Gothic (headings,
HUD numbers), ITC Conduit (running text) and T26 Carbon (mission intro screens), none of which has Cyrillic. The mod now
renders the game's own font tables again from free fonts with Cyrillic and puts them into the game's memory: the **H\*-Mod
fonts** (made by Vlad Loktionov for h2-mod): a Bank Gothic like face for the headings and the HUD, a Conduit like one for
descriptions and subtitles, a bold one for the mission intro text and subheadings, with smaller glyphs (SIL OFL,
shipped in `mw2coop\fonts`). Works in the 64-bit and the 32-bit build. `fonts=0` under `[display]` in `settings.ini`
keeps the game's own font; the English game is never touched.

### New: scenes that only the first player had are now for both

These scenes were scripted for one player; the other stood by or was left behind. Each now plays for two, with a
rig of his own where the animation needs one.

- **The Gulag, cellblock rope**: both rappel down, one after the other.
- **The Gulag, the evacuation**: both lie buried, each clips on the Pavelow's rope with his own use key (the
  rope's trigger stays on until both have), both are pulled away. A player who is too late is buried by the
  cave-in, as before.
- **Just Like Old Times, the opening rappel**: whoever hooks up first rappels and kills the guard below; the hook-up
  is free again and the other rappels the same way. The barracks checkpoint waits until both are down (it used to
  pull player 1 up to the top in the middle of his knife kill).
- **Escape from Favela, the big jump over the gap**: whoever leaps (or falls) first starts it; both play the
  jump (Soap reaches for both) and wake up on the ground side by side. Each rig starts 55 units from the other, so
  nobody begins inside a wall or inside the other.
- **Escape from Favela, the chopper's rope ladder**: whoever leaps for the ladder first starts the scene, the other
  catches the same rope. A partner far behind is brought to the rooftop slide, so you reach the ledge together.
  Both screens fade out at the end.
- **The Only Easy Day..., the escape**: both board the little bird, side by side on the bench; it takes off
  when both are on board.
- **Wolverines!, the Humvee at the end**: checked (it was already done): player 2 gets a seat of the parked Humvee.

### Cliffhanger and the other levels, from the playtest and after

- **Cliffhanger**: both players get the level's stealth visibility (player 2 was seen at any distance); the other
  player sees the snowmobile's full third-person model; the rider sits on his snowmobile in his own body (the
  co-op body's mesh on Soap's skeleton, so the AI driving animation fits), hands on the handlebar, upright; on the
  ice climb the partner sees the climber's body, not four arms, and the ice picks stand upright in both hands.
  A swing clicked during the last one counts (the mouse buttons sometimes needed two clicks); this one is a
  guess and easy to revert on its own.
- **S.S.D.D.**: the course's gates close once both players are through; either player's sprint counts.
- **Team Player**: either player can start the seat swap; the passenger starts looking ahead; nobody hurts the
  other during the ride.
- **Lobby**: the password mode has its own Password row, and Start is refused until a password is set.

### Fixes in the engine side

- The game leaks a reader count when a model name is unknown, and the next `playsound` then hangs; hooked.
- GSC `GetEntArray( flag, "script_flag" )` finds nothing in this build; the S.S.D.D. gates and the checkpoint leader
  now look at the trigger classes.

### Hardening and housekeeping (code audit, `docs/audit-2026-10-25.md`)

- The host's start packet and the lobby's "ready" packet are acted on only when they come from the partner's
  address; anyone else's is dropped (the first five are logged).
- The command file (`mw2coop\commands.txt`, the QA interface) is read only if it exists when the game starts.
- `mw2coop.log` past 5 MB is set aside as `mw2coop.log.old`; the recorder wrote about 1 MB an hour and nothing
  ever emptied it.
- The buffers of the models made for the game (the co-op rider, the ice pick, No Russian's crew) are freed when the
  level after next loads, instead of never.
- One helper (`src/hook.c`) for all the code patches; its trampoline pages are read-execute, not writable and
  executable.

### Development

- QA scenarios for each new scene, a generic scene watcher, and tools to turn the frames of both screens into
  contact sheets (`tools/shots2png.ps1`, `tools/sheet.ps1`, `tools/spoof_test.py`).
- The patch generators are one per level; `deploy.sh` stops when a patch does not apply.

### Checked by hand

Joining through a Steam invite between two different Steam accounts works (tested by the owner with a friend); it
is off the known issues.

### Not changed

Slow motion still shows on both screens; the Museum epilogue and No Russian's far ending: see the known issues of
the next release.

*Кратко по-русски:* Гулаг (верёвка в блоке и эвакуация к вертолёту), спуск в «Таком же, как в старые времена»,
большой прыжок и лестница вертолёта в «Побеге из фавел», посадка на вертолёт в «Единственном лёгком дне» теперь
проходятся вдвоём; исправлены найденные в игре баги Скалолаза, S.S.D.D. и «Командной игры»; пакеты старта и готовности
принимаются только от напарника; лог больше не растёт бесконечно.
