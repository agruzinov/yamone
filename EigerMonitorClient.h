#ifndef EIGER_MONITOR_CLIENT_H
#define EIGER_MONITOR_CLIENT_H

#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

class EigerMonitorClient {
private:
  std::string host_;
  int port_;
  std::string version_;
  bool verbose_;
  std::string urlPrefix_;
  std::string user_;
  CURL *connection_;

  static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                              std::string *output) {
    output->append((char *)contents, size * nmemb);
    return size * nmemb;
  }

public:
  EigerMonitorClient(const std::string &host = "127.0.0.1", int port = 80,
                     bool verbose = false, const std::string &urlPrefix = "",
                     const std::string &user = "")
      : host_(host), port_(port), version_("1.8.0"), verbose_(verbose),
        urlPrefix_(urlPrefix), user_(user), connection_(nullptr) {
    connection_ = curl_easy_init();
    if (!connection_) {
      throw std::runtime_error("Failed to initialize CURL");
    }
  }

  ~EigerMonitorClient() {
    if (connection_) {
      curl_easy_cleanup(connection_);
    }
  }

  void setUrlPrefix(const std::string &urlPrefix) { urlPrefix_ = urlPrefix; }

  void setUser(const std::string &user) { user_ = user; }

  std::string _url(const std::string &module, const std::string &task,
                   const std::string &parameter = "") {
    std::string url =
        "/" + urlPrefix_ + module + "/api/" + version_ + "/" + task + "/";
    if (!parameter.empty()) {
      url += parameter;
    }
    return url;
  }

  std::string getUrl(const std::string &module, const std::string &task,
                     const std::string &parameter = "") {
    std::stringstream url;
    url << "http://" << host_ << ":" << port_ << "/" << urlPrefix_ << module
        << "/api/" << version_ << "/" << task << "/";
    if (!parameter.empty()) {
      url << parameter;
    }
    
    return url.str();
  }

  std::string _getRequest(const std::string &url,
                          const std::string &dataType = "native",
                          FILE *fileId = nullptr) {
    std::string response;
    CURLcode res;

    curl_easy_setopt(connection_, CURLOPT_URL, url.c_str());
    if (dataType == "native") {
      curl_easy_setopt(connection_, CURLOPT_ACCEPT_ENCODING,
                       "application/json; charset=utf-8");
    } else if (dataType == "tif") {
      curl_easy_setopt(connection_, CURLOPT_ACCEPT_ENCODING,
                       "application/tiff");
    } else if (dataType == "hdf5") {
      curl_easy_setopt(connection_, CURLOPT_ACCEPT_ENCODING,
                       "application/hdf5");
    }
    if (!user_.empty()) {
      curl_easy_setopt(connection_, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
      curl_easy_setopt(connection_, CURLOPT_USERPWD, user_.c_str());
    }
    curl_easy_setopt(connection_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(connection_, CURLOPT_WRITEDATA, &response);
    res = curl_easy_perform(connection_);
    if (res != CURLE_OK) {
      throw std::runtime_error("Failed to connect to host: " +
                               std::string(curl_easy_strerror(res)));
    }
    return response;
  }

  std::string _putRequest(const std::string &url, const std::string &dataType,
                          const std::string &data = "") {
    std::string preparedData;
    std::string mimeType;

    std::tie(preparedData, mimeType) = _prepareData(data, dataType);
    std::cout << url << std::endl;

    return _request(url, "PUT", mimeType, preparedData);
  }

  std::string _request(const std::string &url, const std::string &method,
                       const std::string &mimeType,
                       const std::string &data = "") {
    std::string response;
    CURLcode res;
    curl_easy_setopt(connection_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(connection_, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (method == "GET") {
      curl_easy_setopt(connection_, CURLOPT_ACCEPT_ENCODING, mimeType.c_str());
    } else if (method == "PUT") {
      curl_easy_setopt(connection_, CURLOPT_POSTFIELDS, data.c_str());
      curl_easy_setopt(connection_, CURLOPT_POSTFIELDSIZE, data.size());
    }
    curl_easy_setopt(connection_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(connection_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(connection_, CURLOPT_HTTPHEADER,
                     "Content-Type: " + mimeType);
    res = curl_easy_perform(connection_);
    if (res != CURLE_OK) {
      throw std::runtime_error("Failed to connect to host: " +
                               std::string(curl_easy_strerror(res)));
    }
    return response;
  }

  std::pair<std::string, std::string>
  _prepareData(const std::string &data, const std::string &dataType) {
    std::string preparedData;
    std::string mimeType;

    if (data.empty()) {
      return std::make_pair(preparedData, "text/html");
    }

    if (dataType != "native") {
      // Check if data is a file
      if (dataType.empty()) {
        mimeType = _guessMimeType(data);
        if (!mimeType.empty()) {
          return std::make_pair(data, mimeType);
        }
      } else if (dataType == "tif") {
        return std::make_pair(data, "application/tiff");
      }
    }

    mimeType = "application/json; charset=utf-8";

    return std::make_pair(data, mimeType);
  }

  std::string _guessMimeType(const std::string &data) {
    if (data.find("\x49\x49\x2A\x00") == 0 ||
        data.find("\x4D\x4D\x00\x2A") == 0) {
      // TIFF magic numbers
      _log("Determined mimetype: tiff");
      return "application/tiff";
    } else if (data.find("\x89\x48\x44\x46\x0d\x0a\x1a\x0a") == 0) {
      // HDF5 magic numbers
      _log("Determined mimetype: hdf5");
      return "application/hdf5";
    }
    return "";
  }

  std::string monitorImages(const std::string &param = "") {
    std::string url;
    if (param.empty()) {
      url = _url("monitor", "images");
    } else if (param == "next" || param == "monitor") {
      url = _url("monitor", "images", param);
    } else {
      // Extract sequence id and image id
      size_t pos = param.find('/');
      if (pos != std::string::npos) {
        std::string seqIdStr = param.substr(0, pos);
        std::string imgIdStr = param.substr(pos + 1);
        try {
          int seqId = std::stoi(seqIdStr);
          int imgId = std::stoi(imgIdStr);
          url = _url("monitor", "images",
                     std::to_string(seqId) + "/" + std::to_string(imgId));
        } catch (const std::invalid_argument &e) {
          // Handle invalid parameter
          throw std::runtime_error("Invalid parameter: " + param);
        }
      } else {
        throw std::runtime_error("Invalid parameter: " + param);
      }
    }

    // Make HTTP request based on constructed URL
    url = "http://" + host_ + ":" + std::to_string(port_) + url;
    return _getRequest(url, "tif");
  }

  std::string setMonitorConfig(const std::string &param,
                               const std::string &value) {
    // Construct the full URL with parameter and value
    std::string configUrl = "http://" + host_ + ":" + std::to_string(port_) +
                            "/monitor/api/1.8.0/config/" + param;

    std::cout << "Setting monitor config on " << configUrl << std::endl;

    // Make the PUT request to set the monitor configuration
    return _putRequest(configUrl, "native", value);
  }

  void deleteRequest(const std::string &url) {
    CURLcode res;
    curl_easy_setopt(connection_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(connection_, CURLOPT_CUSTOMREQUEST, "DELETE");
    if (!user_.empty()) {
      curl_easy_setopt(connection_, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
      curl_easy_setopt(connection_, CURLOPT_USERPWD, user_.c_str());
    }
    res = curl_easy_perform(connection_);
    if (res != CURLE_OK) {
      throw std::runtime_error("Failed to connect to host: " +
                               std::string(curl_easy_strerror(res)));
    }
  }

  void _log(const std::string &message) {
    if (verbose_) {
      std::cout << message << std::endl;
    }
  }
};

#endif
