#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <png.h>


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
    char attributes[1024]; // Increased size to ensure space for attributes
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
    header.magic_number = INTEL_ORDER32(0x762f5020);
    header.version = INTEL_ORDER32(2);

    char* attributes = header.attributes;
    memset(attributes, 0, sizeof(header.attributes));

    Box2i data_window = {0, 0, width - 1, height - 1};
    Box2i display_window = {0, 0, width - 1, height - 1};
    Channel channel = {"Z", INTEL_ORDER32(2), 1, {0}}; // 'Z' channel, 32-bit float

    write_box2i_attr(attributes, "dataWindow", data_window);
    write_box2i_attr(attributes, "displayWindow", display_window);
    write_int_attr(attributes, "lineOrder", 0); // Increasing Y
    write_channel_attr(attributes, "channels", {channel}); // Single 'Z' channel
    write_string_attr(attributes, "compression", "PIZ_COMPRESSION");
    write_int_attr(attributes, "pixelAspectRatio", 1);

    fwrite(&header, sizeof(ExrHeader), 1, file);
    fwrite(attributes, sizeof(header.attributes), 1, file);
    fputc('\0', file);
}

std::vector<float> read_png_depth(const std::string& filename, int& width, int& height, float minVal, float maxVal) {
    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp) {
        std::cerr << "Failed to open PNG file: " << filename << std::endl;
        exit(1);
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        std::cerr << "Failed to create PNG read struct" << std::endl;
        fclose(fp);
        exit(1);
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        std::cerr << "Failed to create PNG info struct" << std::endl;
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        exit(1);
    }

    if (setjmp(png_jmpbuf(png))) {
        std::cerr << "Error during PNG creation" << std::endl;
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        exit(1);
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    width = png_get_image_width(png, info);
    height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16)
        png_set_strip_16(png);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);

    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);

    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    std::vector<png_bytep> row_pointers(height);
    for (int y = 0; y < height; y++) {
        row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png, info));
    }

    png_read_image(png, row_pointers.data());

    fclose(fp);

    std::vector<float> depth_data(width * height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float normalized_value = static_cast<float>(row_pointers[y][x]) / 255.0f;
            depth_data[y * width + x] = normalized_value * (maxVal - minVal) + minVal;
        }
    }

    for (int y = 0; y < height; y++) {
        free(row_pointers[y]);
    }
    png_destroy_read_struct(&png, &info, NULL);

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
    outfile << "Depth Data:\n";

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            outfile << depth_data[y * width + x] << " ";
        }
        outfile << "\n";
    }

    outfile.close();
}

int main() {
    const std::string png_filename = "/home/eflinspy/CLionProjects/EXRHeader/img.png";
    const std::string exr_filename = "depth_image.exr";
    const std::string txt_filename = "depth_data.txt";

    // These values need to be the same as those used during normalization
    float minVal = 71.4000015258789f; // minVal
    float maxVal = 500.0f; //  maxVal

    int width, height;
    std::vector<float> depth_data = read_png_depth(png_filename, width, height, minVal, maxVal);
    if (depth_data.empty()) {
        return 1;
    }

    FILE* file = fopen(exr_filename.c_str(), "wb");
    if (!file) {
        std::cerr << "Failed to open EXR file: " << exr_filename << std::endl;
        return 1;
    }

    create_exr_header(file, width, height);

    fwrite(depth_data.data(), sizeof(float), width * height, file);

    fclose(file);

    write_depth_data_to_text(txt_filename, depth_data, width, height, minVal, maxVal);

    return 0;
}