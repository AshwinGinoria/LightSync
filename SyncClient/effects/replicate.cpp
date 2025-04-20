#include "../led_strip.hpp"
#include "../logger.hpp"
#include "effect.hpp"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>
#include <winrt/Windows.System.Profile.h>
#include <Unknwn.h>

#include <chrono>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <queue>
#include <Unknwn.h>  // for IUnknown

using Microsoft::WRL::ComPtr;
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

// Correct manual definition for IDirect3DDxgiInterfaceAccess
#ifndef IDirect3DDxgiInterfaceAccess_DEFINED
#define IDirect3DDxgiInterfaceAccess_DEFINED


struct __declspec(uuid("a9e2faa0-c3d3-11e0-8f47-0002a5d5c51b")) IDirect3DDxgiInterfaceAccess : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** ppvObject) = 0;
};

#endif

HRESULT TryGetDXGIInterfaceFromObject(IInspectable* object, REFIID iid, void** result) {
    using FnType = HRESULT(__stdcall*)(IInspectable*, REFIID, void**);
    static FnType resolved = nullptr;
    static bool attempted = false;

    if (!attempted) {
        HMODULE module = LoadLibraryW(L"windows.graphics.capture.dll");
        if (module) {
            resolved = reinterpret_cast<FnType>(GetProcAddress(module, "GetDXGIInterfaceFromObject"));
        }
        attempted = true;
    }

    if (resolved) {
        return resolved(object, iid, result);
    } else {
        return E_NOINTERFACE;
    }
}


class Replicate : public Effect {
  private:
    int dead_leds;
    int n_pixels;
    int border_length;
    int fps;
    bool debug_mode = true;
    int frame_number = 0;

    std::vector<std::array<uint8_t, 3>> _leds;
    int _height;
    int _width;

    com_ptr<ID3D11Device> d3dDevice;
    com_ptr<ID3D11DeviceContext> d3dContext;
    Direct3D11CaptureFramePool framePool{nullptr};
    GraphicsCaptureSession session{nullptr};
    GraphicsCaptureItem item{nullptr};

    void update_target_dimensions(float aspect_ratio = 16.0f / 9.0f) {
        if (n_pixels > 0) {
            float total = (n_pixels - dead_leds + 4) / 2.0f;
            _height = total / (1.0 + aspect_ratio);
            _width = total / (1.0 / aspect_ratio + 1.0);
        }
        LOGGER.info("Target Parameters Updated - Height: {}, Width: {}", _height, _width);
    }

    void set_parameter(const std::string &key, const Parameter &param) override {
        try {
            if (key == "DeadPixels")
                set_int_parameter(dead_leds, param);
            else if (key == "Npixels")
                set_int_parameter(n_pixels, param);
            else if (key == "BorderLength")
                set_int_parameter(border_length, param);
            else if (key == "FPS") {
                set_int_parameter(fps, param);
                interval_ms = 1000 / fps;
            } else {
                LOGGER.error("Undefined Parameter {} for effect {}", key, name);
                return;
            }
            update_target_dimensions();
            _leds.resize(n_pixels, {0, 0, 0});
        } catch (const std::exception &e) {
            LOGGER.error("Parameter setting failed: {}", e.what());
        }
    }

    std::array<uint8_t, 3> process_pixel(const cv::Vec3b &pixel) {
        return {pixel[0], pixel[1], pixel[2]};
    }


    bool initialize_wgc_capture() {
        try {
            // Log system info
            try {
                auto osInfo = winrt::Windows::System::Profile::AnalyticsInfo::VersionInfo();
                LOGGER.debug("Windows version: {}", winrt::to_string(osInfo.ProductName()));
            } catch (const winrt::hresult_error& e) {
                LOGGER.warn("Failed to get Windows version: {}", winrt::to_string(e.message()));
            }

            LOGGER.info("Initializing WGC capture with DirectX 11...");
            if (!GraphicsCaptureSession::IsSupported()) {
                LOGGER.error("WGC not supported on this system");
                return false;
            }
    
            init_apartment();
    
            D3D_FEATURE_LEVEL level;
            HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                           D3D11_SDK_VERSION, d3dDevice.put(), &level, d3dContext.put());
            if (FAILED(hr)) {
                LOGGER.error("D3D11CreateDevice failed: {}", std::format("0x{:08X}", static_cast<unsigned>(hr)));
                return false;
            }
            LOGGER.info("Created D3D_Device 0x{}, feature level 0x{}",
                        std::format("{:X}", reinterpret_cast<uintptr_t>(d3dDevice.get())),
                        std::format("{:X}", static_cast<unsigned>(level)));
            LOGGER.debug("D3DDevice ref count: {}", d3dDevice->AddRef() - 1);
            d3dDevice->Release();

            ComPtr<IDXGIDevice> dxgiDeviceRaw;
            hr = d3dDevice->QueryInterface(IID_PPV_ARGS(dxgiDeviceRaw.GetAddressOf()));
            if (FAILED(hr)) {
                LOGGER.error("QueryInterface for IDXGIDevice failed: 0x{}", std::format("{:08X}", static_cast<unsigned>(hr)));
                return false;
            }
            LOGGER.info("Got IDXGIDevice 0x{}", std::format("{:X}", reinterpret_cast<uintptr_t>(dxgiDeviceRaw.Get())));
    
            // Check HDR status
            {
                ComPtr<IDXGIAdapter> adapter;
                hr = dxgiDeviceRaw->GetAdapter(adapter.GetAddressOf());
                if (FAILED(hr)) {
                    LOGGER.error("GetAdapter failed: {}", std::format("0x{:08X}", static_cast<unsigned>(hr)));
                    return false;
                }
    
                ComPtr<IDXGIOutput> output;
                hr = adapter->EnumOutputs(0, output.GetAddressOf());
                if (SUCCEEDED(hr)) {
                    ComPtr<IDXGIOutput6> output6;
                    hr = output->QueryInterface(IID_PPV_ARGS(output6.GetAddressOf()));
                    if (SUCCEEDED(hr)) {
                        DXGI_OUTPUT_DESC1 desc1;
                        hr = output6->GetDesc1(&desc1);
                        if (SUCCEEDED(hr)) {
                            bool isHdrSupported = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                                                 desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 ||
                                                 desc1.MaxLuminance > 0;
                            bool isHdrActive = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                            LOGGER.info("HDR status: supported={}, active={}, color_space=0x{}, max_luminance={}",
                                        isHdrSupported ? "yes" : "no",
                                        isHdrActive ? "yes" : "no",
                                        static_cast<int>(desc1.ColorSpace),
                                        desc1.MaxLuminance);
                        } else {
                            LOGGER.warn("GetDesc1 failed: 0x{}", std::format("{:08X}", static_cast<unsigned>(hr)));
                        }
                    } else {
                        LOGGER.warn("Monitor doesn’t support IDXGIOutput6: 0x{}", std::format("{:08X}", static_cast<unsigned>(hr)));
                    }
                } else {
                    LOGGER.warn("EnumOutputs failed: 0x{}", std::format("{:08X}", static_cast<unsigned>(hr)));
                }
            }

            com_ptr<IDXGIDevice> dxgiDevice;
            dxgiDevice.attach(dxgiDeviceRaw.Detach());
    
            com_ptr<IInspectable> inspectable;
            hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put());
            if (FAILED(hr)) {
                LOGGER.error("CreateDirect3D11DeviceFromDXGIDevice failed: 0x{}", std::format("{:08X}", static_cast<unsigned>(hr)));
                return false;
            }
    
            auto winrtDevice = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
            LOGGER.debug("Created WinRT IDirect3DDevice: valid={}", winrtDevice ? "yes" : "no");
    
            HMONITOR hmon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
            if (!hmon) {
                LOGGER.error("Failed to get primary monitor");
                return false;
            }
            LOGGER.debug("Got primary monitor HMONITOR: 0x{}", 
                std::format("{:X}", reinterpret_cast<uintptr_t>(hmon))
            );
    
            com_ptr<IGraphicsCaptureItemInterop> interop =
                get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    
            hr = interop->CreateForMonitor(hmon, guid_of<GraphicsCaptureItem>(),
                                           reinterpret_cast<void**>(put_abi(item)));
            if (FAILED(hr)) {
                LOGGER.error("CreateForMonitor failed: 0x{}", std::format("{:08X}", static_cast<unsigned>(hr)));
                return false;
            }
    
            auto size = item.Size();
            LOGGER.debug("Capture item size: width={}, height={}, name={}", size.Width, size.Height, winrt::to_string(item.DisplayName()));
    
            framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrtDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
            LOGGER.debug("Created frame pool: format=0x{}, buffers=2, width={}, height={}",
                         std::format("{:X}", static_cast<unsigned>(winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized)),
                         size.Width, size.Height);
    
            session = framePool.CreateCaptureSession(item);
            LOGGER.debug("Created capture session");
    
            // Log session properties
            try {
                LOGGER.debug("Session properties: IsBorderRequired={}, IsCursorCaptureEnabled={}",
                             session.IsBorderRequired() ? "yes" : "no",
                             session.IsCursorCaptureEnabled() ? "yes" : "no");
            } catch (const winrt::hresult_error& e) {
                LOGGER.warn("Failed to get session properties: {}", winrt::to_string(e.message()));
            }

            session.StartCapture();
            LOGGER.info("WGC session started!!");
    
            return true;
        } catch (const winrt::hresult_error& e) {
            LOGGER.error("WGC initialization failed: {}", winrt::to_string(e.message()));
            return false;
        } catch (const std::exception& e) {
            LOGGER.error("WGC initialization failed: {}", e.what());
            return false;
        }
    }

    cv::Mat get_wgc_frame(int width, int height) {
        if (!d3dDevice || !d3dContext) {
            LOGGER.error("get_wgc_frame: Invalid device or context - d3dDevice={}, d3dContext={}",
                         std::format("{:X}", reinterpret_cast<uintptr_t>(d3dDevice.get())),
                         std::format("{:X}", reinterpret_cast<uintptr_t>(d3dContext.get())));
            return {};
        }
        LOGGER.debug("get_wgc_frame: d3dDevice={}, d3dContext={}",
                     std::format("{:X}", reinterpret_cast<uintptr_t>(d3dDevice.get())),
                     std::format("{:X}", reinterpret_cast<uintptr_t>(d3dContext.get())));

        try {
            auto frame = framePool.TryGetNextFrame();
            if (!frame) {
                LOGGER.debug("No frame available from framePool");
                return {};
            }
            LOGGER.debug("get_wgc_frame: got frame from framePool");
            
            auto surface = frame.Surface();
            if (!surface) {
                LOGGER.error("Frame surface is null");
                return {};
            }

            LOGGER.debug("get_wgc_frame: got surface");
            try {
                auto surfaceSize = surface.Description(); // May throw
                LOGGER.debug("Surface size from description: width={}, height={}", surfaceSize.Width, surfaceSize.Height);
            } catch (...) {
                LOGGER.warn("Failed to get surface.Description()");
            }

            
            IUnknown* surfaceRaw = reinterpret_cast<IUnknown*>(winrt::get_abi(surface));
            auto try_interface = [&](REFIID iid, const char* name) {
                void* out = nullptr;
                HRESULT hr = surfaceRaw->QueryInterface(iid, &out);
                LOGGER.debug("{}: {}", name, SUCCEEDED(hr) ? "yes" : std::format("no (HRESULT=0x{:08X})", static_cast<unsigned>(hr)));
                if (SUCCEEDED(hr)) static_cast<IUnknown*>(out)->Release(); // clean up
            };

            try_interface(__uuidof(IDirect3DDxgiInterfaceAccess), "IDirect3DDxgiInterfaceAccess");
            try_interface(__uuidof(ID3D11Texture2D), "ID3D11Texture2D");
            try_interface(__uuidof(IDXGISurface), "IDXGISurface");
            try_interface(__uuidof(ID3D11Resource), "ID3D11Resource");

            ComPtr<ID3D11Texture2D> texture;
            bool textureAcquired = false;

            // Try surface.as<ID3D11Texture2D>() first
            try {
                com_ptr<ID3D11Texture2D> winrtTexture = surface.as<ID3D11Texture2D>();
                texture = winrtTexture.get();
                LOGGER.debug("get_wgc_frame: got ID3D11Texture2D via surface.as<ID3D11Texture2D>()");
                textureAcquired = true;
            } catch (const winrt::hresult_error& e) {
                LOGGER.warn("surface.as<ID3D11Texture2D>() failed: {}", winrt::to_string(e.message()));
            }

            // Fallback: IDirect3DDxgiInterfaceAccess
            if (!textureAcquired) {
                winrt::com_ptr<IUnknown> unknown = surface.as<IUnknown>();
                LOGGER.debug("Fallback: trying IDirect3DDxgiInterfaceAccess via IUnknown 0x{:X}",
                             reinterpret_cast<uintptr_t>(unknown.get()));

                ComPtr<IDirect3DDxgiInterfaceAccess> interop;
                HRESULT hr = unknown->QueryInterface(__uuidof(IDirect3DDxgiInterfaceAccess), &interop);
                if (SUCCEEDED(hr)) {
                    hr = interop->GetInterface(IID_PPV_ARGS(&texture));
                    if (SUCCEEDED(hr)) {
                        LOGGER.debug("get_wgc_frame: got ID3D11Texture2D via IDirect3DDxgiInterfaceAccess fallback");
                        textureAcquired = true;
                    } else {
                        LOGGER.error("interop->GetInterface(ID3D11Texture2D) failed: {}", std::format("0x{:08X}", static_cast<unsigned>(hr)));
                    }
                } else {
                    LOGGER.error("QueryInterface for IDirect3DDxgiInterfaceAccess failed: {}", std::format("0x{:08X}", static_cast<unsigned>(hr)));
                }
            }

            winrt::com_ptr<IDXGIDevice> surfaceDxgiDevice;
            HRESULT hr = TryGetDXGIInterfaceFromObject(
                reinterpret_cast<IInspectable*>(winrt::get_abi(surface.as<IDirect3DDevice>())),
                __uuidof(IDXGIDevice),
                surfaceDxgiDevice.put_void()
            );
            
            if (SUCCEEDED(hr)) {
                LOGGER.debug("Got IDXGIDevice from surface's owning device: pointer = 0x{:X}", reinterpret_cast<uintptr_t>(surfaceDxgiDevice.get()));
            } else {
                LOGGER.error("Failed to get IDXGIDevice from surface's device: HRESULT = 0x{:08X}", static_cast<unsigned>(hr));
            }
            
            if (!textureAcquired) {
                LOGGER.error("Failed to acquire ID3D11Texture2D by any method");
                return {};
            }

            LOGGER.info("SUCCESSS!!!");

            // Copy to staging texture (not shown here for brevity)
            // ...

            return {};
        } catch (const winrt::hresult_error& e) {
            LOGGER.error("Frame capture (WinRT) failed: {}", winrt::to_string(e.message()));
            return {};
        } catch (const std::exception& e) {
            LOGGER.error("Frame capture failed: {}", e.what());
            return {};
        } catch (...) {
            LOGGER.error("Frame capture failed: unknown exception");
            return {};
        }
    }

    void calc_lights(std::vector<std::array<uint8_t, 3>> &leds) {
        LOGGER.debug("Calculating lights for {} pixels", n_pixels);
        cv::Mat image = get_wgc_frame(_width, _height);

        if (image.empty()) {
            LOGGER.warn("Using previous frame due to capture failure");
            return;
        }

        LOGGER.debug("Captured frame {} with size {}x{}", frame_number++, image.cols, image.rows);
        if (debug_mode) {
            std::string filename = "frames/frame" + std::to_string(frame_number) + ".png";
            cv::imwrite(filename, image);
        }

        int i = 0;
        while (i < dead_leds) leds[i++] = {0, 0, 0};

        try {
            for (int j = _height - 2; j >= 0 && i < n_pixels; --j)
                leds[i++] = process_pixel(image.at<cv::Vec3b>(j, 0));
            for (int j = 1; j < _width && i < n_pixels; ++j)
                leds[i++] = process_pixel(image.at<cv::Vec3b>(0, j));
            for (int j = 1; j < _height && i < n_pixels; ++j)
                leds[i++] = process_pixel(image.at<cv::Vec3b>(j, _width - 1));
            for (int j = _width - 2; j >= 0 && i < n_pixels; --j)
                leds[i++] = process_pixel(image.at<cv::Vec3b>(_height - 1, j));

            while (i < n_pixels) leds[i++] = {0, 0, 0};
            LOGGER.debug("Processed {} pixels for LED strip", i);
        } catch (const cv::Exception &e) {
            LOGGER.error("Image processing error: {}", e.what());
        }
    }

    void cleanup() {
        try {
            if (session) session.as<winrt::Windows::Foundation::IClosable>().Close();
            if (framePool) framePool.as<winrt::Windows::Foundation::IClosable>().Close();
            session = nullptr;
            framePool = nullptr;
            item = nullptr;
            d3dContext = nullptr;
            d3dDevice = nullptr;
            uninit_apartment();
        } catch (...) {}
    }

  public:
    Replicate(int target_fps = 30, int n_pixels = 288, int dead_leds = 2)
        : Effect("Replicate", 1000 / target_fps), dead_leds(dead_leds), fps(target_fps), n_pixels(n_pixels), border_length(0) {

        auto logSink = std::make_unique<FileSink>("sync_lights.log");
        LOGGER.addSink(std::move(logSink));
        LOGGER.setFormat("{timestamp} [{level}] {message}");

        if (!initialize_wgc_capture()) throw std::runtime_error("Failed to initialize WGC Capture");

        update_target_dimensions();
        _leds.resize(n_pixels, {0, 0, 0});
    }

    ~Replicate() { cleanup(); }

    void animate(LEDStrip &lights) override {
        calc_lights(_leds);
        // lights.update(_leds);
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
