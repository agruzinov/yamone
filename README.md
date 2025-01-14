# Yamone

_Yamone_ is a C++ tool designed to interact with Eiger X-ray detectors. It retrieves TIFF data from the Eiger monitoring interface, processes the output, and shows the image using [ADXV](https://www.scripps.edu/tainer/arvai/adxv.html). The project utilizes several libraries to manage HTTP connections, read TIFF files, and handle network communications. Inspired by [EIGERStreamReceiver](https://github.com/SaschaAndresGrimm/EIGERStreamReceiver).

## Dependencies

This application requires the following libraries and tools:

1. **libtiff-dev** - For handling TIFF images.
2. **libtango** - For interfacing with Tango Controls.
3. **libcurl** - For HTTP communication with the detector.
4. **omniORB** - For CORBA support in Tango Controls:
   - `libomniorb4`
   - `libomnithread`
   - `libcos4`
   - `libomnidynamic4`
   - `libomnicodesets4`

On Debian-based systems, you can install these dependencies with:

```bash
sudo apt-get install libtiff-dev libtango-dev libcurl4-openssl-dev libomniorb4-dev libomnithread-dev libcos4-dev libomnidynamic4-dev libomnicodesets4-dev
```
---
## Configuration:
   
   - Set the detector IP address in the 'monitorReceiver()' constructor in `main.cpp` before running the program.
   - Set the Tango adress of the detector in 'Tango::DeviceProxy device()' in 'main.cpp'.
   - Ensure `ADXV` is installed and in your `PATH`. The program checks if [ADXV](https://www.scripps.edu/tainer/arvai/adxv.html) is available (path is currently hardcoded in main.cpp).

## Compilation

```bash
g++ main.cpp -o yamone -ltiff -ltango -lcurl -lomniORB4 -g -lomnithread -lCOS4 -lomniDynamic4 -lomniCodeSets4 -I/usr/include/tango
```
---

## Usage 

**Run the application with:**
   ```bash
   ./yamone
   ```
   It should open the ADXV window and start to wait for the new images from monitoring interface. The recent monitoring interface image will be saved under `/tmp/eiger_monitor`. The beam center information is written to `/tmp/.adxv_beam_center` and used by ADXV. Images are automatically displayed in ADXV once the new one is arrived through the monitoring interface.

## TODO

Make detector parameters configurable.
