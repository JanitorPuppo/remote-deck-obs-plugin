# Remote Deck for OBS

This plugin lets Remote Deck mute and change volume on the computer running OBS.

You need a Remote Deck studio. This plugin is made by Remote Deck, not by the OBS Project.

## Install on Windows

You need OBS Studio 32 or newer.

1. Quit OBS. In OBS, click **File**, then **Exit**. Closing the window is not always enough.
2. Download **obs-remote-deck-*-windows-installer.exe** from [Releases](https://github.com/JanitorPuppo/remote-deck-obs-plugin/releases).
3. Run the installer. If Windows asks for permission, click Yes.
4. Open OBS. Click **Tools**, then **Remote Deck**.
5. Click **Authenticate with Remote Deck** and sign in in the browser that opens.

That is the whole setup.

If Windows says it protected your PC, click **More info**, then **Run anyway**.

A zip is also on the release page if you would rather copy the files yourself. See that zip's README.

## If something is wrong

- **Tools → Remote Deck is missing.** Quit OBS with File → Exit, run the installer again, then reopen OBS.
- **Sign-in does not work.** Check that this computer is online. Use the installer from Releases, not a leftover test copy.

## Other platforms

Windows is what we ship today. Mac and Linux are not ready.

## For developers

How to build from source is in [BUILD.md](BUILD.md). The wire format is in [PROTOCOL.md](PROTOCOL.md).

## License

GPL-2.0-or-later.
