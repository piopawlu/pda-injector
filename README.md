# Condor 3 PDA Injector
This project was created to enable embedding XCSoar (or any other Windows application) inside the Condor 2 Soaring Simulator's PDA for use in VR. While there are other methods, like a Virtual Desktop window placement, they do not provide the same level of immersion.

![Running XCSoar inside JS3-18 Navigation Computer](docs/xcsoar_pda_js3-18.jpeg?raw=true "Running XCSoar inside JS3-18 Navigation Computer")

The injection process works by proxying access to **d3dx11_43.dll** to substitute the PDA texture file for a higher resolution and hook to 3 GDI calls (LineTo, Ellipse and ExtTextOutA). The GDI hooks are used to detect PDA screen drawing and call the injection method after the last call. This was previously done via binary patching of the Condor app which would break multiplayer access. There's still a tiny bit of live runtime patching left, just two bytes, to change the texture size.

Currently only Condor3 3.0.2 is supported. I will try to maintain it and update the build within a few days when a new version appears.

# Known issues

This is a beta release and there's at least one known issue and some limitations:

1. ~~Taking in-game screenshot (S key) casues the game to crash at the moment (2024/03/30)~~ (Solved in Beta 3)

2. ~~No effort has been made to provide any sort of control over the XCSoar at this point.~~ (Solved in Beta 1.4)

3. ~~Multiplayer is broken due to modified binary~~ (Solved in Beta 1.4, no more exe patching)

# Installation

You can either build everything by yourself (Visual Studio Community Edition project is a part of this repo), or you can simply download a prebuilt binary from the Releases and follow the instructions in the README.txt

Go to [PDA Injector Releases](https://github.com/piopawlu/pda-injector/releases "PDA Injector Releases")

You can toggle between the built in PDA and XCSoar by using the F10 key (hardcoded for now)

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
swap_colours=true
show_waiting_screen=true

[joystick]
# If enabled, the PDA injector app will listen for joystick actions and pass selected button presses to either the target app or condor as keyboard events.
# This lets you use controllers with up to 128 buttons natively in condor.
# Syntax:
#
# VID:PID:X:BTN:S:VKEY
#
# VID and PID are hexadecimal VendorID and ProductID
# BTN is a decimal button number, use debug mode to identify buttons
# X is either B for button or S for switch (POV is a switch)
# S is 0 or 1 status, a change from !S to S will trigger the action, WK_KEYDOWN will be sent on S, and WK_KEYUP will be sent on !S
# TARGET is either C - Condor, or A - App (XCSoar/LK8000). Condor input is passed globally with SendInput API, app input is using window messages directly.
#
# Example:
#
# command_1=4098:BE62:20:1:0x28:A ; Will send 'DOWN' key to APP (XCSoar) when button 20 on Joystick 4098:BE62 changes from 0 to 1
#
# You can also enable the debug mode in which case whenever you press a button you will get a message box including the button number and state as well as optionally copy the command to be pasted in this file.

enabled=false
debug=false

command_1=4098:BEA8:B:36:1:26:A ; pov up as up to xcsoar
command_2=4098:BEA8:B:38:1:28:A ; pov dn as dn to xcsoar
command_3=4098:BEA8:B:39:1:25:A ; pov left as left to xcsoar
command_4=4098:BEA8:B:37:1:27:A ; pov right as right to xcsoar
command_5=4098:BEA8:B:35:1:0D:A ; pov center as enter to xcsoar
command_6=4098:BE62:B:05:1:70:A ; TH L as F1
command_7=4098:BE62:B:06:1:71:A ; TH R as F2

command_8=4098:BE62:B:20:1:@R:C ; btn 20 as R - Release
command_9=4098:BEA8:S:00:0:2D:C ; POV UP as Ins
command_10=4098:BEA8:S:00:5:2E:C ; POV DN as Del
command_11=4098:BEA8:B:18:1:7B:C ; POV CTR as F12

command_12=4098:BEA8:B:34:1:@1:C ; PDA 1
command_13=4098:BEA8:B:32:1:@2:C ; PDA 2
command_14=4098:BEA8:B:30:1:@3:C ; PDA 3
command_15=4098:BEA8:B:31:1:@4:C ; PDA 4
command_16=4098:BEA8:B:33:1:@M:C ; PDA Next

command_17=4098:BE62:B:15:1:@V:C ; Flaps down - V
command_18=4098:BE62:B:13:1:@F:C ; Flaps up - F
command_19=4098:BE62:B:72:1:@G:C ; Gear toggle - G
command_20=4098:BE62:B:79:1:@P:C ; Pause - P
command_21=4098:BE62:B:10:1:65:C ; Reset View - Num5
command_22=4098:BE62:B:95:1:@Q:C ; Miracle - Q
command_23=4098:BEA8:B:07:1:BE:C ; Wheel brake - .

mouse_enabled=true
mouse_x_axis=0
mouse_y_axis=1
mouse_button=21
mouse_vid=4098
mouse_pid=BE62
```

The swap_colours option is needed to swap red and blue channels in the resulting bitmap. If you don't care about colours not matching you can disable that and potentially gain some performance on slower computers.
