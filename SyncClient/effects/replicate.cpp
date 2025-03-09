#include "../logger.cpp"
#include "../led_strip.cpp"
#include "effect.cpp"
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
// #include <Eigen/Dense>
#include <vector>
#include <numeric>

class Replicate : public Effect
{
private:
    int offset;
    int _height;
    int _width;
    int dead_leds;

    cv::Mat get_ss()
    {
        cv::Mat screenshot;
        cv::VideoCapture cap(cv::CAP_DSHOW);
        cap >> screenshot;
        return screenshot;
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

    std::vector<std::vector<int>> calc_lights(const cv::Mat &image)
    {
        cv::Mat resized_image;
        cv::resize(image, resized_image, cv::Size(_width, _height), 0, 0, cv::INTER_CUBIC);
        cv::cvtColor(resized_image, resized_image, cv::COLOR_BGR2RGB);

        cv::Mat cut_image = _cut_border(resized_image);
        std::vector<std::vector<int>> leds;

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
    Replicate(LEDStrip *lights, int offset) : Effect(lights, 0.02), offset(offset), dead_leds(0)
    {
        cv::Mat ss = get_ss();
        int W = ss.cols;
        int H = ss.rows;
        float scale_factor = (lights->get_num_pixels() + 4 - dead_leds) / (2.0f * (H + W));

        _height = static_cast<int>(H * scale_factor);
        _width = static_cast<int>(W * scale_factor);
    }

    void animate()
    {
        cv::Mat ss = get_ss();
        std::vector<std::vector<int>> leds = calc_lights(ss);
        std::vector<std::vector<int>> shifted_leds(leds.begin() + offset, leds.end());
        shifted_leds.insert(shifted_leds.end(), leds.begin(), leds.begin() + offset);
        lights->update(shifted_leds);
    }
};
