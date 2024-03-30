# Condor 2 PDA Injector
This project was created to enable embdding XCSoar (or any other windows appliocation) inside the Condor 2 Soaring Simulator's PDA for use in VR. While there are other methods, like a Virtual Desktop window placement, they do not provide the same level of immersion. They do make it easier to read XCSoar though ;-)

![Running XCSoar inside ASW28 PDA](https://github.com/piopawlu/pda-injector/blob/main/docs/xcsoar_pda_asw28.jpeg?raw=true "Running XCSoar inside ASW28 PDA")

It consists of two elements:
1. A dynamic linked library (PDAInjector.dll) that contains the code responsible for capturing XCSoar and putting it onto the HDC of the PDA texture.

2. A patch for the original Condor.exe binary that facilitates the injection and changes a few bits of code responsible for texture handling. 

There's currently no ASM listing provided as it's not finalized and could potentially result in some licensing issues. You can, however, open the patched binary in a free disassembler (like IDA Free for example) and go to the address 0x0080A000 to see the listing.

Currently only Condor2 2.2.0 is supported. I will try to maintain it when a new version appears and hopefully even in Condor 3 if necessary.

# Known issues

This is a beta release and there's at least one known issue and some limitations:

~~1. Taking in-game screenshot (S key) casues the game to crash at the moment (2024/03/30)~~ (Solved in Beta 3)

2. No effort has been made to provide any sort of control over the XCSoar at this point.

3. Symbols on the XCSoar screen may appear too small and be difficult to read. This can be solved by increasing the text size and DPI setting inside XCSoar.

# Installation

You can either build everything by yourself (Visual Studio Community Edition project is a part of this repo), or you can simply download a prebuilt binary from the Releases and follow the instructions in the README.txt

Go to [PDA Injector Releases](https://github.com/piopawlu/pda-injector/releases "PDA Injector Releases")

# Configuration

You can now configure some of the PDA Injector behaviour by editing the pda.ini file. 

By default the following config is used:

```
[app]
window=XCSoar

[pda]
enabled=true
page=2
swap_colours=true
show_waiting_screen=true
```

The swap_colours option is needed to swap red and blue channels in the resulting bitmap. If you don't care about colours not matching you can disable that and potentially gain some performance on slower computers.