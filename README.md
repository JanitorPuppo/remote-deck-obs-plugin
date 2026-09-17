# Remote Deck for OBS

This plugin lets a Remote Deck producer within your studio to control OBS remotely and securely. Remote Deck handles user  permission management.  
  
This plugin facilitates the communication between your OBS and Remote Deck. Currently, this only supports volume and mute controls for audio sources.

## Install on Windows

You need OBS Studio 32 or newer.

1. Quit OBS. In OBS, click **File**, then **Exit**. Closing the window is not always enough.
2. Download **obs-remote-deck-*-windows-installer.exe** from [Releases](https://github.com/JanitorPuppo/remote-deck-obs-plugin/releases).
3. Run the installer. It installs to `C:\ProgramData\obs-studio\plugins` (OBS 32 loads Windows plugins from here) and will ask for administrator permission.
4. Open OBS. Click **Tools**, then **Remote Deck**.
5. Click **Authenticate with Remote Deck** and sign in in the browser that opens.

Updates from **Tools → Remote Deck** may show a UAC prompt. OBS must fully exit via **File → Exit**, not just the tray.

Release builds upload **latest.json** next to the installer so the plugin can verify downloads before installing.

## If something is wrong

- **Tools → Remote Deck is missing.** Quit OBS with File → Exit, run the installer again, then reopen OBS.
- **Sign-in does not work.** Check that this computer is online. Use the installer from Releases, not a leftover test copy.

## License

GPL-2.0-or-later.