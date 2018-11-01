# frodo-go
Commodore 64 emulation for ODROID-GO

To use, you will need to do the following steps:

1) Purchase and receive the ODROID-GO QWERTY Keyboard which is required for this emulator.

2) Create the below directory on your SD card.
```
/roms/c64/
```
3) Place the following 4 BIOS files in the /roms/c64/ directory, named exactly as shown.
```
1541.ROM
Basic.ROM
Char.ROM
Kernal.ROM
```
4) Place your .D64 files in the /roms/c64/ directory. Only D64 format is supported at this time.

# Keyboard Map
Below is the current keyboard mapping graphic for this emulator.
![QWERTY Map](https://i.imgur.com/i7CNOGj.png)

Please note the symbols above the number keys have changed to match the original C64.
Red means the keys were remapped from what is shown on the actual keyboard.
Blue means that remapping is only active when Fn-lock is on.
H = "HOME / CLR"
RE = "RESTORE"
RS = "Right Shift"
