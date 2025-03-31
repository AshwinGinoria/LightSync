#include "../logger.hpp"
#include "../led_strip.hpp"
#include "effect.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <opencv2/opencv.hpp>
#include <iostream>

#include <vector>
#include <array>
#include <numeric>

using Microsoft::WRL::ComPtr;

class Replicate : public Effect
{
private:
    // Parameters
    int offset;
    int dead_leds;
    int BorderLength;

    int _height;
    int _width;

    bool isEmpty(const std::vector<int> &arr)
    {
        return std::accumulate(arr.begin(), arr.end(), 0) == 0;
    }

    cv::Mat _cut_border(const cv::Mat &image)
    {
        cv::Mat result = image.clone();
        int h = image.rows;

        for (int i = BorderLength; i > 0; --i)
        {
            result.row(i - 1) = result.row(i);
            result.row(h - i) = result.row(h - i - 1);
        }

        return result;
    }

    std::vector<std::array<uint8_t, 3>> calc_lights(const cv::Mat &image)
    {
        cv::Mat resized_image;
        cv::resize(image, resized_image, cv::Size(_width, _height), 0, 0, cv::INTER_CUBIC);
        cv::cvtColor(resized_image, resized_image, cv::COLOR_BGR2RGB);

        cv::Mat cut_image = _cut_border(resized_image);
        std::vector<std::array<uint8_t, 3>> leds;

        // Left
        for (int i = _height - 1; i >= 0; --i)
        {
            leds.push_back({cut_image.at<cv::Vec3b>(i, 0)[0], cut_image.at<cv::Vec3b>(i, 0)[1], cut_image.at<cv::Vec3b>(i, 0)[2]});
        }
        // Top
        for (int i = 0; i < _width; ++i)
        {
            leds.push_back({cut_image.at<cv::Vec3b>(0, i)[0], cut_image.at<cv::Vec3b>(0, i)[1], cut_image.at<cv::Vec3b>(0, i)[2]});
        }
        // Right
        for (int i = 0; i < _height; ++i)
        {
            leds.push_back({cut_image.at<cv::Vec3b>(i, _width - 1)[0], cut_image.at<cv::Vec3b>(i, _width - 1)[1], cut_image.at<cv::Vec3b>(i, _width - 1)[2]});
        }
        // Bottom
        for (int i = _width - 1; i >= 0; --i)
        {
            leds.push_back({cut_image.at<cv::Vec3b>(_height - 1, i)[0], cut_image.at<cv::Vec3b>(_height - 1, i)[1], cut_image.at<cv::Vec3b>(_height - 1, i)[2]});
        }

        return leds;
    }

    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIOutputDuplication> duplication;

    bool initialize_dxgi_capture() {
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &d3dDevice,
            &featureLevel,
            &d3dContext);

        if (FAILED(hr)) {
            std::cerr << "Failed to create D3D11 device." << std::endl;
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        d3dDevice.As(&dxgiDevice);

        ComPtr<IDXGIAdapter> adapter;
        dxgiDevice->GetAdapter(&adapter);

        ComPtr<IDXGIOutput> output;
        adapter->EnumOutputs(0, &output);

        ComPtr<IDXGIOutput1> output1;
        output.As(&output1);

        DXGI_OUTPUT_DESC outputDesc;
        output->GetDesc(&outputDesc);

        hr = output1->DuplicateOutput(d3dDevice.Get(), &duplication);
        if (FAILED(hr)) {
            std::cerr << "DuplicateOutput failed. Try running with admin rights." << std::endl;
            return false;
        }

        std::cout << "Capturing from: " << outputDesc.DeviceName << std::endl;
        return true;
    }

    cv::Mat get_frame(int width = 640, int height = 360) {
        ComPtr<IDXGIResource> desktopResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;

        HRESULT hr = duplication->AcquireNextFrame(500, &frameInfo, &desktopResource);
        if (FAILED(hr)) {
            return cv::Mat();
        }

        ComPtr<ID3D11Texture2D> acquiredTexture;
        desktopResource.As(&acquiredTexture);

        D3D11_TEXTURE2D_DESC desc;
        acquiredTexture->GetDesc(&desc);

        D3D11_TEXTURE2D_DESC cpuDesc = desc;
        cpuDesc.Usage = D3D11_USAGE_STAGING;
        cpuDesc.BindFlags = 0;
        cpuDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        cpuDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> cpuTexture;
        d3dDevice->CreateTexture2D(&cpuDesc, nullptr, &cpuTexture);

        d3dContext->CopyResource(cpuTexture.Get(), acquiredTexture.Get());

        D3D11_MAPPED_SUBRESOURCE resource;
        d3dContext->Map(cpuTexture.Get(), 0, D3D11_MAP_READ, 0, &resource);

        cv::Mat frame(desc.Height, desc.Width, CV_8UC4, resource.pData, resource.RowPitch);
        cv::Mat resized;
        cv::resize(frame, resized, cv::Size(width, height));

        d3dContext->Unmap(cpuTexture.Get(), 0);
        duplication->ReleaseFrame();

        return resized;
    }

public:
    // 33 ms interval => 30 fps
    Replicate(LEDStrip *lights, int target_fps = 33, int offset = 0) : Effect(lights, 1000 / target_fps), offset(offset), dead_leds(0)
    {
        initialize_dxgi_capture()

        cv::Mat ss = get_frame();
        int W = ss.cols;
        int H = ss.rows;
        float scale_factor = (lights->nPixels + 4 - dead_leds) / (2.0f * (H + W));

        _height = static_cast<int>(H * scale_factor);
        _width = static_cast<int>(W * scale_factor);
    }

    void animate() override
    {
        cv::Mat frame = get_frame();
        std::vector<std::array<uint8_t, 3>> leds = calc_lights(frame);
        std::vector<std::array<uint8_t, 3>> shifted_leds(leds.begin() + offset, leds.end());
        shifted_leds.insert(shifted_leds.end(), leds.begin(), leds.begin() + offset);
        lights->update(shifted_leds);
    }
};

static Replicate replicate_effect;