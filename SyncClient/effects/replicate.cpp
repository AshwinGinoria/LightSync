#include "../led_strip.hpp"
#include "../logger.hpp"
#include "effect.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <opencv2/opencv.hpp>
#include <wrl/client.h>

#include <array>
#include <numeric>
#include <vector>

using Microsoft::WRL::ComPtr;

inline std::string wide_to_utf8(const std::wstring &wstr) {
    if (wstr.empty()) return {};

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr,
                                          0, nullptr, nullptr);

    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, result.data(),
                        size_needed, nullptr, nullptr);
    result.pop_back(); // remove null terminator
    return result;
}

class Replicate : public Effect {
  private:
    // Parameters
    int dead_leds;
    int n_pixels;
    int border_length;
    int fps;
    int frame_number = 0;

    std::vector<std::array<uint8_t, 3>> _leds;
    int _height;
    int _width;

    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIOutputDuplication> duplication;

    inline float ConvertHalfToFloat(uint16_t value) {
        uint32_t t1 = value & 0x7fff; // Non-sign bits
        uint32_t t2 = value & 0x8000; // Sign bit
        uint32_t t3 = value & 0x7c00; // Exponent

        t1 <<= 13;               // Align mantissa on MSB
        t2 <<= 16;               // Shift sign bit into position
        t1 += 0x38000000;        // Adjust bias
        t1 = (t3 == 0 ? 0 : t1); // Denormals-as-zero
        t1 |= t2;                // Re-insert sign bit
        float f;
        memcpy(&f, &t1, sizeof(f)); // Reinterpret bits as float
        return f;
    }

    void calc_lights(std::vector<std::array<uint8_t, 3>> &leds) {
        LOGGER.debug("Calculating lights for {} pixels", n_pixels);
        int i = 0;
        cv::Mat image = get_frame(_width, _height);
        LOGGER.debug("Got lights for {} pixels", n_pixels);
        frame_number++;

        // If failed to capture frame keep previous frame
        if (image.empty()) {
            LOGGER.error("Failed to capture frame.");
            return;
        }

        LOGGER.debug("Captured frame {}", frame_number);
        std::string filename =
            "frames/frame" + std::to_string(frame_number) + ".png";
        cv::imwrite(filename, image);

        // Dead LEDs
        while (i < dead_leds) leds[i++] = {0, 0, 0};

        // Left
        for (int j = _height - 2; j >= 0; --j)
            leds[i++] = to_rgb(image.at<cv::Vec3b>(j, 0));

        // Top
        for (int j = 1; j < _width; ++j)
            leds[i++] = to_rgb(image.at<cv::Vec3b>(0, j));

        // Right
        for (int j = 1; j < _height; ++j)
            leds[i++] = to_rgb(image.at<cv::Vec3b>(j, _width - 1));

        // Bottom
        for (int j = _width - 2; j >= 0; --j)
            leds[i++] = to_rgb(image.at<cv::Vec3b>(_height - 1, j));

        // Dead LEDs
        while (i < n_pixels) leds[i++] = {0, 0, 0};
    }

    std::array<uint8_t, 3> to_rgb(const cv::Vec3b &pixel) {
        return {pixel[0], pixel[1], pixel[2]};
    }

    bool initialize_dxgi_capture() {
        // Specify minimum feature level for HDR support
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0, // Minimum for
                                    // DXGI_FORMAT_R16G16B16A16_FLOAT
            D3D_FEATURE_LEVEL_10_0  // Fallback
        };

        HRESULT hr = D3D11CreateDevice(
            nullptr, // Default adapter
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,                           // No software rasterizer
            D3D11_CREATE_DEVICE_VIDEO_SUPPORT, // Enable video features for
                                               // better performance
            featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
            &d3dDevice,
            nullptr, // Don't need feature level output
            &d3dContext);

        if (FAILED(hr)) {
            // Try WARP as fallback (software rendering)
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                   D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                                   featureLevels, _countof(featureLevels),
                                   D3D11_SDK_VERSION, &d3dDevice, nullptr,
                                   &d3dContext);

            if (FAILED(hr)) {
                return false;
            }
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = d3dDevice.As(&dxgiDevice);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDXGIOutput> output;
        hr = adapter->EnumOutputs(0, &output); // Primary monitor only
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr)) {
            return false;
        }

        DXGI_OUTPUT_DESC outputDesc;
        output->GetDesc(&outputDesc);

        hr = output1->DuplicateOutput(d3dDevice.Get(), &duplication);
        if (FAILED(hr)) {
            // Retry with Unknown interface (less common, but can work around
            // some driver issues)
            ComPtr<IUnknown> unknownDevice;
            d3dDevice.As(&unknownDevice);
            hr = output1->DuplicateOutput(unknownDevice.Get(), &duplication);
            if (FAILED(hr)) {
                return false;
            }
        }

        return true;
    }

    cv::Mat get_frame(int width = 640, int height = 360) {
        ComPtr<IDXGIResource> desktopResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;

        HRESULT hr =
            duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);
        if (FAILED(hr)) {
            return cv::Mat();
        }

        ComPtr<ID3D11Texture2D> acquiredTexture;
        desktopResource.As(&acquiredTexture);

        D3D11_TEXTURE2D_DESC desc;
        acquiredTexture->GetDesc(&desc);

        ComPtr<ID3D11Texture2D> cpuTexture;
        D3D11_TEXTURE2D_DESC cpuDesc = desc;
        cpuDesc.Usage = D3D11_USAGE_STAGING;
        cpuDesc.BindFlags = 0;
        cpuDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        cpuDesc.MiscFlags = 0;

        hr = d3dDevice->CreateTexture2D(&cpuDesc, nullptr, &cpuTexture);
        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            return cv::Mat();
        }

        d3dContext->CopyResource(cpuTexture.Get(), acquiredTexture.Get());

        D3D11_MAPPED_SUBRESOURCE resource;
        hr = d3dContext->Map(cpuTexture.Get(), 0, D3D11_MAP_READ, 0, &resource);
        if (FAILED(hr)) {
            duplication->ReleaseFrame();
            return cv::Mat();
        }

        LOGGER.info("{}: Width {}, Height {}, RowPitch {}, Format {}", name,
                    desc.Width, desc.Height, resource.RowPitch, desc.Format);

        cv::Mat rgb(height, width, CV_8UC3);
        static cv::Mat temp;

        if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
            if (temp.empty() || temp.cols != desc.Width ||
                temp.rows != desc.Height || temp.type() != CV_8UC4)
                temp = cv::Mat(desc.Height, desc.Width, CV_8UC4);

            uint8_t *src = static_cast<uint8_t *>(resource.pData);
            uint8_t *dst = temp.ptr<uint8_t>();
            size_t rowBytes = desc.Width * 4;
            for (UINT i = 0; i < desc.Height; i++) {
                memcpy(dst, src, rowBytes);
                src += resource.RowPitch;
                dst += rowBytes;
            }

            cv::Mat tempRGB;
            cv::cvtColor(temp, tempRGB, cv::COLOR_BGRA2RGB);
            cv::resize(tempRGB, rgb, cv::Size(width, height), 0, 0,
                       cv::INTER_NEAREST);
        } else if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
            if (temp.empty() || temp.cols != desc.Width ||
                temp.rows != desc.Height || temp.type() != CV_32FC4)
                temp = cv::Mat(desc.Height, desc.Width, CV_32FC4);

            // Direct memory copy for float data
            uint8_t *srcBytes = static_cast<uint8_t *>(resource.pData);
            float *dst = temp.ptr<float>();

            for (UINT i = 0; i < desc.Height; i++) {
                uint16_t *srcRow = reinterpret_cast<uint16_t *>(srcBytes);
                for (UINT j = 0; j < desc.Width * 4; j++) {
                    dst[j] = ConvertHalfToFloat(srcRow[j]);
                }
                srcBytes += resource.RowPitch;
                dst += desc.Width * 4;
            }

            // Optimized HDR conversion
            cv::Mat rgb32f(temp.rows, temp.cols, CV_32FC3);
            int fromTo[] = {0, 0, 1, 1, 2, 2}; // R,G,B channels
            cv::mixChannels(&temp, 1, &rgb32f, 1, fromTo, 3);

            // Resize and convert in one step
            cv::resize(rgb32f, rgb32f, cv::Size(width, height), 0, 0,
                       cv::INTER_NEAREST);
            cv::min(rgb32f, 1.0f, rgb32f);
            cv::max(rgb32f, 0.0f, rgb32f);
            rgb32f.convertTo(rgb, CV_8UC3, 255.0f);
        } else {
            LOGGER.error("Unsupported format: {}", desc.Format);
            d3dContext->Unmap(cpuTexture.Get(), 0);
            duplication->ReleaseFrame();
            return cv::Mat();
        }

        d3dContext->Unmap(cpuTexture.Get(), 0);
        duplication->ReleaseFrame();

        return rgb;
    }

    void Effect::set_parameter(const std::string &key,
                               const Parameter &value) override {
        if (key == "DeadPixels")
            set_int_parameter(dead_leds, value);
        else if (key == "Npixels")
            set_int_parameter(n_pixels, value);
        else if (key == "BorderLength")
            set_int_parameter(border_length, value);
        else if (key == "FPS") {
            set_int_parameter(fps, value);
            interval_ms = 1000 / fps;
        } else
            LOGGER.error("Undefined Paramter {} for effect {}", key, name);

        update_target_dimensions();
    }

    void update_target_dimensions(float aspect_ratio = 16.0f / 9.0f) {
        if (n_pixels > 0) {
            float total = (n_pixels - dead_leds + 4) / 2.0f;
            _height = total / (1.0 + aspect_ratio);
            _width = total / (1.0 / aspect_ratio + 1.0);
        }

        LOGGER.info("Target Parameters Updated - Height: {}, Width: {}",
                    _height, _width);
    }

  public:
    Replicate(int target_fps = 60, int n_pixels = 288, int dead_leds = 2)
        : Effect("Replicate", 1000 / target_fps), dead_leds(dead_leds),
          fps(target_fps), n_pixels(n_pixels), border_length(0) {
        if (initialize_dxgi_capture()) {
            LOGGER.info("DXGI Capture Initialized Successfully.");
        } else {
            LOGGER.error("Failed to initialize DXGI Capture.");
            throw std::runtime_error("Failed to initialize DXGI Capture.");
        }

        update_target_dimensions();
        _leds = std::vector<std::array<uint8_t, 3>>(n_pixels, {0, 0, 0});
    }

    void animate(LEDStrip &lights) override {
        calc_lights(_leds);
        lights.update(_leds);
    }

    std::map<std::string, Parameter> Effect::get_parameters(void) {
        return {{"DeadPixels", dead_leds},
                {"Npixels", n_pixels},
                {"BorderLength", border_length},
                {"FPS", fps}};
    }
};

static Replicate replicate_effect;