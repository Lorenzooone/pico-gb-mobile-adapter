# Pico GB Mobile Adapter

USB implementation for the Pico of the [REON libmobile project](https://github.com/REONTeam/libmobile).

A basic implementation is available for the Stacksmashing board, though changing the used pins should make it possible to use other boards as well.

Make and install the firmware onto your pico board. Instructions available below.

Once that is done, connect your device to a USB port and launch usb_pico_interface.py, in order to offer an interface to the Pico for the internet.
Finally, connect the device to a Game Boy using a Link Cable, to emulate the GB Mobile Adapter.

## Build dependencies

### On Debian:

```
sudo apt install git build-essential cmake gcc-arm-none-eabi
```

Your Linux distribution does need to provide a recent CMake (3.25+).
If not, compile [CMake from source](https://cmake.org/download/#latest) first.

### On Windows:

- Install ARM GCC compiler: https://developer.arm.com/downloads/-/gnu-rm
- Install MSYS
- Install CMake

Then use this command 
```
cmake .. -G "MSYS Makefiles"
```

instead of
```
cmake ..
```
in the build instructions below.

## Build instructions

```
git clone https://github.com/Lorenzooone/pico-gb-switch
cd pico-gb-switch
git checkout main
git submodule update --init
mkdir -p build
cd build
cmake ..
make
```

Copy the resulting pico_gb_mobile_adapter.uf2 file to the Pi Pico mass storage device manually.
