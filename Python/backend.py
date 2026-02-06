# =============================================================================
#  backend.py - Hailo VLM Backend (Python)
#
#  C++版と同等の機能:
#    - キーワードベースの回答分類
#    - デバッグ出力 [raw: ...]
#    - 診断モード
#    - HEFパス指定
# =============================================================================

import time
import multiprocessing as mp
import numpy as np
import cv2
from hailo_platform import VDevice
from hailo_platform.genai import VLM


def vlm_worker_process(request_queue, response_queue, hef_path, max_tokens, temperature, seed):
    """Standalone worker function that creates its own VLM instance"""
    try:
        vdevice = VDevice()
        vlm = VLM(vdevice, hef_path)

        while True:
            item = request_queue.get()
            if item is None:
                break
            try:
                current_max_tokens = item.get('max_tokens', max_tokens)
                result = _hailo_inference_inner(
                    item['numpy_image'], vlm, item['trigger'],
                    item['prompts'], current_max_tokens, temperature, seed
                )
                response_queue.put({'result': result, 'error': None})
            except Exception as e:
                import traceback
                response_queue.put({'result': None, 'error': f"{str(e)}\n{traceback.format_exc()}"})

    except Exception as e:
        response_queue.put({'result': None, 'error': f"Worker initialization failed: {str(e)}"})
    finally:
        try:
            vlm.release()
            vdevice.release()
        except:
            pass


def _classify_response(response, prompts, trigger):
    """
    キーワードベースの回答分類 (C++版と同等)
    
    1. keywords がある場合: レスポンスからキーワードでカテゴリを判定
    2. keywords がない場合: options の直接マッチ (フォールバック)
    """
    response_lower = response.lower()
    use_case = prompts['use_cases'].get(trigger, {})
    options = use_case.get('options', [])
    keywords = use_case.get('keywords', {})
    
    # キーワードマッチング方式
    if keywords:
        for option in options:
            if option not in keywords:
                continue
            for keyword in keywords[option]:
                if keyword.lower() in response_lower:
                    return option
        # どのキーワードにもマッチしない場合 → 最初のオプション
        if options:
            return options[0]
        return "No Event Detected"
    
    # フォールバック: options 直接マッチ
    # 先頭部分を抽出 (復唱対策)
    first_part = response_lower
    for delim in ['\n', '.', ',', ' if ', ' or ']:
        pos = first_part.find(delim)
        if pos > 0:
            first_part = first_part[:pos]
    first_part = first_part.strip().strip("'\"")
    
    for option in options:
        option_lower = option.lower()
        if first_part == option_lower or first_part.startswith(option_lower):
            return option
    
    # 短い応答なら含有マッチ
    if len(response_lower) < 30:
        for option in options:
            if option.lower() in response_lower:
                return option
    
    return "No Event Detected"


def _hailo_inference_inner(image, vlm, trigger, prompts, max_tokens, temperature, seed):
    """Standalone inference function"""
    if trigger == 'custom':
        structured_prompt = [
            {
                "role": "system",
                "content": [{"type": "text", "text": prompts['hailo_system_prompt']}]
            },
            {
                "role": "user",
                "content": [
                    {"type": "image"},
                    {"type": "text", "text": prompts['hailo_user_prompt']}
                ]
            }
        ]
    else:
        details = prompts['use_cases'][trigger].get('details', '')
        user_prompt = prompts['hailo_user_prompt'].replace("{details}", details)
        structured_prompt = [
            {
                "role": "system",
                "content": [{"type": "text", "text": prompts['hailo_system_prompt']}]
            },
            {
                "role": "user",
                "content": [
                    {"type": "image"},
                    {"type": "text", "text": user_prompt}
                ]
            }
        ]

    try:
        response = ''
        start_time = time.time()
        
        with vlm.generate(prompt=structured_prompt, frames=[image],
                          temperature=temperature, seed=seed,
                          max_generated_tokens=max_tokens) as generation:
            num_tokens = 0
            for chunk in generation:
                if trigger == 'custom':
                    if chunk != '<|im_end|>':
                        print(chunk, end='', flush=True)
                response += chunk
                num_tokens += 1
                if num_tokens > max_tokens:
                    break
        
        vlm.clear_context()

        # Parse response
        response = response.replace('<|im_end|>', '').strip()

        if trigger == 'custom':
            parsed_response = response
        else:
            parsed_response = _classify_response(response, prompts, trigger)

        return {
            'answer': parsed_response,
            'raw': response,
            'time': f"{time.time() - start_time:.2f}s"
        }
    except Exception as e:
        return {
            'answer': f'Error: {str(e)}',
            'raw': '',
            'time': f"{time.time() - start_time:.2f}s"
        }


class Backend:
    def __init__(self, prompts=None, hef_path='../hef/Qwen2-VL-2B-Instruct.hef'):
        self.prompts = prompts
        self.hef_path = hef_path
        self.max_tokens = 15  # C++版と同じ
        self.temperature = 0.1
        self.seed = 42
        self.trigger = list(self.prompts['use_cases'].keys())[0] if prompts else None
        
        self._request_queue = mp.Queue()
        self._response_queue = mp.Queue()
        self._process = mp.Process(
            target=vlm_worker_process,
            args=(self._request_queue, self._response_queue,
                  self.hef_path, self.max_tokens, self.temperature, self.seed)
        )
        self._process.start()
        print(f"[Backend] Active use case: \"{self.trigger}\"")

    def hailo_inference(self, image):
        """監視推論"""
        self._request_queue.put({
            'numpy_image': self.convert_resize_image(image),
            'trigger': self.trigger,
            'prompts': self.prompts
        })
        try:
            response = self._response_queue.get(timeout=20)
        except Exception:
            self._clear_queues()
            return {'answer': 'Hailo timeout', 'raw': '', 'time': '20s'}
        if response['error']:
            return {'answer': 'Hailo error', 'raw': '', 'time': 'N/A'}
        return response['result']

    def vlm_custom_inference(self, image, custom_prompt):
        """カスタム推論 (質問応答)"""
        custom_prompts = {
            'hailo_system_prompt': "You are a helpful assistant that analyzes images and answers questions about them.",
            'hailo_user_prompt': custom_prompt,
            'use_cases': {'custom': {'details': custom_prompt, 'options': []}}
        }
        custom_max_tokens = 200
        self._request_queue.put({
            'numpy_image': self.convert_resize_image(image),
            'trigger': 'custom',
            'prompts': custom_prompts,
            'max_tokens': custom_max_tokens
        })
        try:
            response = self._response_queue.get(timeout=60)
        except Exception:
            self._clear_queues()
            return {'answer': 'VLM timeout', 'raw': '', 'time': '60s'}
        if response['error']:
            return {'answer': 'VLM error', 'raw': '', 'time': 'N/A'}
        return response['result']

    def _clear_queues(self):
        """タイムアウト時のキュークリア"""
        while not self._request_queue.empty():
            try:
                self._request_queue.get_nowait()
            except:
                break
        while not self._response_queue.empty():
            try:
                self._response_queue.get_nowait()
            except:
                break

    def close(self):
        """Clean shutdown of the backend"""
        try:
            self._request_queue.put(None)
            self._process.join(timeout=5)
            if self._process.is_alive():
                print("[Backend] Worker not responding - terminating.")
                self._process.terminate()
                self._process.join()
        except Exception as e:
            print(f"[Backend] Error closing: {e}")

    @staticmethod
    def convert_resize_image(image_array, target_shape=(336, 336, 3), target_dtype=np.uint8):
        """画像の前処理 (C++版と同等)"""
        target_height, target_width, _ = target_shape
        img = image_array.copy()
        if len(img.shape) == 3 and img.shape[2] == 3:
            img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        if img.shape[0] != target_height or img.shape[1] != target_width:
            # C++版と同じく INTER_NEAREST を使用 (高速)
            img = cv2.resize(img, (target_width, target_height), interpolation=cv2.INTER_NEAREST)
        img = img.astype(target_dtype)
        return img

    @staticmethod
    def diagnose_device():
        """デバイス診断 (C++版と同等)"""
        print("[Diag] ===== Hailo Device Diagnostics =====")
        try:
            from hailo_platform import Device
            devices = Device.scan()
            if not devices:
                print("[Diag] No devices found.")
                return False
            for dev_id in devices:
                print(f"[Diag] Device: {dev_id}")
            # 接続テスト
            vdevice = VDevice()
            print("[Diag] VDevice created OK.")
            vdevice.release()
            print("[Diag] OK")
            return True
        except Exception as e:
            print(f"[Diag] Error: {e}")
            return False