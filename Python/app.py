# =============================================================================
#  app.py - VLM カメラ/動画アプリケーション (Python)
#
#  C++版と同等の機能:
#    - カメラ入力 / 動画ファイル / フォルダ内の全動画
#    - 表示スケール (--scale)
#    - クールダウン (--cooldown)
#    - 診断モード (--diagnose)
#    - HEFパス指定 (--hef)
#    - デバッグ出力 [raw: ...]
#
#  終了: 'q' キーまたは Ctrl+C
# =============================================================================

import threading
import argparse
import signal
import cv2
import sys
import queue
import multiprocessing
import os
import time
import json
import concurrent.futures
from pathlib import Path
from backend import Backend


def resolve_video_sources(path):
    """動画ファイル一覧を取得 (ファイルまたはフォルダ)"""
    if not path:
        return []
    
    p = Path(path)
    video_extensions = {'.mp4', '.avi', '.mkv', '.mov', '.wmv', '.webm'}
    
    if p.is_dir():
        files = []
        for entry in p.iterdir():
            if entry.is_file() and entry.suffix.lower() in video_extensions:
                files.append(str(entry))
        return sorted(files)
    elif p.is_file():
        return [path]
    return []


def scale_for_display(frame, scale):
    """表示用リサイズ (scale < 1.0 のときのみ縮小)"""
    if scale >= 1.0 or scale <= 0.0:
        return frame
    new_size = (int(frame.shape[1] * scale), int(frame.shape[0] * scale))
    return cv2.resize(frame, new_size, interpolation=cv2.INTER_NEAREST)


class App:
    def __init__(self, prompts=None, camera_id=0, video_path='',
                 hef_path='Qwen2-VL-2B-Instruct.hef', cooldown_ms=1000, scale=1.0):
        self.camera_id = camera_id
        self.video_path = video_path
        self.hef_path = hef_path
        self.cooldown_ms = cooldown_ms
        self.scale = scale
        self.running = True
        self.executor = concurrent.futures.ThreadPoolExecutor()
        self.backend = Backend(prompts=prompts, hef_path=hef_path)
        signal.signal(signal.SIGINT, self.signal_handler)

        # Interactive mode variables
        self.interactive_mode = False
        self.frozen_frame = None
        self.hailo_processing_enabled = True
        self.waiting_for_question = False
        self.waiting_for_continue = False

        # Thread-based non-blocking stdin input
        self._input_queue = queue.Queue()
        self._input_thread = threading.Thread(target=self._input_reader, daemon=True)
        self._input_thread.start()

    def _input_reader(self):
        """Background thread that reads lines from stdin"""
        while self.running:
            try:
                line = input()
                self._input_queue.put(line)
            except EOFError:
                break
            except Exception:
                break

    def signal_handler(self, sig, frame):
        self.stop()

    def stop(self):
        self.running = False
        self.interactive_mode = False
        if self.backend:
            self.backend.close()
        self.executor.shutdown(wait=True)

    def check_keyboard_input(self):
        """Check for input in a non-blocking way"""
        try:
            return self._input_queue.get_nowait()
        except queue.Empty:
            return None

    def find_available_camera(self):
        """Find the first available camera"""
        for cam_id in range(10):
            cap = cv2.VideoCapture(cam_id, cv2.CAP_DSHOW)
            if cap.isOpened():
                cap.release()
                return cam_id
        return None

    def format_video_info(self, cap, name):
        """動画情報のフォーマット"""
        filename = Path(name).name
        w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fps = int(cap.get(cv2.CAP_PROP_FPS))
        return f"Playing: {filename} ({w}x{h} @ {fps}fps)"

    def calc_wait_ms(self, cap, is_video):
        """フレーム待機時間の計算"""
        if not is_video:
            return 25
        fps = cap.get(cv2.CAP_PROP_FPS)
        return max(1, int(1000.0 / fps)) if fps > 0 else 25

    def show_video(self):
        # ---- 入力ソース ----
        video_files = resolve_video_sources(self.video_path)
        use_video = len(video_files) > 0
        video_idx = 0

        cap = cv2.VideoCapture()

        if use_video:
            print(f"Playlist ({len(video_files)} files):")
            for i, f in enumerate(video_files):
                print(f"  [{i}] {f}")
            cap.open(video_files[0])
            if not cap.isOpened():
                print(f"Cannot open: {video_files[0]}")
                return
            print(self.format_video_info(cap, video_files[0]))
        else:
            if self.camera_id != 0:
                cap.open(self.camera_id, cv2.CAP_DSHOW)
            else:
                available_cam = self.find_available_camera()
                if available_cam is None:
                    print("No video devices found.")
                    return
                cap.open(available_cam, cv2.CAP_DSHOW)

            cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
            cap.set(cv2.CAP_PROP_FPS, 30)

            if not cap.isOpened():
                print(f"Error: Could not open camera with ID {self.camera_id}")
                return

        wait_ms = self.calc_wait_ms(cap, use_video)

        # ウィンドウ作成 (WINDOW_AUTOSIZE でサイズ固定)
        cv2.namedWindow('Video', cv2.WINDOW_AUTOSIZE)
        cv2.namedWindow('Frame', cv2.WINDOW_AUTOSIZE)

        # Banner
        mode_str = "VIDEO" if use_video else "CAMERA"
        print("\n" + "=" * 80)
        print(f"  {mode_str} STARTED  |  ENTER=ask  q=quit")
        print("=" * 80 + "\n")

        hailo_future = None
        vlm_future = None
        last_infer_time = time.time() - (self.cooldown_ms / 1000.0)
        pending_video_msg = ""

        while cap.isOpened() and self.running:
            ret, frame = cap.read()
            if not ret or frame is None or frame.size == 0:
                if use_video:
                    # 次の動画 (末尾なら先頭に戻る)
                    video_idx = (video_idx + 1) % len(video_files)
                    cap.open(video_files[video_idx])
                    if cap.isOpened():
                        pending_video_msg = self.format_video_info(cap, video_files[video_idx])
                        wait_ms = self.calc_wait_ms(cap, True)
                    continue
                break

            # 常にライブビデオを表示
            cv2.imshow('Video', scale_for_display(frame, self.scale))

            key = cv2.waitKey(wait_ms) & 0xFF
            if key == ord('q') or key == ord('Q'):
                print("\n'q' pressed - shutting down...")
                self.stop()
                break

            # 入力チェック
            user_input = self.check_keyboard_input()

            # State 1: Normal mode
            if not self.interactive_mode and not self.waiting_for_question and not self.waiting_for_continue:
                # 動画切り替えメッセージ
                if pending_video_msg:
                    print(pending_video_msg)
                    pending_video_msg = ""

                if user_input is not None:
                    self.interactive_mode = True
                    self.frozen_frame = frame.copy()
                    self.hailo_processing_enabled = False
                    self.waiting_for_question = True
                    cv2.imshow('Frame', scale_for_display(self.frozen_frame, self.scale))
                    print("\n\nQuestion (Enter='Describe the image'): ", end="", flush=True)

            # State 2: Waiting for question
            elif self.waiting_for_question and user_input is not None:
                question = user_input.strip()
                self.waiting_for_question = False

                if not question:
                    question = "Describe the image"
                    print(f"=> {question}")

                print("Processing...")
                vlm_future = self.executor.submit(
                    self.backend.vlm_custom_inference,
                    self.frozen_frame.copy(), question
                )

            # State 3: Waiting for continue
            elif self.waiting_for_continue and user_input is not None:
                self.interactive_mode = False
                self.waiting_for_continue = False
                self.hailo_processing_enabled = True
                print("\n" + "=" * 80)
                print("  RESUMED  |  ENTER=ask  q=quit")
                print("=" * 80 + "\n")

            # VLM response
            if self.interactive_mode and vlm_future and vlm_future.done() and not self.waiting_for_continue:
                try:
                    vlm_future.result()
                except Exception as e:
                    print(f"VLM error: {e}")
                vlm_future = None
                self.waiting_for_continue = True
                print("\n\nPress Enter to continue...")

            # Normal Hailo processing with cooldown
            if not self.interactive_mode and self.hailo_processing_enabled:
                current_time = time.time()
                cooldown_sec = self.cooldown_ms / 1000.0

                if hailo_future is None and (current_time - last_infer_time) >= cooldown_sec:
                    hailo_future = self.executor.submit(self.backend.hailo_inference, frame.copy())
                    cv2.imshow('Frame', scale_for_display(frame, self.scale))

                elif hailo_future and hailo_future.done():
                    try:
                        hailo_result = hailo_future.result()
                        now_str = time.strftime("%H:%M:%S")
                        answer = hailo_result.get('answer', '')
                        raw = hailo_result.get('raw', '')
                        time_str = hailo_result.get('time', 'N/A')

                        # タグ判定 (C++版と同等)
                        if answer == "No Event Detected":
                            tag = "[OK]"
                        elif "error" in answer.lower() or "timeout" in answer.lower():
                            tag = "[WARN]"
                        else:
                            tag = "[INFO]"

                        # デバッグ出力 [raw: ...] (C++版と同等)
                        raw_preview = raw[:80] + "..." if len(raw) > 80 else raw
                        print(f"[{now_str}] {tag} {answer} [raw: {raw_preview}] | {time_str}")

                    except Exception as e:
                        print(f"[WARN] Inference error: {e}")

                    hailo_future = None
                    last_infer_time = time.time()

        print("Shutting down...")
        cap.release()
        cv2.destroyAllWindows()

    def run(self):
        self.video_thread = threading.Thread(target=self.show_video)
        self.video_thread.start()
        try:
            self.video_thread.join()
        except KeyboardInterrupt:
            self.stop()
            self.video_thread.join()


if __name__ == "__main__":
    multiprocessing.freeze_support()
    
    parser = argparse.ArgumentParser(description="VLM Camera/Video App (Python)")
    parser.add_argument("--prompts", "-p", type=str, required=False, help="Prompts JSON file")
    parser.add_argument("--camera", "-c", type=int, default=0, help="Camera ID (default: 0)")
    parser.add_argument("--video", "-v", type=str, default="", help="Video file or folder")
    parser.add_argument("--hef", "-m", type=str, default="../hef/Qwen2-VL-2B-Instruct.hef", help="HEF model path")
    parser.add_argument("--scale", type=float, default=1.0, help="Display scale (0.5=half, default: 1.0)")
    parser.add_argument("--cooldown", type=int, default=1000, help="Pause between inferences in ms (default: 1000)")
    parser.add_argument("--diagnose", "-d", action="store_true", help="Device diagnostics")
    args = parser.parse_args()

    # 診断モード
    if args.diagnose:
        success = Backend.diagnose_device()
        sys.exit(0 if success else 1)

    # prompts必須チェック
    if not args.prompts:
        print("Error: --prompts required.")
        sys.exit(1)

    # プロンプト読み込み
    with open(args.prompts, "r", encoding="utf-8") as f:
        prompts = json.load(f)

    # 入力情報表示 (C++版と同等)
    input_str = args.video if args.video else f"Camera {args.camera}"
    print(f"VLM App (Python / HailoRT)")
    print(f"  HEF:      {args.hef}")
    print(f"  Input:    {input_str}")
    print(f"  Scale:    {args.scale}")
    print(f"  Cooldown: {args.cooldown} ms")

    app = App(
        prompts=prompts,
        camera_id=args.camera,
        video_path=args.video,
        hef_path=args.hef,
        cooldown_ms=args.cooldown,
        scale=args.scale
    )
    app.run()
    sys.exit(0)