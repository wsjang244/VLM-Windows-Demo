#pragma once
// =============================================================================
//  backend.h - HailoRT GenAI VLM Backend (HailoRT 5.2.0)
// =============================================================================

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <optional>
#include <iostream>
#include <sstream>
#include <future>
#include <memory>

#include <opencv2/opencv.hpp>

#ifdef _WIN32
  #ifdef interface
    #undef interface
  #endif
  #ifdef GetObject
    #undef GetObject
  #endif
  #ifdef CreateEvent
    #undef CreateEvent
  #endif
#endif

#include "hailo/hailort.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/device.hpp"
#include "hailo/genai/vlm/vlm.hpp"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// =============================================================================
struct InferenceResult {
    std::string answer;
    std::string time_str;
};

struct MonitoringResult {
    cv::Mat frame;
    InferenceResult result;
};

// =============================================================================
class Backend {
public:
    Backend(const json& prompts,
            const std::string& hef_path = "Qwen2-VL-2B-Instruct.hef",
            uint32_t max_tokens = 40,
            float temperature = 0.1f,
            uint32_t seed = 42,
            int cooldown_ms = 1000,
            int max_retries = 5);
    ~Backend();

    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    void update_frame(const cv::Mat& frame);
    bool poll_result(MonitoringResult& out);
    void pause_monitoring();
    void resume_monitoring();

    InferenceResult vlm_custom_inference(const cv::Mat& image,
                                         const std::string& custom_prompt);

    void abort_current();
    void close();
    bool is_ready() const { return m_device_ready.load(); }
    static bool diagnose_device();

private:
    void worker_func();
    cv::Mat preprocess_image(const cv::Mat& image, int h, int w);
    std::vector<std::string> build_messages(
        const std::string& trigger,
        const std::string& system_prompt,
        const std::string& user_prompt);

    json m_prompts;
    std::string m_hef_path;
    uint32_t m_max_tokens;
    float m_temperature;
    uint32_t m_seed;
    std::string m_trigger;
    int m_cooldown_ms;
    int m_max_retries;

    std::thread m_worker;
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_device_ready{false};
    std::atomic<bool> m_abort_requested{false};
    std::atomic<bool> m_worker_done{false};  // close() のタイムアウト用

    std::mutex m_mtx;
    std::condition_variable m_cv;

    cv::Mat m_pending_frame;
    bool m_has_pending = false;
    std::atomic<bool> m_paused{false};

    MonitoringResult m_result_buf;
    bool m_has_result = false;

    struct VLMReq {
        cv::Mat image;
        std::string prompt;
        std::shared_ptr<std::promise<InferenceResult>> promise_ptr;
        std::shared_ptr<std::atomic<bool>> cancelled;
    };
    std::optional<VLMReq> m_vlm_req;

    int m_frame_h = 336;
    int m_frame_w = 336;
};