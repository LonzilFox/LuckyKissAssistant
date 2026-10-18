# Lucky Kiss Assistant

A Windows desktop companion for **Lucky Kiss** by Randy Fox. It provides a dark, pink-accented interface for controlling dice results and editing selected game values while the game is running.

The application has a C# / WPF desktop interface and a native C++ module that communicates with the game's Unity IL2CPP runtime. Game executables and assets are not included.

## Features

### Dice controls

- **Follow in-game selection:** Use the die face selected within the game as the target for the five dice.
- **Manual selection:** Choose a target face from 1 through 6 in the assistant.
- **Off:** Disable dice modifications without closing the assistant.
- View status information about installed hooks, game selection events, dice processing, and result modification attempts.

### Gold

- Enter a desired player gold balance and apply it to the running game.
- Review runtime write and game-data synchronization status.
- A successful runtime readback does not, by itself, guarantee that the balance has been written permanently to disk.

### Relationships

- Edit **Lura** and **Riya** relationship values separately.
- Read the game's available values before editing.
- Check write/readback status in the assistant.

### Character statistics

The editor exposes available base fields and upgrade-point fields for:

- Level, upgrade points, and experience
- Current health and mana
- Health and mana regeneration upgrades
- Movement speed and defense upgrades
- Dagger, sword, bow, and crossbow damage upgrades
- Bow reload and crossbow fire-rate upgrades

Some displayed statistics are **calculated values**, not directly stored numeric fields. For supported calculated statistics, the assistant can attempt to find an upgrade-point value that produces the requested result, then verify the value returned by the game. Exact targets may be unavailable or rejected by the game. The experience requirement for the next level is read-only.

### Diagnostics and shutdown

- Inspect connection status, hook activity, modification requests, readbacks, and reported errors.
- Copy the full diagnostic report for troubleshooting.
- Use **Stop modifications / Prepare to exit** to stop issuing modifications before leaving the game. This cannot be reversed in the same game process; restart the game to enable modifications again.

## Requirements

- Windows x64
- An installed copy of Lucky Kiss
- .NET SDK 8 or later for building the WPF application
- Visual Studio C++ x64 build tools for compiling the native module
- Windows PowerShell for the development build check

The published Release application includes its managed runtime. The native module is built separately and must remain alongside the application executable.

## Getting started

1. Extract the project archive. Keep the `LuckyKissAssistant` folder intact.
2. Run `run-dev.bat` from the project root. On the first launch, or when source files change, it compiles the native module and WPF application automatically. Otherwise, it launches the existing Debug build.
3. Start Lucky Kiss and load a playable game session.
4. Click **Connect Game** in the assistant. Wait for the runtime module and status indicators to become available.
5. Choose a page in the sidebar and apply the desired settings.
6. Before exiting the game, use **Stop modifications / Prepare to exit** if you want to disable further modifications, then exit the game normally.

For a distributable build, run `build-release.bat`. The output is written to `out\release`.

**Always exit the game fully before switching to a different build of the native module.** Do not try to replace the injected module inside an already-running game process.

## Using the editor

1. Connect to the game and navigate to **Relationships** or **Character Stats**.
2. Use the available refresh/read action to obtain a current snapshot.
3. Type the desired number into the relevant input box. The application does not overwrite an in-progress edit during routine status refreshes.
4. Click the corresponding apply/set action.
5. Check the displayed readback and diagnostic status before making additional edits.

The **Fill inputs from snapshot** action explicitly replaces the current input values with the last available snapshot. Use it only when you intend to discard unsaved text edits.

Back up your save data before changing any game values. Relationship-driven events, computed attributes, and persistence across restarts are dependent on the game's own logic.

## Troubleshooting

**Cannot connect to the game**

- Confirm that the game is running and that its current process is accessible.
- Close old assistant instances and fully restart the game before reconnecting.
- Read the Diagnostics page for connection and runtime initialization information.

**An input is not applied**

- Verify that the assistant is connected and modifications have not been stopped.
- Check the input format and the field's expected value type.
- Read the request status and readback results; a queued request is not the same as a completed modification.

**A value changes temporarily but does not survive a restart**

- Confirm whether game-data synchronization was reported.
- Do not assume disk persistence from a successful runtime readback alone.
- Record the relevant diagnostic report before closing the game.

**The game freezes or behaves unexpectedly**

- Stop making changes and avoid forcing additional requests.
- Copy the diagnostic report if the assistant remains responsive.
- Restart the game completely before attempting another test.

## Build commands

| File | Purpose |
| --- | --- |
| `run-dev.bat` | Build Debug output when necessary and launch the desktop application. |
| `build-release.bat` | Compile and publish the Windows x64 Release output. |

These are the only batch entry points. `scripts/needs-build.ps1` is an internal helper for checking whether the development build needs to be refreshed.

## Project structure

```text
LuckyKissAssistant/
  src/
    LuckyKissMod.cpp
    Shared.h
  ui/
    App.xaml
    App.xaml.cs
    MainWindow.xaml
    MainWindow.xaml.cs
    LuckyKissAssistant.csproj
  scripts/
    needs-build.ps1
  research/
    findings.md
  run-dev.bat
  build-release.bat
  README.md
  .gitignore
```

## Notes

This tool is intended for local use with your own game installation. The native component uses game-specific IL2CPP methods and memory structures, so compatibility may change with game updates. Native hooks also carry a risk of instability. Test changes incrementally and keep a backup of important saves.
