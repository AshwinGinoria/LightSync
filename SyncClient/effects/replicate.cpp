#include "../led_strip.hpp"
#include "../logger.hpp"
#include "effect.hpp"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.graphics.capture.interop.h>
#include <windows.h>

#include <opencv2/opencv.hpp>
#include <chrono>
#include <mutex>
#include <queue>

namespace {
    HRESULT GetDXGISurfaceFromDirect3DSurface(
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface,
        winrt::com_ptr<IDXGISurface>& dxgiSurface) {
        // Get the underlying IUnknown interface
        winrt::com_ptr<IUnknown> unknown = surface.as<IUnknown>();
        return unknown->QueryInterface(IID_PPV_ARGS(dxgiSurface.put()));
    }
}

using Microsoft::WRL::ComPtr;

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

    // Windows Graphics Capture
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};

    void update_target_dimensions(float aspect_ratio = 16.0f / 9.0f) {
        if (n_pixels > 0) {
            float total = (n_pixels - dead_leds + 4) / 2.0f;
            _height = total / (1.0 + aspect_ratio);
            _width = total / (1.0 / aspect_ratio + 1.0);
        }

        LOGGER.info("Target Parameters Updated - Height: {}, Width: {}",
                    _height, _width);
    }

    void set_parameter(const std::string& key, const Parameter& param) override {
        try {
            if (key == "DeadPixels") set_int_parameter(dead_leds, param);
            else if (key == "Npixels") set_int_parameter(n_pixels, param);
            else if (key == "BorderLength") set_int_parameter(border_length, param);
            else if (key == "FPS") {
                set_int_parameter(fps, param);
                interval_ms = 1000 / fps;
            }
            else {
                LOGGER.error("Undefined Parameter {} for effect {}", key, name);
                return;
            }
            update_target_dimensions();
            _leds.resize(n_pixels, {0, 0, 0});
        }
        catch (const std::exception& e) {
            LOGGER.error("Parameter setting failed: {}", e.what());
        }
    }

    void calc_lights(std::vector<std::array<uint8_t, 3>>& leds) {
        LOGGER.debug("Calculating lights for {} pixels", n_pixels);
        cv::Mat image = get_wgc_frame(_width, _height);

        if (image.empty()) {
            LOGGER.warn("Using previous frame due to capture failure");
            return;
        }

        LOGGER.debug("Captured frame {}", frame_number);

        // Save frame (optional, for debugging)
        std::string filename = "frames/frame" + std::to_string(frame_number) + ".png";
        frame_number++;
        cv::imwrite(filename, image);

        int i = 0;
        // Dead LEDs
        while (i < dead_leds) leds[i++] = {0, 0, 0};

        // Sample borders with error checking
        try {
            // Left
            for (int j = _height - 2; j >= 0 && i < n_pixels; --j)
                leds[i++] = process_pixel(image.at<cv::Vec3b>(j, 0));

            // Top
            for (int j = 1; j < _width && i < n_pixels; ++j)
                leds[i++] = process_pixel(image.at<cv::Vec3b>(0, j));

            // Right
            for (int j = 1; j < _height && i < n_pixels; ++j)
                leds[i++] = process_pixel(image.at<cv::Vec3b>(j, _width - 1));

            // Bottom
            for (int j = _width - 2; j >= 0 && i < n_pixels; --j)
                leds[i++] = process_pixel(image.at<cv::Vec3b>(_height - 1, j));

            // Dead LEDs at end
            while (i < n_pixels) leds[i++] = {0, 0, 0};
        }
        catch (const cv::Exception& e) {
            LOGGER.error("Image processing error: {}", e.what());
        }
    }

    std::array<uint8_t, 3> process_pixel(const cv::Vec3b& pixel) {
        return {
            static_cast<uint8_t>(pixel[0]),
            static_cast<uint8_t>(pixel[1]),
            static_cast<uint8_t>(pixel[2])
        };
    }

    bool initialize_wgc_capture() {
        try {
            LOGGER.info("Initializing WGC capture...");

            if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
                throw std::runtime_error("WGC is not supported on this system.");
            }

            winrt::init_apartment();
    
            // STEP 1: Create D3D11 device (hardware + BGRA support)
            D3D_FEATURE_LEVEL featureLevel;
            HRESULT hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0,
                D3D11_SDK_VERSION,
                d3dDevice.put(),
                &featureLevel,
                d3dContext.put()
            );
            if (FAILED(hr)) throw std::runtime_error("D3D11CreateDevice failed");
    
            // STEP 2: Log adapter name
            Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDeviceRaw;
            hr = d3dDevice->QueryInterface(IID_PPV_ARGS(dxgiDeviceRaw.GetAddressOf()));
            if (FAILED(hr)) throw std::runtime_error("QueryInterface for IDXGIDevice failed");

            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            dxgiDeviceRaw->GetAdapter(&adapter);
            DXGI_ADAPTER_DESC desc;
            adapter->GetDesc(&desc);
            std::wstring adapterName(desc.Description);

            // STEP 3: Wrap device for WinRT
            winrt::com_ptr<IDXGIDevice> dxgiDevice;
            dxgiDevice.attach(dxgiDeviceRaw.Detach());

            winrt::com_ptr<IInspectable> inspectable;
            hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put());
            if (FAILED(hr)) throw std::runtime_error("CreateDirect3D11DeviceFromDXGIDevice failed");

            auto winrtDevice = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

            // DEBUG: Sanity check all pointers
            LOGGER.info("Pointers:");
            LOGGER.info(" - d3dDevice: {}", static_cast<void*>(d3dDevice.get()));
            LOGGER.info(" - dxgiDevice: {}", static_cast<void*>(dxgiDevice.get()));
            LOGGER.info(" - winrtDevice: {}", static_cast<void*>(winrt::get_abi(winrtDevice)));

            // STEP 4: Get primary monitor
            HMONITOR hmon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
            if (!hmon) throw std::runtime_error("Failed to get primary monitor");

            // STEP 5: Create GraphicsCaptureItem for monitor
            winrt::com_ptr<IGraphicsCaptureItemInterop> interop =
                winrt::get_activation_factory<
                    winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                    IGraphicsCaptureItemInterop>();

            hr = interop->CreateForMonitor(
                hmon,
                winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
                reinterpret_cast<void**>(winrt::put_abi(item)));
            if (FAILED(hr)) throw std::runtime_error("CreateForMonitor failed");

            auto size = item.Size();

            // STEP 6: Create frame pool using the SAME winrtDevice
            framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrtDevice,
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2,
                size
            );

            session = framePool.CreateCaptureSession(item);
            session.StartCapture();

            LOGGER.info("WGC session started: width = {}, height = {}", size.Width, size.Height);
            return true;
        } catch (const std::exception& e) {
            LOGGER.error("WGC initialization failed: {}", e.what());
            cleanup();
            return false;
        }
    }    


    cv::Mat get_wgc_frame(int width, int height) {
        try {
            auto frame = framePool.TryGetNextFrame();
            if (!frame) {
                LOGGER.warn("No frame available from WGC capture");
                return cv::Mat();
            }

            auto surface = frame.Surface();

            winrt::com_ptr<IDXGISurface> dxgiSurface;
            HRESULT hr = GetDXGISurfaceFromDirect3DSurface(surface, dxgiSurface);
            if (FAILED(hr)) {
                LOGGER.error("QueryInterface for IDXGISurface returned: 0x{:08X}", static_cast<unsigned>(hr));
                throw std::runtime_error("GetDXGISurfaceFromDirect3DSurface failed");
            }

            winrt::com_ptr<ID3D11Texture2D> texture;
            hr = dxgiSurface->QueryInterface(IID_PPV_ARGS(texture.put()));
            if (FAILED(hr)) throw std::runtime_error("QueryInterface for texture failed");

            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);

            // Create staging texture to copy from GPU to CPU
            winrt::com_ptr<ID3D11Texture2D> cpuTexture;
            D3D11_TEXTURE2D_DESC cpuDesc = desc;
            cpuDesc.Usage = D3D11_USAGE_STAGING;
            cpuDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            cpuDesc.BindFlags = 0;
            cpuDesc.MiscFlags = 0;

            hr = d3dDevice->CreateTexture2D(&cpuDesc, nullptr, cpuTexture.put());
            if (FAILED(hr)) throw std::runtime_error("CreateTexture2D for CPU texture failed");

            d3dContext->CopyResource(cpuTexture.get(), texture.get());

            D3D11_MAPPED_SUBRESOURCE mapped;
            hr = d3dContext->Map(cpuTexture.get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) throw std::runtime_error("Map failed");

            cv::Mat raw(desc.Height, desc.Width, CV_8UC4);
            for (UINT y = 0; y < desc.Height; ++y) {
                memcpy(raw.ptr(y), static_cast<uint8_t*>(mapped.pData) + y * mapped.RowPitch, desc.Width * 4);
            }
            d3dContext->Unmap(cpuTexture.get(), 0);

            // Resize and convert format
            cv::Mat resized, final;
            cv::resize(raw, resized, cv::Size(width, height), 0, 0, cv::INTER_AREA);
            cv::cvtColor(resized, final, cv::COLOR_BGRA2RGB);

            return final;
        } catch (const std::exception& e) {
            LOGGER.error("Frame capture failed: {}", e.what());
            return cv::Mat();
        }
    }


    void cleanup() {
        try {
            // Close session first if it exists
            if (session != nullptr) {
                auto closable = session.as<winrt::Windows::Foundation::IClosable>();
                if (closable != nullptr) {
                    closable.Close();
                }
                session = nullptr;
            }

            // Then close frame pool
            if (framePool != nullptr) {
                auto closable = framePool.as<winrt::Windows::Foundation::IClosable>();
                if (closable != nullptr) {
                    closable.Close();
                }
                framePool = nullptr;
            }

            // Clear other resources
            d3dContext = nullptr;
            d3dDevice = nullptr;
            item = nullptr;
        }
        catch (const winrt::hresult_error& e) {
            std::string errorMsg = winrt::to_string(e.message());
            LOGGER.error("Error during cleanup: {}", errorMsg);
        }
        catch (const std::exception& e) {
            LOGGER.error("Error during cleanup: {}", e.what());
        }
    }

public:
    Replicate(int target_fps = 30, int n_pixels = 288, int dead_leds = 2)
        : Effect("Replicate", 1000 / target_fps), dead_leds(dead_leds),
          fps(target_fps), n_pixels(n_pixels), border_length(0) {


        if (!initialize_wgc_capture()) {
            throw std::runtime_error("Failed to initialize WGC Capture");
        }

        update_target_dimensions();

        // Initialize LEDs with default color (black)
        _leds = std::vector<std::array<uint8_t, 3>>(n_pixels, {0, 0, 0});
    }

    ~Replicate() {
        cleanup();
    }

    void animate(LEDStrip& lights) override {
        calc_lights(_leds);
        lights.update(_leds);
    }

    std::map<std::string, Parameter> get_parameters(void) override {
        return {
            {"DeadPixels", dead_leds},
            {"Npixels", n_pixels},
            {"BorderLength", border_length},
            {"FPS", fps}
        };
    }
};

static Replicate replicate_effect;