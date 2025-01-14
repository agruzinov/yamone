//Compile:
//sudo apt-get install libtiff-dev
//g++ main.cpp -o yamone -ltiff -ltango -lcurl -lomniORB4 -g -lomnithread -lCOS4 -lomniDynamic4 -lomniCodeSets4 -I/usr/include/tango

//Eiger interface returns tif file as uint32. Read the result with libtiff, strip the heaser and resave it under /tmp/eiger_monitor
//to make sure that the pixels are interpreted correctly.
#include "EigerMonitorClient.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib> // For setenv
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <tango.h>
#include <thread> // For std::this_thread::sleep_for
#include <unistd.h>
#include <vector>
#include <tiffio.h>

// Custom TIFF error handler to suppress warnings
void tiffErrorHandler(const char* module, const char* fmt, va_list ap) {
    // Do nothing, suppress warnings
}

class MonitorReceiver {
private:
    std::string ip_;
    int port_;
    std::string name_;
    std::string imageFilename_;
    std::vector<uint32_t> previousData_;
    std::string beamCenterFile_;
    std::pair<double, double> imageDimensions_;
    double bcX_;
    double bcY_;
    double dDistance_;
    double incidentEnergy_;
    double incidentWavelength_;
    double pixX_;
    double pixY_;
    EigerMonitorClient client_;

public:
    MonitorReceiver(const std::string &ip, int port = 80, const std::string &name = "")
        : ip_(ip), port_(port),
          name_(name.empty() ? "EIGER_" + ip + "_" + std::to_string(port) : name),
          imageFilename_("/tmp/eiger_monitor"),
          beamCenterFile_("/tmp/.adxv_beam_center"), imageDimensions_(4148, 4362),
          client_(ip, port) {
        // Initialize other attributes
        TIFFSetWarningHandler(tiffErrorHandler); // Set custom TIFF error handler
    }

    ~MonitorReceiver() {
        // Destructor
    }

    bool saveImage(const std::vector<uint8_t> &imageData) {
        // Create a temporary file to write the incoming data
        std::string tempFilename = "/tmp/temp_image.tiff";
        std::ofstream tempFile(tempFilename, std::ios::binary);
        tempFile.write(reinterpret_cast<const char*>(imageData.data()), imageData.size());
        tempFile.close();

        // Open the temporary file with libtiff to read the header information
        TIFF* tif = TIFFOpen(tempFilename.c_str(), "r");
        if (!tif) {
            std::cerr << "Error: Unable to open TIFF file" << std::endl;
            return false;
        }

        // Read the image dimensions
        uint32_t width = 0, height = 0;
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
        imageDimensions_ = {width, height};

        // Read the pixel size
        double pixelSizeX = 0.0, pixelSizeY = 0.0;
        TIFFGetField(tif, TIFFTAG_XRESOLUTION, &pixelSizeX);
        TIFFGetField(tif, TIFFTAG_YRESOLUTION, &pixelSizeY);
        pixX_ = pixelSizeX;
        pixY_ = pixelSizeY;

        // Calculate the number of strips
        uint32_t stripSize = TIFFStripSize(tif);
        uint32_t numStrips = TIFFNumberOfStrips(tif);

        // Read the image data from the strips
        std::vector<uint32_t> imageArray(width * height);
        uint8_t* buf = (uint8_t*)_TIFFmalloc(stripSize);

        for (uint32_t strip = 0; strip < numStrips; ++strip) {
            uint32_t offset = strip * stripSize;
            if (TIFFReadEncodedStrip(tif, strip, buf, stripSize) == -1) {
                std::cerr << "Error: Failed to read strip " << strip << std::endl;
                _TIFFfree(buf);
                TIFFClose(tif);
                return false;
            }

            // Copy the strip data to the image array
            std::memcpy(reinterpret_cast<uint8_t*>(&imageArray[0]) + offset, buf, stripSize);
        }

        _TIFFfree(buf);
        TIFFClose(tif);

        // Compare new image data with previous data
        if (previousData_.empty() ||
            !std::equal(previousData_.begin(), previousData_.end(), imageArray.begin())) {

            try {
                double bcX = 0.0, bcY = 0.0, dDistance = 0.0, incidentEnergy = 0.0,
                       incidentWavelength = 0.0;

                Tango::DeviceProxy device("<Put Tango adress of the detector interface here>");

                // Read attributes
                Tango::DeviceAttribute att_reply;

                att_reply = device.read_attribute("BeamCenterX");
                att_reply >> bcX;

                att_reply = device.read_attribute("BeamCenterY");
                att_reply >> bcY;

                att_reply = device.read_attribute("DetectorDistance");
                att_reply >> dDistance;

                att_reply = device.read_attribute("IncidentEnergy");
                att_reply >> incidentEnergy;

                incidentWavelength = 12400.0 / incidentEnergy; // Convert energy to wavelength (assuming energy is in eV)

                // Write the beam center file
                writeBeamCenterFile(bcX, bcY);

                // Construct the header
                std::string header = R"({
                    HEADER_BYTES=512;
                    DIM=2;
                    BYTE_ORDER=little_endian;
                    TYPE=unsigned_int;
                    SIZE1=)" + std::to_string(width) +
                                   R"(;
                    SIZE2=)" + std::to_string(height) +
                                   R"(;
                    PIXEL_SIZE=)" + std::to_string(pixelSizeX) +
                                   R"(;
                    BEAM_CENTER_X=)" + std::to_string(bcX * pixelSizeX) +
                                   R"(;
                    BEAM_CENTER_Y=)" + std::to_string(bcY * pixelSizeY) +
                                   R"(;
                    DISTANCE=)" + std::to_string(dDistance * 1000) +
                                   R"(;
                    WAVELENGTH=)" + std::to_string(incidentWavelength) +
                                   R"(;
                })";

                // Write the image data along with the header to a file
                std::ofstream file(imageFilename_, std::ios::binary);
                // Ensure header is 512 bytes
                file << header;
                std::string padding(512 - header.size(), ' ');
                file << padding;

                file.write(reinterpret_cast<const char *>(imageArray.data()), imageArray.size() * sizeof(uint32_t));
                file.close();
            } catch (Tango::DevFailed &e) {
                Tango::Except::print_exception(e);
                return false;
            }

            // Update previous data
            previousData_ = imageArray;
            return true;
        } else {
            // Image data has not changed
            return false;
        }
    }

    void writeBeamCenterFile(double beamX, double beamY) {
        std::ofstream file(beamCenterFile_);
        if (file.is_open()) {
            file << beamX << " " << beamY << " " << imageDimensions_.first << " "
                 << imageDimensions_.second;
            file.close();
        } else {
            throw std::runtime_error("Unable to open beam center file for writing.");
        }
    }

    void enableMonitor() {
        // Logging
        std::cout << "Enabling monitor on " << ip_ << std::endl;

        try {
            // Call the setMonitorConfig method of the client
            client_.setMonitorConfig("mode", "enabled");
            std::cerr << "Monitor on " << ip_ << ":" << port_ << " enabled" << std::endl;
        } catch (const std::exception &e) {
            // Error handling
            std::cerr << "Error enabling monitor on " << ip_ << ": " << e.what() << std::endl;
            // You can handle the error as needed, such as throwing an exception
            throw;
        }
    }

    std::vector<uint8_t> receive() {
        // Logging
        // std::cerr << "Monitor receiver " << name_ << " polling " << ip_ << ":" << port_ << std::endl;

        try {
            // Call the monitorImages method of the client
            std::string frame = client_.monitorImages("monitor");

            // Convert the string to a vector of uint8_t
            std::vector<uint8_t> imageData(frame.begin(), frame.end());

            return imageData;
        } catch (const std::exception &e) {
            // Error handling
            std::cerr << "Monitor " << name_ << " error: " << e.what() << std::endl;
            // You can handle the error as needed, such as throwing an exception
            throw;
        }
    }

    std::vector<uint8_t> processFrames(const std::vector<uint8_t> &frame) {
        return frame;
    }

    void showImageInADXV() {

        try {
            // ADXV connection details
            std::string adxvHost = "127.0.0.1";
            int adxvPort = 8100;

            // Create a socket
            int adxvSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (adxvSocket < 0) {
                throw std::runtime_error("Failed to create socket");
            }

            // Server address structure
            struct sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(adxvPort);
            inet_pton(AF_INET, adxvHost.c_str(), &serverAddr.sin_addr);

            // Connect to the server
            if (connect(adxvSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
                close(adxvSocket);
                throw std::runtime_error("Failed to connect to ADXV server");
            }

            // Prepare the message to send
            std::string message = "load_image " + imageFilename_ + "\n";

            // Send the message
            ssize_t bytesSent = send(adxvSocket, message.c_str(), message.length(), 0);
            if (bytesSent < 0) {
                close(adxvSocket);
                throw std::runtime_error("Failed to send notification to ADXV");
            } else if (bytesSent != static_cast<ssize_t>(message.length())) {
                close(adxvSocket);
                throw std::runtime_error("Incomplete message sent to ADXV");
            }

            // Close the socket
            close(adxvSocket);
        } catch (const std::exception &e) {
            // Error handling
            std::cerr << "Error in ADXV notification: " << e.what() << std::endl;
            // You can handle the error as needed, such as throwing an exception
            throw;
        }
    }

    void run() {
        // TODO: Check why it is going to the segfault.
        // enableMonitor();
        while (true) {
            try {
                auto frame = receive();
                if (!frame.empty()) {
                    auto data = processFrames(frame);
                    if (saveImage(data)) {
                        std::cout << "Image received from " << name_ << " and saved as "
                                  << imageFilename_ << std::endl;
                        showImageInADXV();
                    }
                }
            } catch (const std::exception &e) {
                std::cerr << "Error in monitor " << name_ << ": " << e.what() << std::endl;
            }
        }
    }
};

bool isProcessRunning(const std::string &processName) {
    FILE *pipe = popen(("pgrep -x " + processName).c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    char buffer[128];
    std::string result = "";
    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr) {
            result += buffer;
        }
    }
    pclose(pipe);
    return !result.empty();
}

int main(int argc, char *argv[]) {
    std::string adxvProcessName = "adxv";

    if (!isProcessRunning(adxvProcessName)) {
        // Start ADXV in a disconnected way with output redirected to /dev/null
        std::system("/opt/xray/bin/adxv -socket -rings > /dev/null 2>&1 &");
    } else {
        std::cout << "ADXV is already running." << std::endl;
    }

    // // Wait for ADXV to start (adjust the sleep duration as needed)
    // std::this_thread::sleep_for(std::chrono::seconds(1)); // Wait for 5 seconds

    MonitorReceiver monitorReceiver("<Set detector IP adress here>");
    monitorReceiver.run();

    return 0;
}
