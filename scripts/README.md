# Remote Deck for OBS

This zip is the manual install. Most people should use the **windows-installer.exe** on the release page instead.

You need OBS Studio 32 or newer on Windows, and a Remote Deck studio. This plugin is made by Remote Deck, not by the OBS Project.

## Install

1. Quit OBS. In OBS, click **File**, then **Exit**.
2. Copy the `obs-remote-deck` folder into:

   `C:\ProgramData\obs-studio\plugins`

   When you are done you should have:

   `C:\ProgramData\obs-studio\plugins\obs-remote-deck\bin\64bit\obs-remote-deck.dll`

3. Open OBS. Click **Tools**, then **Remote Deck**.
4. Click **Authenticate with Remote Deck** and sign in in the browser that opens.

`ProgramData` is a hidden folder. Paste that path into File Explorer's address bar if you do not see it.

## If something is wrong

- **Tools → Remote Deck is missing.** Quit OBS with File → Exit, check the folder path above, then reopen OBS.
- **Sign-in does not work.** Check that this computer is online.
