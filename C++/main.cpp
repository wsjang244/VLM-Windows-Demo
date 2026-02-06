// =============================================================================
//  main.cpp - VLM カメラ/動画アプリケーション (Windows/Linux)
//
//  入力: USBカメラ / 動画ファイル / フォルダー内の全動画
//  終了: 'q' キーまたは Ctrl+C
// =============================================================================

#include "backend.h"

#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <future>
#include <csignal>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#include <sys/select.h>
#endif

static std::atomic<bool> g_running{true};
static void signal_handler(int) { g_running = false; }

#ifdef _WIN32
static bool check_enter() {
    if (_kbhit()) { int ch = _getch(); if (ch == '\r' || ch == '\n') return true; }
    return false;
}
#else
static bool check_enter() {
    fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {0, 0};
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
        std::string d; std::getline(std::cin, d); return true;
    }
    return false;
}
#endif

static std::string read_line() { std::string l; std::getline(std::cin, l); return l; }

static std::string now_str() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm b;
#ifdef _WIN32
    localtime_s(&b, &t);
#else
    localtime_r(&t, &b);
#endif
    std::ostringstream o; o << std::put_time(&b, "%H:%M:%S"); return o.str();
}

static int find_camera(int pref) {
    auto try_cam = [](int id) {
#ifdef _WIN32
        cv::VideoCapture c(id, cv::CAP_DSHOW);
#else
        cv::VideoCapture c(id);
#endif
        bool ok = c.isOpened(); if (ok) c.release(); return ok;
    };
    if (try_cam(pref)) return pref;
    for (int i = 0; i < 10; i++) if (i != pref && try_cam(i)) return i;
    return -1;
}

// =============================================================================
//  動画ファイル一覧を取得 (ファイルまたはフォルダー)
// =============================================================================
static std::vector<std::string> resolve_video_sources(const std::string& path) {
    std::vector<std::string> files;
    if (path.empty()) return files;

    fs::path p(path);
    if (fs::is_directory(p)) {
        for (const auto& entry : fs::directory_iterator(p)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" ||
                ext == ".mov" || ext == ".wmv" || ext == ".webm")
            {
                files.push_back(entry.path().string());
            }
        }
        std::sort(files.begin(), files.end());
    } else if (fs::is_regular_file(p)) {
        files.push_back(path);
    }
    return files;
}

// =============================================================================
//  表示用リサイズ (scale < 1.0 のときのみ縮小)
// =============================================================================
static cv::Mat scale_for_display(const cv::Mat& frame, double scale) {
    if (scale >= 1.0 || scale <= 0.0) return frame;
    cv::Mat scaled;
    cv::resize(frame, scaled,
               cv::Size((int)(frame.cols * scale), (int)(frame.rows * scale)),
               0, 0, cv::INTER_NEAREST);
    return scaled;
}

// =============================================================================
class App {
public:
    App(const json& prompts, int cam, const std::string& video_path,
        const std::string& hef, int cooldown_ms, double display_scale)
        : m_backend(prompts, hef,
                    /*max_tokens=*/15, /*temp=*/0.1f,
                    /*seed=*/42, cooldown_ms)
        , m_cam_id(cam)
        , m_video_path(video_path)
        , m_scale(display_scale)
    {}

    void run() {
        std::signal(SIGINT, signal_handler);

        std::cout << "Waiting for Hailo device..." << std::endl;
        for (int i = 0; i < 80 && !m_backend.is_ready() && g_running; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (i > 0 && i % 10 == 0)
                std::cout << "  (" << (i / 2) << "s)" << std::endl;
        }
        if (!m_backend.is_ready())
            std::cerr << "WARNING: Device not ready." << std::endl;

        // ---- 入力ソース ----
        auto video_files = resolve_video_sources(m_video_path);
        bool use_video = !video_files.empty();

        cv::VideoCapture cap;
        size_t video_idx = 0;

        if (use_video) {
            std::cout << "Playlist (" << video_files.size() << " files):" << std::endl;
            for (size_t i = 0; i < video_files.size(); i++)
                std::cout << "  [" << i << "] " << video_files[i] << std::endl;
            cap.open(video_files[0]);
            if (!cap.isOpened()) {
                std::cerr << "Cannot open: " << video_files[0] << std::endl;
                return;
            }
            std::cout << format_video_info(cap, video_files[0]) << std::endl;
        } else {
            int cam = find_camera(m_cam_id);
            if (cam < 0) { std::cerr << "No camera." << std::endl; return; }
#ifdef _WIN32
            cap.open(cam, cv::CAP_DSHOW);
#else
            cap.open(cam);
#endif
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
            cap.set(cv::CAP_PROP_FPS, 30);
            if (!cap.isOpened()) { std::cerr << "Cannot open camera." << std::endl; return; }
        }

        int wait_ms = calc_wait_ms(cap, use_video);

        // WINDOW_AUTOSIZE: ウィンドウサイズ = 画像サイズ (比率は絶対に崩れない)
        // サイズは --scale で制御 (例: --scale 0.5 で半分)
        cv::namedWindow("Video", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("Frame", cv::WINDOW_AUTOSIZE);

        banner(use_video ? "VIDEO STARTED  |  ENTER=ask  q=quit"
                         : "CAMERA STARTED  |  ENTER=ask  q=quit");

        enum class Mode { MONITORING, WAIT_Q, PROC_VLM, WAIT_CONT };
        Mode mode = Mode::MONITORING;
        cv::Mat frozen;
        std::future<InferenceResult> vlm_fut;
        std::string pending_video_msg;

        while (cap.isOpened() && g_running) {
            cv::Mat frame;
            if (!cap.read(frame) || frame.empty()) {
                if (use_video) {
                    // 次の動画 (末尾なら先頭に戻る)
                    video_idx = (video_idx + 1) % video_files.size();
                    cap.open(video_files[video_idx]);
                    if (cap.isOpened()) {
                        pending_video_msg = format_video_info(cap, video_files[video_idx]);
                        wait_ms = calc_wait_ms(cap, true);
                    }
                    continue;
                }
                break;
            }

            cv::imshow("Video", scale_for_display(frame, m_scale));

            int key = cv::waitKey(wait_ms) & 0xFF;
            if (key == 'q' || key == 'Q') {
                std::cout << "\n'q' pressed - shutting down..." << std::endl;
                g_running = false;
                break;
            }

            switch (mode) {
            case Mode::MONITORING: {
                // バッファしていた動画切り替えメッセージを出力
                if (!pending_video_msg.empty()) {
                    std::cout << pending_video_msg << std::endl;
                    pending_video_msg.clear();
                }
                // 推論には原寸フレームを渡す
                if (m_backend.is_ready()) m_backend.update_frame(frame);

                MonitoringResult mr;
                if (m_backend.poll_result(mr)) {
                    cv::imshow("Frame", scale_for_display(mr.frame, m_scale));
                    std::string tag = "[OK]";
                    if (mr.result.answer.find("rror") != std::string::npos ||
                        mr.result.answer.find("bort") != std::string::npos)
                        tag = "[WARN]";
                    else if (mr.result.answer.find("No Event Detected") == std::string::npos)
                        tag = "[INFO]";
                    std::cout << "[" << now_str() << "] " << tag << " "
                              << mr.result.answer << " | " << mr.result.time_str
                              << std::endl;
                }
                if (check_enter()) {
                    m_backend.pause_monitoring();
                    m_backend.abort_current();
                    frozen = frame.clone();
                    cv::imshow("Frame", scale_for_display(frozen, m_scale));
                    mode = Mode::WAIT_Q;
                    std::cout << "\n\nQuestion (Enter='Describe the image'): " << std::flush;
                }
                break;
            }
            case Mode::WAIT_Q: {
                std::string q = read_line();
                if (q.empty()) { q = "Describe the image"; std::cout << "=> " << q << std::endl; }
                if (!m_backend.is_ready()) {
                    std::cout << "[ERROR] Device not ready.\nPress Enter..." << std::endl;
                    mode = Mode::WAIT_CONT;
                } else {
                    std::cout << "Processing..." << std::endl;
                    auto fc = frozen.clone();
                    vlm_fut = std::async(std::launch::async,
                        [this, fc, q]() { return m_backend.vlm_custom_inference(fc, q); });
                    mode = Mode::PROC_VLM;
                }
                break;
            }
            case Mode::PROC_VLM: {
                if (vlm_fut.valid() &&
                    vlm_fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    try { vlm_fut.get(); } catch (...) {}
                    mode = Mode::WAIT_CONT;
                    std::cout << "\n\nPress Enter to continue..." << std::endl;
                }
                break;
            }
            case Mode::WAIT_CONT: {
                if (check_enter()) {
                    m_backend.resume_monitoring();
                    mode = Mode::MONITORING;
                    banner("RESUMED  |  ENTER=ask  q=quit");
                }
                break;
            }
            }
        }

        std::cout << "Shutting down..." << std::endl;
        m_backend.abort_current();
        m_backend.close();
        cap.release();
        cv::destroyAllWindows();
    }

private:
    void banner(const std::string& s) {
        std::cout << "\n" << std::string(80, '=')
                  << "\n  " << s
                  << "\n" << std::string(80, '=') << "\n" << std::endl;
    }

    std::string format_video_info(const cv::VideoCapture& c, const std::string& name) {
        std::ostringstream o;
        o << "Playing: " << fs::path(name).filename().string()
          << " (" << (int)c.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
          << (int)c.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ "
          << (int)c.get(cv::CAP_PROP_FPS) << "fps)";
        return o.str();
    }

    static int calc_wait_ms(const cv::VideoCapture& c, bool is_video) {
        if (!is_video) return 25;
        double fps = c.get(cv::CAP_PROP_FPS);
        return (fps > 0) ? std::max(1, (int)(1000.0 / fps)) : 25;
    }

    Backend m_backend;
    int m_cam_id;
    std::string m_video_path;
    double m_scale;
};

// =============================================================================
struct Args {
    std::string prompts, hef = "Qwen2-VL-2B-Instruct.hef", video;
    int camera = 0, cooldown = 1000;
    double scale = 1.0;
    bool diagnose = false;
};

static Args parse(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if ((s == "--prompts" || s == "-p") && i+1 < argc) a.prompts = argv[++i];
        else if ((s == "--camera" || s == "-c") && i+1 < argc) a.camera = std::stoi(argv[++i]);
        else if ((s == "--video" || s == "-v") && i+1 < argc) a.video = argv[++i];
        else if ((s == "--hef" || s == "-m") && i+1 < argc) a.hef = argv[++i];
        else if (s == "--cooldown" && i+1 < argc) a.cooldown = std::stoi(argv[++i]);
        else if (s == "--scale" && i+1 < argc) a.scale = std::stod(argv[++i]);
        else if (s == "--diagnose" || s == "-d") a.diagnose = true;
        else if (s == "--help" || s == "-h") {
            std::cout << "Usage: " << argv[0] << "\n"
                "  --prompts, -p <path>   Prompts JSON\n"
                "  --camera,  -c <id>     Camera (0)\n"
                "  --video,   -v <path>   Video file or folder of videos\n"
                "  --hef,     -m <path>   HEF model\n"
                "  --scale <factor>       Display scale (0.5=half, default: 1.0)\n"
                "  --cooldown <ms>        Pause between inferences (1000)\n"
                "  --diagnose, -d         Device diagnostics\n";
            std::exit(0);
        }
    }
    if (!a.diagnose && a.prompts.empty()) {
        std::cerr << "Error: --prompts required." << std::endl; std::exit(1);
    }
    return a;
}

int main(int argc, char* argv[]) {
    auto args = parse(argc, argv);
    if (args.diagnose) return Backend::diagnose_device() ? 0 : 1;

    json prompts;
    {
        std::ifstream f(args.prompts);
        if (!f.is_open()) { std::cerr << "Cannot open " << args.prompts << std::endl; return 1; }
        try { f >> prompts; }
        catch (const json::parse_error& e) { std::cerr << "Bad JSON: " << e.what() << std::endl; return 1; }
    }

    std::string input_str = args.video.empty()
        ? "Camera " + std::to_string(args.camera) : args.video;

    std::cout << "VLM App (C++ / HailoRT 5.2.0)\n"
              << "  HEF:      " << args.hef << "\n"
              << "  Input:    " << input_str << "\n"
              << "  Scale:    " << args.scale << "\n"
              << "  Cooldown: " << args.cooldown << " ms" << std::endl;

    try {
        App(prompts, args.camera, args.video, args.hef,
            args.cooldown, args.scale).run();
    }
    catch (const std::exception& e) { std::cerr << "Fatal: " << e.what() << std::endl; return 1; }

    std::cout << "Exited." << std::endl;
    return 0;
}