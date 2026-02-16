# VLM Windows Demo on Hailo-10H

Hailo-10H AI プロセッサを使用した VLM（Vision Language Model）リアルタイム映像監視・対話型画像分析アプリケーションです。

C++ 版（推奨）と Python 版の2つの実装があり、同じプロンプトファイル・HEF モデルを共有できます。

---

## 目次

1. [動作環境](#動作環境)
2. [フォルダー構成](#フォルダー構成)
3. [事前準備（共通）](#事前準備共通)
4. [C++ 版（推奨）](#c-版推奨)
5. [Python 版](#python-版)
6. [プロンプトファイル](#プロンプトファイル)
7. [操作方法](#操作方法)
8. [トラブルシューティング](#トラブルシューティング)

---

## 動作環境

| 項目 | 要件 |
|------|------|
| OS | Windows 10, 11（動作確認済み） |
| AI プロセッサ | Hailo-10H 8GB |
| 接続方法 | デスクトップ PC の PCIe / M.2 スロット、またはノート PC の Thunderbolt に変換アダプター経由 |
| HailoRT | 5.2.0 |
| モデル | Qwen2-VL-2B-Instruct |

---

## フォルダー構成

```
VLM-Windows-Demo/
│
├── C++/                        # C++ 版ソースコード
│   ├── backend.cpp
│   ├── backend.h
│   ├── CMakeLists.txt
│   └── main.cpp
│
├── Python/                 # Python 版ソースコード
│   ├── app.py
│   ├── backend.py
│   ├── requirements.txt
│   └── README.md
│
├── hef/                        # モデルファイル（C++/Python 共通）
│   └── Qwen2-VL-2B-Instruct.hef
│
├── Prompts/                    # プロンプト定義ファイル（C++/Python 共通）
│   ├── prompt_person.json
│   ├── prompt_phone.json
│   ├── prompt_retail_behavior.json
│   └── ...
│   └── ...
│
├── Videos/                     # デモ用動画ファイル
│   └── ...
│
├── README.md                   # 英語版 README
└── README_jp.md                # 本ファイル
```

---

## 事前準備（共通）

### 1. 必要なソフトウェアのダウンロード

以下のソフトウェアをダウンロードしてインストールしてください。

| ソフトウェア | 必要バージョン | ダウンロードリンク |
|---|---|---|
| HailoRT | 5.2.0 | [Hailo Developer Zone](https://hailo.ai/developer-zone/software-downloads/) （要登録） |
| Build Tools for Visual Studio | 2019 以降(17.X) | [Microsoft](https://learn.microsoft.com/en-gb/visualstudio/releases/2022/release-history#release-dates-and-build-numbers) |
| CMake | 3.16 以降 | [cmake.org](https://cmake.org/download/) |
| OpenCV | 4.x | [opencv.org](https://opencv.org/releases/) |
| Python | 3.10 | [python.org](https://www.python.org/downloads/release/python-31011/) |
| Git | 2.47.X 以降| [gitforwindows.org](https://gitforwindows.org/) |


> **注意**: `Install/` フォルダーにインストーラーの例が含まれていますが、ライセンスの関係で GitHub には公開していません。上記リンクから各自ダウンロードしてください。

### 2. Hailo-10H の接続確認

Hailo-10H を PC に接続した状態で HailoRT をインストールします。インストール後、コマンドプロンプトで以下を実行して、デバイスが認識されていることを確認します。

```powershell
hailortcli scan
hailortcli fw-control identify
```

デバイスが表示されれば OK です。表示されない場合は接続やドライバーを確認してください。

```powershell
> hailortcli fw-control identify
Executing on device: 0000:3c:00.0
Identifying board
Control Protocol Version: 2
Firmware Version: 5.2.0 (release,app)
Logger Version: 0
Device Architecture: HAILO10H
```

### 3. HEF モデルファイルの配置

`Qwen2-VL-2B-Instruct.hef` を `hef/` フォルダーに配置します。ファイルが手元にない場合は [Hailo Model Zoo GenAI](https://github.com/hailo-ai/hailo_model_zoo_genai) からダウンロードするか、以下のコマンドでダウンロードできます。

```powershell
curl.exe -O "https://dev-public.hailo.ai/v5.2.0/blob/Qwen2-VL-2B-Instruct.hef"
```

---

## C++ 版

### 必要なソフトウェア

| ソフトウェア | 必要バージョン | 備考 |
|---|---|---|
| HailoRT | 5.2.0 | |
| Build Tools for Visual Studio | 2019 以降(17.X) | 「C++ によるデスクトップ開発」を選択 |
| CMake | 3.16 以降 | |
| OpenCV | 4.x | |

### 環境変数の設定

OpenCV をインストールした後、環境変数 `OpenCV_DIR` を設定してください。

```powershell
# 例: OpenCV を C:\opencv に展開した場合
[Environment]::SetEnvironmentVariable("OpenCV_DIR", "C:\opencv\build", "User")
```

設定後、新しいコマンドプロンプトを開いてください。

### ビルド

```powershell
cd C++
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

もし環境変数が正しく設定されていない場合、以下のように直接指定してください。

```powershell
cmake .. -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:\opencv\build"
```

ビルドが成功すると `build\Release\vlm_app.exe` が生成されます。

### 実行

```powershell
# カメラ入力（監視モード）
.\build\Release\vlm_app.exe `
    --prompts ..\Prompts\prompt_person.json `
    --camera 0 `
    --hef ..\hef\Qwen2-VL-2B-Instruct.hef

# 動画ファイル入力（表示を 40% に縮小）
.\build\Release\vlm_app.exe `
    --prompts ..\Prompts\prompt_person.json `
    --video ..\Videos\1322056-hd_1920_1080_30fps.mp4 `
    --scale 0.4 `
    --hef ..\hef\Qwen2-VL-2B-Instruct.hef

# 動画フォルダー一括再生（アルファベット順にループ）
.\build\Release\vlm_app.exe `
    --prompts ..\Prompts\prompt_retail_behavior.json `
    --video ..\Videos\ `
    --scale 0.4 `
    --hef ..\hef\Qwen2-VL-2B-Instruct.hef
```

### コマンドライン引数

| 引数 | 必須 | 説明 | デフォルト |
|------|------|------|------|
| `--prompts, -p <file>` | ○ | プロンプト JSON ファイルのパス | - |
| `--hef, -m <file>` | | HEF モデルファイルのパス | `Qwen2-VL-2B-Instruct.hef` |
| `--camera, -c <id>` | △ | カメラ ID | 0 |
| `--video, -v <path>` | △ | 動画ファイルまたはフォルダーのパス | - |
| `--scale <factor>` | | 表示倍率（例: 0.5 で半分のサイズ） | 1.0 |
| `--cooldown <ms>` | | 監視推論の間隔 ms | 1000 |
| `--diagnose, -d` | | デバイス診断モード | - |

`--camera` と `--video` はどちらか一方を指定してください。両方省略した場合はカメラ 0 が使用されます。

### ソースファイル

| ファイル | 説明 |
|---|---|
| `main.cpp` | メインアプリ（カメラ/動画入力、OpenCV 表示、対話モード、引数解析） |
| `backend.cpp` | VLM 推論バックエンド（Hailo デバイス管理、推論ループ、キーワード分類） |
| `backend.h` | Backend クラスのヘッダー |
| `CMakeLists.txt` | ビルド設定（HailoRT, OpenCV, nlohmann/json の自動検出） |

---

## Python 版

初期に開発された実装で、カメラ入力のみに対応しています。

### 必要なソフトウェア

| ソフトウェア | 必要バージョン | 備考 |
|---|---|---|
| Python | 3.10 | 3.11 以降は HailoRT 非対応の可能性あり |
| HailoRT | 5.2.0 | Python wheel 含む |

### セットアップ（Windows）

```powershell
# 実行ポリシーの設定（初回のみ）
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned

# Python バージョン確認
python --version

# 仮想環境の作成と有効化
python -m venv venv-win
.\venv-win\Scripts\activate

# HailoRT Python wheel のインストール（環境に合わせてファイル名を変更）
pip install hailort-5.2.0-cp310-cp310-win_amd64.whl

# 依存パッケージのインストール
pip install -r requirements.txt
```

### 実行

```powershell
cd Python
.\venv-win\Scripts\activate

# カメラ入力
python app.py --prompts ..\Prompts\prompt_person.json

# 動画ファイル入力
python app.py --prompts ..\Prompts\prompt_person.json --video ..\Videos\video.mp4

# 動画フォルダー一括再生（表示を 40% に縮小）
python app.py `
    --prompts ..\Prompts\prompt_retail_behavior.json `
    --video ..\Videos\ `
    --scale 0.4

# デバイス診断
python app.py --diagnose
```

### コマンドライン引数

| 引数 | 必須 | 説明 | デフォルト |
|------|------|------|------|
| `--prompts, -p <file>` | ○ | プロンプト JSON ファイルのパス | - |
| `--hef, -m <file>` | | HEF モデルファイルのパス | `../hef/Qwen2-VL-2B-Instruct.hef` |
| `--camera, -c <id>` | △ | カメラ ID | 0 |
| `--video, -v <path>` | △ | 動画ファイルまたはフォルダーのパス | - |
| `--scale <factor>` | | 表示倍率（例: 0.5 で半分のサイズ） | 1.0 |
| `--cooldown <ms>` | | 監視推論の間隔 ms | 1000 |
| `--diagnose, -d` | | デバイス診断モード | - |

### ソースファイル

| ファイル | 説明 |
|---|---|
| `app.py` | メインアプリ（カメラ入力、OpenCV 表示、対話モード） |
| `backend.py` | VLM 推論バックエンド（multiprocessing でワーカープロセスを分離） |
| `requirements.txt` | Python 依存パッケージ（numpy, opencv-python） |

---

## プロンプトファイル

プロンプトファイルは JSON 形式で、監視時の検出対象を定義します。C++ 版と Python 版で共通のフォーマットです。

### 基本構造

```json
{
    "use_cases": {
        "カテゴリ名": {
            "options": ["選択肢1", "選択肢2"],
            "details": "モデルへの具体的な指示"
        }
    },
    "hailo_system_prompt": "システムプロンプト",
    "hailo_user_prompt": "{details}"
}
```

`hailo_user_prompt` 内の `{details}` は、最初の use_case の `details` で自動的に置換されます。

### キーワードベース分類

2B モデルの特性に合わせた高精度な分類方式です。モデルの自由回答からキーワードでカテゴリを判定します。

```json
{
    "use_cases": {
        "retail_behavior": {
            "options": ["empty", "pickup", "browsing"],
            "keywords": {
                "empty":    ["no person", "nobody", "no one", "empty aisle"],
                "pickup":   ["grabbing", "picking", "examining", "selecting", "handling"],
                "browsing": ["person", "walking", "pushing", "cart", "shopping"]
            },
            "details": "What is the person's hands doing in this store image? ..."
        }
    },
    "hailo_system_prompt": "Describe the person's action with specific verbs...",
    "hailo_user_prompt": "{details}"
}
```

`options` の記載順がマッチング優先度になります（先頭が最優先）。どのキーワードにもマッチしない場合は最初のオプションがフォールバックとして使用されます。

### 同梱プロンプト一覧

| ファイル | 用途 | 分類方式 |
|---|---|---|
| `prompt_hat.json` | 帽子着用検出 | 直接分類 |
| `prompt_person.json` | 人物の有無検出 | 直接分類 |
| `prompt_phone.json` | 電話使用検出 | 直接分類 |
| `prompt_retail_behavior.json` | 小売店行動分析（empty/pickup/browsing） | キーワード |
| `prompt_retail_stock.json` | 棚在庫状況検出 | キーワード |

---

## 操作方法

C++ 版と Python 版で基本操作は共通です。

### 画面構成

アプリ起動後、2 つの OpenCV ウィンドウが表示されます。

- **Video**: カメラまたは動画のリアルタイム映像
- **Frame**: 直前の推論に使用されたフレーム（対話モード時は固定フレーム）

### 監視モード

アプリ起動後、自動的に監視モードで動作します。定期的にフレームを取得し、VLM で分析した結果がコンソールに表示されます。

```
[12:52:20] [INFO] pickup [raw: examining product] | 1.98s
[12:52:24] [OK] empty [raw: no person visible] | 1.72s
[12:52:28] [INFO] browsing [raw: person walking] | 1.85s
```

| タグ | 意味 |
|------|------|
| `[OK]` | No Event Detected（イベントなし） |
| `[INFO]` | イベント検出 |
| `[WARN]` | エラーまたはタイムアウト |

### 対話モード（Ask モード）

1. **Enter** を押すと監視が一時停止し、現在のフレームが固定されます
2. 質問を入力して **Enter**（空欄で Enter を押すと「Describe the image」がデフォルト）
3. VLM が画像を分析して回答を表示します
4. **Enter** で監視モードに復帰します

### キー操作

| キー | 動作 |
|------|------|
| `Enter` | 対話モードへ切り替え / 質問の送信 / 監視復帰 |
| `q` | アプリ終了 |
| `Ctrl+C` | アプリ強制終了 |

---

## トラブルシューティング

### デバイスが見つからない

`hailortcli scan` でデバイスが表示されない場合、Hailo-10H が正しく接続されているか確認してください。PCIe / M.2 接続の場合は PC の再起動が必要なことがあります。Thunderbolt 接続の場合はアダプターの電源供給を確認してください。

### 長時間動作で遅くなる・停止する

Hailo-10H の発熱が原因です。以下の対策が組み込まれています。

- **コンテキストクリア**: 毎推論後に `vlm.clear_context()` を実行
- **クールダウン**: `--cooldown` で推論間隔を調整可能（デフォルト 1000ms）
- **エラー時リカバリー**: 推論エラー時に Generator を再作成

デスクトップ PC の場合、PCIe スロット周辺のエアフローを確保してください。

### CMake で OpenCV が見つからない

環境変数 `OpenCV_DIR` が正しく設定されているか確認してください。

```powershell
echo $env:OpenCV_DIR
# 例: C:\opencv\build
```

設定されていない場合は cmake 実行時に直接指定できます。

```powershell
cmake .. -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:\opencv\build"
```

### Python 版で HEF が見つからない

デフォルトパスは `../hef/Qwen2-VL-2B-Instruct.hef`（`Python/` フォルダーからの相対パス）です。`--hef` オプションで別のパスを指定することもできます。

---

## C++ 版と Python 版の比較

| 項目 | C++ 版 | Python 版 |
|------|--------|-----------|
| 入力ソース | カメラ / 動画 / フォルダー | カメラ / 動画 / フォルダー |
| 表示スケーリング | ○ `--scale` 対応 | ○ `--scale` 対応 |
| HEF パス指定 | ○ `--hef` 対応 | ○ `--hef` 対応 |
| クールダウン | ○ `--cooldown` 対応 | ○ `--cooldown` 対応 |
| キーワード分類 | ○ | ○ |
| 診断モード | ○ `--diagnose` 対応 | ○ `--diagnose` 対応 |
| デバッグ出力 [raw:] | ○ | ○ |
| マルチプロセス | スレッド（std::thread） | プロセス（multiprocessing） |
| 推論速度 | 約 2.0〜2.5 秒/フレーム | 約 2.0〜2.5 秒/フレーム |

推論速度は Hailo-10H のハードウェア処理が大部分を占めるため、C++ / Python 間で大きな差はありません。

---

## ライセンス

HailoRT SDK は Hailo Technologies Ltd. のライセンスに従います。Qwen2-VL-2B-Instruct モデルは Alibaba Cloud のライセンスに従います。
