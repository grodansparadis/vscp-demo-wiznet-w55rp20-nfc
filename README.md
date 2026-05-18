# vscp-demo-wiznet-w55rp20-nfc
VSCP demo code for a distributed nfc lock mechanism using VSCP link and multicast protocol


## Connecting and enetering Boot Mod

Because it uses the RP2040 core, programming the W55RP20-EVB-Pico via USB requires a simple button combination:

1. Connect the W55RP20-EVB-Pico to your computer via USB.
2. Press and hold the BOOTSEL button on the W55RP20-EVB-Pico.
3. While holding the BOOTSEL button, press and release the RESET button.
4. Release the BOOTSEL button. Your computer will mount the board as a USB mass storage drive called RPI-RP2.
5. You can now copy the compiled firmware (e.g., `firmware.uf2`) to the RPI-RP2 drive to flash it onto the W55RP20-EVB-Pico.


## Firmware startup project

A CMake-based firmware startup project is available in `/firmware` and expects:

- `firmware/submodules/pico-sdk`
- `firmware/submodules/ioLibrary_Driver`

to be populated as git submodules.
