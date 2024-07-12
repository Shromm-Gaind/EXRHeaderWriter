#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <png.h>
#include <cmath>
#include <algorithm>
#include <opencv2/opencv.hpp>

#define INTEL_ORDER32(x) (x)
#define GCC_PACK __attribute__((packed))

struct Box2i {
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
} GCC_PACK;

struct Channel {
    char name[32];
    int32_t type;
    uint8_t pLinear;
    uint8_t reserved[3];
} GCC_PACK;

struct ExrHeader {
    uint32_t magic_number;
    uint32_t version;
    uint32_t chunkCount;
    Box2i dataWindow;
    Box2i displayWindow;
    float pixelAspectRatio;
    int32_t lineOrder;
    char channels[64];
    char compression[32];
} GCC_PACK;

void write_string_attr(char* buffer, const std::string& key, const std::string& value) {
    strcat(buffer, key.c_str());
    strcat(buffer, "\0");
    strcat(buffer, "string\0");
    int32_t length = value.length() + 1;
    memcpy(buffer + strlen(buffer), &length, sizeof(int32_t));
    strcat(buffer, value.c_str());
    strcat(buffer, "\0");
}

void write_int_attr(char* buffer, const std::string& key, int32_t value) {
    strcat(buffer, key.c_str());
    strcat(buffer, "\0");
    strcat(buffer, "int\0");
    int32_t length = sizeof(int32_t);
    memcpy(buffer + strlen(buffer), &length, sizeof(int32_t));
    memcpy(buffer + strlen(buffer), &value, sizeof(int32_t));
}

void write_box2i_attr(char* buffer, const std::string& key, const Box2i& box) {
    strcat(buffer, key.c_str());
    strcat(buffer, "\0");
    strcat(buffer, "box2i\0");
    int32_t length = sizeof(Box2i);
    memcpy(buffer + strlen(buffer), &length, sizeof(int32_t));
    memcpy(buffer + strlen(buffer), &box, sizeof(Box2i));
}

void write_channel_attr(char* buffer, const std::string& key, const std::vector<Channel>& channels) {
    strcat(buffer, key.c_str());
    strcat(buffer, "\0");
    strcat(buffer, "channels\0");
    int32_t length = channels.size() * sizeof(Channel) + 1;
    memcpy(buffer + strlen(buffer), &length, sizeof(int32_t));
    for (const auto& channel : channels) {
        strcat(buffer, channel.name);
        strcat(buffer, "\0");
        memcpy(buffer + strlen(buffer), &channel.type, sizeof(int32_t));
        memcpy(buffer + strlen(buffer), &channel.pLinear, sizeof(uint8_t));
        strcat(buffer, "\0\0\0"); // Reserved
    }
    strcat(buffer, "\0");
}

void create_exr_header(FILE* file, int width, int height) {
    ExrHeader header{};
    header.magic_number = INTEL_ORDER32(20000630);  // OpenEXR magic number
    header.version = INTEL_ORDER32(2);  // Version number
    header.chunkCount = 1;

    header.dataWindow = {0, 0, width - 1, height - 1};
    header.displayWindow = {0, 0, width - 1, height - 1};
    header.pixelAspectRatio = 1.0;
    header.lineOrder = INTEL_ORDER32(0);  // Increasing Y

    std::vector<Channel> channels = {{"Z", INTEL_ORDER32(2), 1, {0}}};  // 'Z' channel, 32-bit float
    memset(header.channels, 0, sizeof(header.channels));
    write_channel_attr(header.channels, "channels", channels);

    memset(header.compression, 0, sizeof(header.compression));
    strcpy(header.compression, "PIZ_COMPRESSION");

    fwrite(&header, sizeof(ExrHeader), 1, file);
}

std::vector<float> read_png_depth(const std::string& filename, int& width, int& height, float minVal, float maxVal) {
    // Read the PNG file using OpenCV
    cv::Mat depth_image = cv::imread(filename, cv::IMREAD_GRAYSCALE);
    if (depth_image.empty()) {
        std::cerr << "Failed to open PNG file: " << filename << std::endl;
        exit(1);
    }

    width = depth_image.cols;
    height = depth_image.rows;

    // Convert the depth image to a vector of floats
    std::vector<float> depth_data(width * height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float normalized_value = static_cast<float>(depth_image.at<uint8_t>(y, x)) / 255.0f;
            depth_data[y * width + x] = normalized_value * (maxVal - minVal) + minVal;
        }
    }

    return depth_data;
}

void write_depth_data_to_text(const std::string& filename, const std::vector<float>& depth_data, int width, int height, float minVal, float maxVal) {
    std::ofstream outfile(filename);
    if (!outfile) {
        std::cerr << "Failed to open text file for writing: " << filename << std::endl;
        return;
    }

    outfile << "Width: " << width << "\n";
    outfile << "Height: " << height << "\n";
    outfile << "Min Depth Value: " << minVal << "\n";
    outfile << "Max Depth Value: " << maxVal << "\n";
    //outfile << "Depth Data:\n";

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            outfile << depth_data[y * width + x] << " ";
        }
        outfile << "\n";
    }

    outfile.close();
}

std::vector<float> read_exr_depth(const std::string& filename, int& width, int& height) {
    FILE* file = fopen(filename.c_str(), "rb");
    if (!file) {
        std::cerr << "Failed to open EXR file: " << filename << std::endl;
        return {};
    }

    ExrHeader header;
    fread(&header, sizeof(ExrHeader), 1, file);

    width = header.dataWindow.max_x - header.dataWindow.min_x + 1;
    height = header.dataWindow.max_y - header.dataWindow.min_y + 1;

    std::vector<float> depth_data(width * height);
    fread(depth_data.data(), sizeof(float), width * height, file);

    fclose(file);
    return depth_data;
}
void write_png_depth(const std::string& filename, const std::vector<float>& depth_data, int width, int height, float minVal, float maxVal) {
    // Normalize depth data to 0-255 range
    cv::Mat depth_image(height, width, CV_8UC1);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float value = depth_data[y * width + x];
            uint8_t normalized_value = static_cast<uint8_t>(std::round(((value - minVal) / (maxVal - minVal)) * 255.0f));
            depth_image.at<uint8_t>(y, x) = normalized_value;

            // Print some normalized values for debugging
            if (y < 3 && x < 10) {  // Print first 10 values of the first 3 rows
                std::cout << "Row " << y << ", Col " << x << " - Original: " << value << ", Normalized: " << static_cast<int>(normalized_value) << std::endl;
            }
        }
    }

    // Write the normalized depth image to a PNG file using OpenCV
    if (!cv::imwrite(filename, depth_image)) {
        std::cerr << "Failed to write PNG file: " << filename << std::endl;
    }
}

bool compare_depth_data(const std::vector<float>& original_data, const std::vector<float>& exr_data, int width, int height, float tolerance = 1e-5) {
    if (original_data.size() != exr_data.size()) {
        std::cerr << "Data size mismatch: Original (" << original_data.size() << ") vs EXR (" << exr_data.size() << ")" << std::endl;
        return false;
    }

    for (size_t i = 0; i < original_data.size(); ++i) {
        if (std::fabs(original_data[i] - exr_data[i]) > tolerance) {
            std::cerr << "Mismatch at index " << i << ": Original (" << original_data[i] << ") vs EXR (" << exr_data[i] << ")" << std::endl;
            return false;
        }
    }

    std::cout << "Depth data is identical within the tolerance." << std::endl;
    return true;
}

std::vector<float> read_png_depth_new(const std::string& filename, int& width, int& height, float minVal, float maxVal) {
    cv::Mat depth_image = cv::imread(filename, cv::IMREAD_GRAYSCALE);
    if (depth_image.empty()) {
        std::cerr << "Failed to open PNG file: " << filename << std::endl;
        exit(1);
    }

    width = depth_image.cols;
    height = depth_image.rows;

    std::vector<float> depth_data(width * height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float normalized_value = static_cast<float>(depth_image.at<uint8_t>(y, x)) / 255.0f;
            depth_data[y * width + x] = normalized_value * (maxVal - minVal) + minVal;
        }
    }

    return depth_data;
}

bool compare_depth_images(const std::vector<float>& original_data, const std::vector<float>& new_data, int width, int height, float tolerance = 1e-5) {
    if (original_data.size() != new_data.size()) {
        std::cerr << "Data size mismatch: Original (" << original_data.size() << ") vs New (" << new_data.size() << ")" << std::endl;
        return false;
    }

    for (size_t i = 0; i < original_data.size(); ++i) {
        if (std::fabs(original_data[i] - new_data[i]) > tolerance) {
            std::cerr << "Mismatch at index " << i << ": Original (" << original_data[i] << ") vs New (" << new_data[i] << ")" << std::endl;
            return false;
        }
    }

    std::cout << "Depth images are identical within the tolerance." << std::endl;
    return true;
}


int main() {
    const std::string png_filename = "/home/eflinspy/CLionProjects/EXRHeader/img.png";
    const std::string exr_filename = "depth_image.exr";
    const std::string txt_filename = "depth_data.txt";
    const std::string orgtxt_filename = "orgdepth_data.txt";
    const std::string output_png_filename = "output_depth.png";

    float minVal = 71.4000015258789f; // Replace with actual minVal used during normalization
    float maxVal = 500.0f; // Replace with actual maxVal used during normalization

    int width, height;
    std::vector<float> depth_data = read_png_depth(png_filename, width, height, minVal, maxVal);
    if (depth_data.empty()) {
        return 1;
    }

    write_depth_data_to_text(orgtxt_filename, depth_data, width, height, minVal, maxVal);

    FILE* file = fopen(exr_filename.c_str(), "wb");
    if (!file) {
        std::cerr << "Failed to open EXR file: " << exr_filename << std::endl;
        return 1;
    }

    create_exr_header(file, width, height);
    fwrite(depth_data.data(), sizeof(float), width * height, file);
    fclose(file);

    // Read depth data from EXR and write it to a text file
    std::vector<float> read_depth_data = read_exr_depth(exr_filename, width, height);
    write_depth_data_to_text(txt_filename, read_depth_data, width, height, minVal, maxVal);

    // Compare the original and EXR depth data
    if (!compare_depth_data(depth_data, read_depth_data, width, height)) {
        std::cerr << "Depth data mismatch between original PNG and EXR file." << std::endl;
        return 1;
    }

    // Verify min and max values in the depth data
    float actual_min = *std::min_element(read_depth_data.begin(), read_depth_data.end());
    float actual_max = *std::max_element(read_depth_data.begin(), read_depth_data.end());
    std::cout << "Actual Min Depth Value: " << actual_min << std::endl;
    std::cout << "Actual Max Depth Value: " << actual_max << std::endl;

    // Write depth data from EXR to a new PNG file
    write_png_depth(output_png_filename, read_depth_data, width, height, minVal, maxVal);

    // Read the newly created PNG depth data
    int new_width, new_height;
    std::vector<float> new_depth_data = read_png_depth_new(output_png_filename, new_width, new_height, minVal, maxVal);

    // Compare the original PNG depth data with the newly created PNG depth data
    if (!compare_depth_images(depth_data, new_depth_data, width, height)) {
        std::cerr << "Depth data mismatch between original PNG and new PNG file." << std::endl;
        return 1;
    }

    return 0;
}
