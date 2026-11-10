![logo](https://cdn.discordapp.com/attachments/339857108025475073/697158808547491840/logo.png)

Source code of VCSPC DLL file, the core of all code changes in Vice City Stories PC Edition.
This very code allowed VCSPC to become more than just a map mod for GTA: San Andreas.
Here you'll find countless changes to the game's engine we've worked on between 2012 and 2018.

Feel free to reuse parts of this code for your heart's content, but credit us if possible.
Thanks and enjoy!

## Experimental PS2 SCM support

This fork explores running the original *GTA: Vice City Stories* PS2 `MAIN.SCM` through the custom [reVCS](https://github.com/DasIstEin20/reVCS) script VM. The VM interprets VCS bytecode and mission threads, while an adapter connects supported commands to the GTA: San Andreas-based VCSPC engine and adds selected VCS-specific hooks. **This integration is experimental and incomplete; it is not a finished campaign port.**

See [`scm-vm/`](scm-vm/README.md) for build and configuration details.
