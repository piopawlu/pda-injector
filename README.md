# Condor 2 PDA Injector
This project was created to enable embedding XCSoar (or any other Windows application) inside the Condor 2 Soaring Simulator's PDA for use in VR. While there are other methods, like a Virtual Desktop window placement, they do not provide the same level of immersion.

![Running XCSoar inside ASW28 PDA](https://github.com/piopawlu/pda-injector/blob/main/docs/xcsoar_pda_asw28.jpeg?raw=true "Running XCSoar inside ASW28 PDA")

The injection process works by proxying access to **d3dx11_43.dll** to substitute the PDA texture file for a higher resolution and hook to 3 GDI calls (LineTo, Ellipse and ExtTextOutA). The GDI hooks are used to detect PDA screen drawing and call the injection method after the last call. This was previously done via binary patching of the Condor app which would break multiplayer access. There's still a tiny bit of live runtime patching left, just two bytes, to change the texture size.

Currently only Condor2 2.2.0 is supported. I will try to maintain it when a new version appears and hopefully even in Condor 3 if necessary.

# Known issues

This is a beta release and there's at least one known issue and some limitations:

1. ~~Taking in-game screenshot (S key) casues the game to crash at the moment (2024/03/30)~~ (Solved in Beta 3)

2. ~~No effort has been made to provide any sort of control over the XCSoar at this point.~~ (Solved in Beta 1.4)

3. ~~Multiplayer is broken due to modified binary~~ (Solved in Beta 1.4, no more exe patching)

4. Symbols on the XCSoar screen may appear too small and be difficult to read. This can be solved by increasing the text size and DPI setting inside XCSoar.

# Installation

You can either build everything by yourself (Visual Studio Community Edition project is a part of this repo), or you can simply download a prebuilt binary from the Releases and follow the instructions in the README.txt

Go to [PDA Injector Releases](https://github.com/piopawlu/pda-injector/releases "PDA Injector Releases")

# Configuration

You can now configure some of the PDA Injector behaviour by editing the pda.ini file. 

By default the following config is used:

```
[app]
window=XCSoar

# Uncomment if you want to enable input passing to the target app
#pass_input=true

# If enabled, will pass all keys to the target app as long as shift key is held down
#pass_all_with_shift=false

# If pass_input is enabled above, you can enter up to 32 virtualkey codes that you want passed or mapped if you use VK1:VK2
# You can read the list of Virtual Keys at https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
#pass_key_1=0x4E:0x28 ; N -> down
#pass_key_2=0x48:0x25 ; H -> left
#pass_key_3=0x55:0x26 ; U -> up
#pass_key_4=0x4B:0x27 ; K -> right
#pass_key_5=0x4D:0x28 ; M -> down
#pass_key_6=0x4A:0x28 ; J -> enter
#pass_key_7=0x59:0x1B ; Y -> ESC
#pass_key_8=0x4A:0x0D ; J -> ENTER

[pda]
enabled=true
page=2
swap_colours=true
show_waiting_screen=true
```

The swap_colours option is needed to swap red and blue channels in the resulting bitmap. If you don't care about colours not matching you can disable that and potentially gain some performance on slower computers.
