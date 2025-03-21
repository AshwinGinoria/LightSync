#include "../logger.cpp"
#include "../led_strip.cpp"
#include "effect.cpp"
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
// #include <Eigen/Dense>
#include <vector>
#include <array>
#include <numeric>
// #include <windows.h>
#include <opencv2/opencv.hpp>

class Replicate : public Effect
{
private:
    int offset;
    int _height;
    int _width;
    int dead_leds;

    cv::Mat get_virtual_monitor_ss(int monitor_index = 1) // 0 = primary, 1 = secondary
    {
        HDC hDesktopDC = GetDC(NULL);
        HDC hMemoryDC = CreateCompatibleDC(hDesktopDC);

        // Get Monitor Dimensions
        MONITORINFO monitorInfo = {sizeof(MONITORINFO)};
        EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMonitor, HDC, LPRECT lprcMonitor, LPARAM dwData) -> BOOL {
            static int index = 0;
            if (index++ == *(int *)dwData)
            {
                MONITORINFO *info = (MONITORINFO *)dwData;
                GetMonitorInfo(hMonitor, info);
            }
            return TRUE;
        }, (LPARAM)&monitorInfo);

        int width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
        int height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;

        // Create Compatible Bitmap
        HBITMAP hBitmap = CreateCompatibleBitmap(hDesktopDC, width, height);
        SelectObject(hMemoryDC, hBitmap);
        
        // Copy Screen to Memory
        BitBlt(hMemoryDC, 0, 0, width, height, hDesktopDC, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top, SRCCOPY);

        // Convert HBITMAP to OpenCV Mat
        BITMAP bmp;
        GetObject(hBitmap, sizeof(BITMAP), &bmp);
        cv::Mat mat(bmp.bmHeight, bmp.bmWidth, CV_8UC4);
        GetBitmapBits(hBitmap, bmp.bmHeight * bmp.bmWidth * 4, mat.data);

        // Cleanup
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hDesktopDC);

        return mat;
    }


    bool isEmpty(const std::vector<int> &arr)
    {
        return std::accumulate(arr.begin(), arr.end(), 0) == 0;
    }

    cv::Mat _cut_border(const cv::Mat &image)
    {
        cv::Mat result = image.clone();
        int h = image.rows;
        int B = 0;

        for (int i = B; i > 0; --i)
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

public:
    // 16 ms interval => 60 fps
    Replicate(LEDStrip *lights, int target_fps = 60, int offset = 0) : Effect(lights, 1000 / target_fps), offset(offset), dead_leds(0)
    {
        cv::Mat ss = get_ss();
        int W = ss.cols;
        int H = ss.rows;
        float scale_factor = (lights->n_pixels + 4 - dead_leds) / (2.0f * (H + W));

        _height = static_cast<int>(H * scale_factor);
        _width = static_cast<int>(W * scale_factor);
    }

    void animate() override
    {
        cv::Mat ss = get_ss();
        std::vector<std::array<uint8_t, 3>> leds = calc_lights(ss);
        std::vector<std::array<uint8_t, 3>> shifted_leds(leds.begin() + offset, leds.end());
        shifted_leds.insert(shifted_leds.end(), leds.begin(), leds.begin() + offset);
        lights->update(shifted_leds);
    }
};
