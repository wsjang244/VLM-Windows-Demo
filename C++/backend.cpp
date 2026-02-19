// =============================================================================
//  backend.cpp - HailoRT GenAI VLM Backend (HailoRT 5.2.0)
//
//  修正点:
//    1. 連続稼働クラッシュ:
//       - generate/read 失敗時に Generator を再作成してリカバリー
//    2. 終了時フリーズ:
//       - close() にタイムアウト付き join (5秒で detach)
//       - abort 後に Worker が応答しなくてもアプリは終了
//    3. 初回 VDevice 失敗:
//       - 初回接続前に 3秒の待機
//       - リトライ間隔を 5秒に延長
//    4. 高速化:
//       - read タイムアウト 2秒 (異常検知高速化)
//       - 監視メッセージキャッシュ (毎回の文字列構築回避)
//       - INTER_NEAREST リサイズ (モデル入力 336x336 品質差なし)
//    5. キーワードベースの回答分類:
//       - JSON の "keywords" でモデルの自由回答からカテゴリを判定
//       - フォールバック: options 直接マッチ (旧方式互換)
//    6. HailoRT User Guide 準拠:
//       - カスタム推論に direct API (vlm.generate(params, msgs, frames)) を使用
//       - Generator 同時存在禁止: カスタム推論前に monitor_gen を破棄し完了後に再作成
// =============================================================================

#include "backend.h"
#include <iomanip>
#include <algorithm>
#include <cctype>

// =============================================================================
static std::string escape_json(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:   o << c;
        }
    }
    return o.str();
}

// =============================================================================
bool Backend::diagnose_device() {
    std::cout << "[Diag] ===== Hailo Device Diagnostics =====" << std::endl;
    auto sr = hailort::Device::scan();
    if (!sr) {
        std::cerr << "[Diag] Device::scan() FAILED (" << (int)sr.status() << ")" << std::endl;
        return false;
    }
    auto& ids = sr.value();
    if (ids.empty()) { std::cerr << "[Diag] No devices." << std::endl; return false; }
    for (const auto& id : ids) std::cout << "[Diag] Device: " << id << std::endl;
    auto dev = hailort::Device::create(ids[0]);
    if (!dev) {
        std::cerr << "[Diag] Cannot open (" << (int)dev.status() << ")" << std::endl;
        return false;
    }
    std::cout << "[Diag] OK: " << dev.value()->get_dev_id() << std::endl;
    return true;
}

// =============================================================================
Backend::Backend(const json& prompts,
                 const std::string& hef_path,
                 uint32_t max_tokens,
                 float temperature,
                 uint32_t seed,
                 int cooldown_ms,
                 int max_retries)
    : m_prompts(prompts), m_hef_path(hef_path),
      m_max_tokens(max_tokens), m_temperature(temperature),
      m_seed(seed), m_cooldown_ms(cooldown_ms),
      m_max_retries(max_retries)
{
    if (m_prompts.contains("use_cases") && !m_prompts["use_cases"].empty())
        m_trigger = m_prompts["use_cases"].begin().key();
    std::cout << "[Backend] Active use case: \"" << m_trigger << "\"" << std::endl;
    m_worker = std::thread(&Backend::worker_func, this);
}

Backend::~Backend() { close(); }

// =============================================================================
//  close - タイムアウト付き join
//
//  Worker が HailoRT 内部でブロックされている場合、join() が永遠に返らない。
//  5秒待って応答がなければ detach してアプリは終了する。
// =============================================================================
void Backend::close() {
    if (!m_running.exchange(false)) return;
    m_abort_requested = true;
    m_cv.notify_all();

    if (m_worker.joinable()) {
        // タイムアウト付き待機: worker_done フラグを 5秒間ポーリング
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!m_worker_done.load() &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (m_worker_done.load()) {
            m_worker.join();
        } else {
            std::cerr << "[Backend] Worker not responding - detaching thread." << std::endl;
            m_worker.detach();
        }
    }
}

// =============================================================================
void Backend::update_frame(const cv::Mat& frame) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        frame.copyTo(m_pending_frame);
        m_has_pending = true;
    }
    m_cv.notify_one();
}

bool Backend::poll_result(MonitoringResult& out) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (!m_has_result) return false;
    out.frame   = std::move(m_result_buf.frame);
    out.result  = std::move(m_result_buf.result);
    m_has_result = false;
    return true;
}

void Backend::pause_monitoring()  { m_paused = true; }
void Backend::resume_monitoring() { m_paused = false; m_cv.notify_one(); }
void Backend::abort_current()     { m_abort_requested = true; }

// =============================================================================
InferenceResult Backend::vlm_custom_inference(const cv::Mat& image,
                                               const std::string& prompt) {
    if (!m_device_ready) return {"Device not ready", "N/A"};

    auto prom = std::make_shared<std::promise<InferenceResult>>();
    auto canc = std::make_shared<std::atomic<bool>>(false);
    auto fut  = prom->get_future();

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_vlm_req = VLMReq{image.clone(), prompt, prom, canc};
    }
    m_cv.notify_one();

    auto st = fut.wait_for(std::chrono::seconds(60));
    if (st == std::future_status::timeout) {
        canc->store(true);
        m_abort_requested = true;
        return {"VLM timeout", "60 seconds"};
    }
    try { return fut.get(); }
    catch (...) { return {"VLM error", "N/A"}; }
}

// =============================================================================
cv::Mat Backend::preprocess_image(const cv::Mat& img, int h, int w) {
    cv::Mat r;
    if (img.channels() == 3) cv::cvtColor(img, r, cv::COLOR_BGR2RGB);
    else r = img.clone();
    if (r.rows != h || r.cols != w)
        cv::resize(r, r, cv::Size(w, h), 0, 0, cv::INTER_NEAREST);
    if (r.depth() != CV_8U) r.convertTo(r, CV_8U);
    if (!r.isContinuous()) r = r.clone();
    return r;
}

// =============================================================================
std::vector<std::string> Backend::build_messages(
    const std::string& trigger,
    const std::string& sys,
    const std::string& usr)
{
    std::vector<std::string> msgs;
    if (!sys.empty()) {
        std::ostringstream o;
        o << R"({"role":"system","content":[{"type":"text","text":")"
          << escape_json(sys) << R"("}]})";
        msgs.push_back(o.str());
    }
    std::string prompt = usr;
    if (trigger != "custom" &&
        m_prompts.contains("use_cases") &&
        m_prompts["use_cases"].contains(trigger) &&
        m_prompts["use_cases"][trigger].contains("details"))
    {
        auto d = m_prompts["use_cases"][trigger]["details"].get<std::string>();
        auto p = prompt.find("{details}");
        if (p != std::string::npos) prompt.replace(p, 9, d);
    }
    {
        std::ostringstream o;
        o << R"({"role":"user","content":[{"type":"image"},{"type":"text","text":")"
          << escape_json(prompt) << R"("}]})";
        msgs.push_back(o.str());
    }
    return msgs;
}

// =============================================================================
//  トークン読み取り (read タイムアウト 2秒)
// =============================================================================
static std::string read_all_tokens(
    hailort::genai::LLMGeneratorCompletion& completion,
    uint32_t max_tokens,
    bool stream,
    std::atomic<bool>& abort_flag,
    const std::shared_ptr<std::atomic<bool>>& cancelled)
{
    std::string response;
    uint32_t n = 0;

    while (completion.generation_status()
           == hailort::genai::LLMGeneratorCompletion::Status::GENERATING)
    {
        if (abort_flag.load() || (cancelled && cancelled->load())) {
            try { completion.abort(); } catch (...) {}
            break;
        }

    // 2秒タイムアウト (高速 abort 応答)
        auto tok = completion.read(std::chrono::seconds(2));
        if (!tok) {
            // read 失敗 → abort して脱出
            try { completion.abort(); } catch (...) {}
            break;
        }

        std::string t = tok.value();
        response += t;
        n++;

        if (stream && t != "<|im_end|>")
            std::cout << t << std::flush;

        if (n >= max_tokens) {
            try { completion.abort(); } catch (...) {}
            break;
        }
    }

    // 後処理
    const std::string eos = "<|im_end|>";
    size_t pos;
    while ((pos = response.find(eos)) != std::string::npos)
        response.erase(pos, eos.length());
    auto l = response.find_first_not_of(" \t\n\r");
    auto r = response.find_last_not_of(" \t\n\r");
    if (l != std::string::npos) response = response.substr(l, r - l + 1);
    else response.clear();

    return response;
}

// =============================================================================
//  worker_func
// =============================================================================
void Backend::worker_func() {
    // -------------------------------------------------------
    //  Phase 1: デバイススキャン
    // -------------------------------------------------------
    std::cout << "[Backend] Scanning devices..." << std::endl;
    {
        auto sr = hailort::Device::scan();
        if (sr) {
            auto& ids = sr.value();
            if (ids.empty()) {
                std::cerr << "[Backend] No devices found." << std::endl;
                m_worker_done = true;
                return;
            }
            for (const auto& id : ids)
                std::cout << "[Backend] Device: " << id << std::endl;
        }
    }

    // -------------------------------------------------------
    //  Phase 2: VDevice 作成
    //
    //  修正: 初回接続前に 3秒待機 (デバイス/サービスの初期化を待つ)
    //        リトライ間隔を 5秒に延長
    // -------------------------------------------------------
    std::shared_ptr<hailort::VDevice> vdevice;
    for (int i = 1; i <= m_max_retries && m_running; i++) {
        // 毎回接続前に待機 (初回は 3秒、リトライは 5秒)
        int wait_sec = (i == 1) ? 3 : 5;
        std::cout << "[Backend] Waiting " << wait_sec
                  << "s before VDevice attempt " << i << "/" << m_max_retries
                  << "..." << std::endl;
        for (int s = 0; s < wait_sec && m_running; s++)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!m_running) break;

        std::cout << "[Backend] Creating VDevice (" << i << "/" << m_max_retries << ")..." << std::endl;
        auto r = hailort::VDevice::create_shared();
        if (r) {
            vdevice = r.release();
            std::cout << "[Backend] VDevice OK." << std::endl;
            break;
        }
        std::cerr << "[Backend] Failed (" << (int)r.status() << ")" << std::endl;
    }
    if (!vdevice) {
        std::cerr << "[Backend] FATAL: Cannot create VDevice." << std::endl;
        m_worker_done = true;
        return;
    }

    // -------------------------------------------------------
    //  Phase 3: VLM ロード
    // -------------------------------------------------------
    std::cout << "[Backend] Loading VLM: " << m_hef_path << std::endl;
    try {
        hailort::genai::VLMParams vlm_params(m_hef_path, true);
        auto vr = hailort::genai::VLM::create(vdevice, vlm_params);
        if (!vr) {
            std::cerr << "[Backend] VLM::create failed (" << (int)vr.status() << ")" << std::endl;
            m_worker_done = true;
            return;
        }
        auto vlm = vr.release();

        auto shape = vlm.input_frame_shape();
        m_frame_h = (int)shape.height;
        m_frame_w = (int)shape.width;
        uint32_t frame_size = vlm.input_frame_size();

        std::cout << "[Backend] VLM ready. Frame: "
                  << m_frame_h << "x" << m_frame_w
                  << " (" << frame_size << " bytes)" << std::endl;

        // -------------------------------------------------------
        //  Phase 4: ジェネレーター管理のヘルパー
        //
        //  監視用ジェネレーターを作成する関数。
        //  エラー時に再作成してリカバリーする。
        // -------------------------------------------------------
        // unique_ptr で管理 (VLMGenerator はコピー/ムーブ代入すべて delete)
        auto create_monitor_generator = [&]()
            -> std::unique_ptr<hailort::genai::VLMGenerator>
        {
            auto p = vlm.create_generator_params()
                .expect("Failed to create monitor params");
            p.set_temperature(m_temperature);
            p.set_max_generated_tokens(m_max_tokens);
            p.set_seed(m_seed);
            auto gen = vlm.create_generator(p)
                .expect("Failed to create monitor generator");
            return std::make_unique<hailort::genai::VLMGenerator>(std::move(gen));
        };

        auto monitor_gen = create_monitor_generator();

        std::cout << "[Backend] Monitor generator ready." << std::endl;
        std::cout << "[Backend] Cooldown: " << m_cooldown_ms << "ms" << std::endl;

        // 監視用メッセージをキャッシュ (毎回同じプロンプト)
        auto cached_monitor_msgs = build_messages(
            m_trigger,
            m_prompts.value("hailo_system_prompt", ""),
            m_prompts.value("hailo_user_prompt", ""));

        m_device_ready = true;

        // -------------------------------------------------------
        //  Phase 5: メインループ
        // -------------------------------------------------------
        auto last_infer = std::chrono::steady_clock::now()
                          - std::chrono::milliseconds(m_cooldown_ms);
        const auto cooldown = std::chrono::milliseconds(m_cooldown_ms);

        while (m_running) {
            std::optional<VLMReq> vlm_req;
            cv::Mat mon_frame;
            bool have_mon = false;

            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cv.wait_for(lk, std::chrono::milliseconds(200), [&] {
                    if (!m_running) return true;
                    if (m_vlm_req.has_value()) return true;
                    if (m_has_pending && !m_paused.load()) {
                        return (std::chrono::steady_clock::now() - last_infer) >= cooldown;
                    }
                    return false;
                });

                if (!m_running) break;

                if (m_vlm_req.has_value()) {
                    vlm_req = std::move(m_vlm_req);
                    m_vlm_req.reset();
                } else if (m_has_pending && !m_paused.load()) {
                    if ((std::chrono::steady_clock::now() - last_infer) >= cooldown) {
                        cv::swap(mon_frame, m_pending_frame);
                        m_has_pending = false;
                        have_mon = true;
                    }
                }
            }

            // =========================================================
            //  VLM カスタム推論
            // =========================================================
            if (vlm_req.has_value()) {
                auto& req = vlm_req.value();
                m_abort_requested = false;
                if (req.cancelled && req.cancelled->load()) continue;

                // ガイド準拠: Generator は同時に1つのみ存在可能
                // カスタム推論前に監視用 Generator を破棄する
                monitor_gen.reset();

                InferenceResult result;
                auto t0 = std::chrono::steady_clock::now();

                try {
                    auto rgb = preprocess_image(req.image, m_frame_h, m_frame_w);
                    hailort::MemoryView fv(rgb.data, frame_size);

                    auto msgs = build_messages(
                        "custom",
                        "You are a helpful assistant that analyzes images and answers questions about them.",
                        req.prompt);

                    auto cp = vlm.create_generator_params()
                        .expect("Failed to create custom params");
                    cp.set_temperature(0.5f);
                    cp.set_max_generated_tokens(200);
                    cp.set_seed(m_seed);

                    // ガイド準拠: 一回限りの推論には direct API を使用
                    auto completion = vlm.generate(cp, msgs, {fv})
                        .expect("Failed to generate (custom)");

                    result.answer = read_all_tokens(
                        completion, 200, true,
                        m_abort_requested, req.cancelled);

                    vlm.clear_context();
                    if (result.answer.empty())
                        result.answer = m_abort_requested ? "Aborted" : "No response";

                } catch (const std::exception& e) {
                    result.answer = std::string("Error: ") + e.what();
                    try { vlm.clear_context(); } catch (...) {}
                }

                // カスタム推論完了後、監視用 Generator を再作成
                try {
                    monitor_gen = create_monitor_generator();
                } catch (const std::exception& e) {
                    std::cerr << "[Backend] Monitor generator recreate failed: "
                              << e.what() << std::endl;
                }

                auto t1 = std::chrono::steady_clock::now();
                double sec = std::chrono::duration<double>(t1 - t0).count();
                std::ostringstream ts;
                ts << std::fixed << std::setprecision(2) << sec << "s";
                result.time_str = ts.str();

                if (req.promise_ptr && req.cancelled) {
                    bool exp = false;
                    if (req.cancelled->compare_exchange_strong(exp, true)) {
                        try { req.promise_ptr->set_value(std::move(result)); }
                        catch (const std::future_error&) {}
                    }
                }
                last_infer = std::chrono::steady_clock::now();
                continue;
            }

            // =========================================================
            //  監視推論
            // =========================================================
            if (have_mon) {
                m_abort_requested = false;

                // monitor_gen が未作成の場合 (前回の再作成失敗時)
                if (!monitor_gen) {
                    try {
                        monitor_gen = create_monitor_generator();
                    } catch (const std::exception& e) {
                        std::cerr << "[Backend] Cannot create monitor generator: "
                                  << e.what() << std::endl;
                        continue;
                    }
                }

                InferenceResult result;
                auto t0 = std::chrono::steady_clock::now();

                try {
                    auto rgb = preprocess_image(mon_frame, m_frame_h, m_frame_w);
                    hailort::MemoryView fv(rgb.data, frame_size);

                    auto completion = monitor_gen->generate(cached_monitor_msgs, {fv})
                        .expect("Failed to generate (monitor)");

                    std::string response = read_all_tokens(
                        completion, m_max_tokens, false,
                        m_abort_requested, nullptr);

                    vlm.clear_context();

                    // レスポンスから分類結果を抽出
                    // 2Bモデルはプロンプトを復唱することがある:
                    //   "pickup if a person is reaching, browsing if..."
                    // 対策: まず短い応答なら全体でマッチ、
                    //       長い応答なら先頭の単語のみでマッチ
                    std::string response_lower = response;
                    std::transform(response_lower.begin(), response_lower.end(),
                                   response_lower.begin(), ::tolower);

                    result.answer = "No Event Detected";
                    if (m_prompts.contains("use_cases") &&
                        m_prompts["use_cases"].contains(m_trigger))
                    {
                    auto& uc = m_prompts["use_cases"][m_trigger];

                    // ---- キーワードマッチング方式 ----
                    // JSON に "keywords" があれば、モデルの自由回答から
                    // キーワードで分類する (options 順に優先)
                    if (uc.contains("keywords") && uc["keywords"].is_object()) {
                        auto& kw_map = uc["keywords"];
                        for (const auto& opt : uc["options"]) {
                            std::string cat = opt.get<std::string>();
                            if (!kw_map.contains(cat)) continue;
                            for (const auto& kw : kw_map[cat]) {
                                std::string k = kw.get<std::string>();
                                std::transform(k.begin(), k.end(),
                                               k.begin(), ::tolower);
                                if (response_lower.find(k) != std::string::npos) {
                                    result.answer = cat;
                                    goto matched;
                                }
                            }
                        }
                        // どのキーワードにもマッチしない場合
                        // → 人に言及していない → 最初のオプション (empty) を使用
                        if (!uc["options"].empty()) {
                            result.answer = uc["options"][0].get<std::string>();
                        }
                    }
                    // ---- フォールバック: 旧方式 (options 直接マッチ) ----
                    else if (uc.contains("options")) {
                        // 先頭部分を抽出 (復唱対策)
                        std::string first_part = response_lower;
                        for (const char* delim : {"\n", ".", ",", " if ", " or "}) {
                            auto pos = first_part.find(delim);
                            if (pos != std::string::npos && pos > 0)
                                first_part = first_part.substr(0, pos);
                        }
                        auto trim = [](std::string& s) {
                            const char* ws = " \t\n\r'\"";
                            auto l = s.find_first_not_of(ws);
                            auto r = s.find_last_not_of(ws);
                            s = (l != std::string::npos) ? s.substr(l, r - l + 1) : "";
                        };
                        trim(first_part);

                        for (const auto& opt : uc["options"]) {
                            std::string o = opt.get<std::string>();
                            std::string o_lower = o;
                            std::transform(o_lower.begin(), o_lower.end(),
                                           o_lower.begin(), ::tolower);
                            if (first_part == o_lower ||
                                first_part.rfind(o_lower, 0) == 0) {
                                result.answer = o;
                                break;
                            }
                        }
                        // 短い応答なら含有マッチ
                        if (result.answer == "No Event Detected" &&
                            response_lower.size() < 30)
                        {
                            for (const auto& opt : uc["options"]) {
                                std::string o = opt.get<std::string>();
                                std::string o_lower = o;
                                std::transform(o_lower.begin(), o_lower.end(),
                                               o_lower.begin(), ::tolower);
                                if (response_lower.find(o_lower) != std::string::npos) {
                                    result.answer = o;
                                    break;
                                }
                            }
                        }
                    }
                    } // use_cases guard
                    matched:

                    // デバッグ: 生レスポンスを表示
                    if (!response.empty()) {
                        std::string preview = response.substr(0, 80);
                        if (response.size() > 80) preview += "...";
                        result.answer += " [raw: " + preview + "]";
                    }

                } catch (const std::exception& e) {
                    result.answer = std::string("Error: ") + e.what();
                    try { vlm.clear_context(); } catch (...) {}

                    // エラー時: ジェネレーター再作成を試行
                    std::cerr << "\n[Backend] Monitor error, recreating generator..."
                              << std::endl;
                    try {
                        monitor_gen.reset();
                        monitor_gen = create_monitor_generator();
                        std::cout << "[Backend] Generator recreated OK." << std::endl;
                    } catch (const std::exception& e2) {
                        std::cerr << "[Backend] Recreate failed: " << e2.what() << std::endl;
                    }
                }

                auto t1 = std::chrono::steady_clock::now();
                double sec = std::chrono::duration<double>(t1 - t0).count();
                std::ostringstream ts;
                ts << std::fixed << std::setprecision(2) << sec << "s";
                result.time_str = ts.str();

                {
                    std::lock_guard<std::mutex> lk(m_mtx);
                    m_result_buf.frame  = std::move(mon_frame);
                    m_result_buf.result = std::move(result);
                    m_has_result = true;
                }
                last_infer = std::chrono::steady_clock::now();
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[Backend] Fatal: " << e.what() << std::endl;
    }

    m_device_ready = false;
    m_worker_done = true;
    std::cout << "[Backend] Worker exiting." << std::endl;
}