# VLM Windows Demo on Hailo-10H

A real-time video monitoring and interactive image analysis application using Vision Language Model (VLM) powered by the Hailo-10H AI processor.

Two implementations are available: C++ (recommended) and Python, both sharing the same prompt files and hef model.

---

## Table of Contents

1. [Requirements](#requirements)
2. [Folder Structure](#folder-structure)
3. [Getting Started](#getting-started)
4. [C++ Version (Recommended)](#c-version-recommended)
5. [Python Version](#python-version)
6. [Prompt Files](#prompt-files)
7. [Usage](#usage)
8. [Troubleshooting](#troubleshooting)

---

## Requirements

| Item | Requirement |
|------|-------------|
| OS | Windows 10, 11 (tested) |
| AI Processor | Hailo-10H 8GB/4GB |
| Connection | PCIe / M.2 slot on desktop PC, or Thunderbolt adapter for laptops |
| HailoRT | 5.2.0 |
| Model | Qwen2-VL-2B-Instruct |

---

## Folder Structure

```
VLM-Windows-Demo/
│
├── C++/                        # C++ source code
│   ├── backend.cpp
│   ├── backend.h
│   ├── CMakeLists.txt
│   └── main.cpp
│
├── Python/                     # Python source code
│   ├── app.py
│   ├── backend.py
│   ├── requirements.txt
│   └── README.md
│
├── hef/                        # Model file (shared by C++/Python)
│   └── Qwen2-VL-2B-Instruct.hef
│
├── Prompts/                    # Prompt definition files (shared by C++/Python)
│   ├── prompt_person.json
│   ├── prompt_phone.json
│   ├── prompt_retail_behavior.json
│   └── ...
│
├── Videos/                     # Demo video files
│   └── ...
│
├── README.md                   # This file
└── README_jp.md                # Japanese README
```

---

## Getting Started

### 1. Download Required Software

Download and install the following software:

| Software | Required Version | Download Link |
|---|---|---|
| HailoRT | 5.2.0 | [Hailo Developer Zone](https://hailo.ai/developer-zone/software-downloads/) (registration required) |
| Build Tools for Visual Studio | 17.X | [Microsoft](https://learn.microsoft.com/en-gb/visualstudio/releases/2022/release-history#release-dates-and-build-numbers) |
| CMake | 3.16 or later | [cmake.org](https://cmake.org/download/) |
| OpenCV | 4.x | [opencv.org](https://opencv.org/releases/) |
| Python | 3.10 | [python.org](https://www.python.org/downloads/release/python-31011/) |
| Git | 2.47.X or later | [gitforwindows.org](https://gitforwindows.org/) |

### 2. Verify Hailo-10H Connection

Install HailoRT with the Hailo-10H connected to your PC. After installation, run the following commands to verify the device is recognized:

```powershell
hailortcli scan
hailortcli fw-control identify
```

If the device is displayed, you're good to go. If not, check the connection and drivers.

```powershell
> hailortcli fw-control identify
Executing on device: 0000:3c:00.0
Identifying board
Control Protocol Version: 2
Firmware Version: 5.2.0 (release,app)
Logger Version: 0
Device Architecture: HAILO10H
```

### 3. Place the hef Model File

Place `Qwen2-VL-2B-Instruct.hef` in the `hef/` folder. If you don't have the file, download it from [Hailo Model Zoo GenAI](https://github.com/hailo-ai/hailo_model_zoo_genai) or use the following command:

```powershell
curl.exe -O "https://dev-public.hailo.ai/v5.2.0/blob/Qwen2-VL-2B-Instruct.hef"
```

---

## C++ Version (Recommended)

### Required Software

| Software | Required Version | Notes |
|---|---|---|
| HailoRT | 5.2.0 | |
| Build Tools for Visual Studio | 2019 or later | Select "Desktop development with C++" |
| CMake | 3.16 or later | |
| OpenCV | 4.x | |

### Set Environment Variable

After installing OpenCV, set the `OpenCV_DIR` environment variable:

```powershell
# Example: If OpenCV is extracted to C:\opencv
[Environment]::SetEnvironmentVariable("OpenCV_DIR", "C:\opencv\build", "User")
```

Open a new command prompt after setting the variable.

### Build

```powershell
cd C++
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

If the environment variable is not set correctly, specify it directly:

```powershell
cmake .. -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:\opencv\build"
```

On successful build, `build\Release\vlm_app.exe` will be created.

### Run

```powershell
# Camera input (monitoring mode)
.\build\Release\vlm_app.exe `
    --prompts ..\Prompts\prompt_person.json `
    --camera 0 `
    --hef ..\hef\Qwen2-VL-2B-Instruct.hef

# Video file input (scaled to 40%)
.\build\Release\vlm_app.exe `
    --prompts ..\Prompts\prompt_person.json `
    --video ..\Videos\1322056-hd_1920_1080_30fps.mp4 `
    --scale 0.4 `
    --hef ..\hef\Qwen2-VL-2B-Instruct.hef

# Video folder playback (loops alphabetically)
.\build\Release\vlm_app.exe `
    --prompts ..\Prompts\prompt_retail_behavior.json `
    --video ..\Videos\ `
    --scale 0.4 `
    --hef ..\hef\Qwen2-VL-2B-Instruct.hef
```

### Command Line Arguments

| Argument | Required | Description | Default |
|----------|----------|-------------|---------|
| `--prompts, -p <file>` | ○ | Path to prompt JSON file | - |
| `--hef, -m <file>` | | Path to hef model file | `Qwen2-VL-2B-Instruct.hef` |
| `--camera, -c <id>` | △ | Camera ID | 0 |
| `--video, -v <path>` | △ | Path to video file or folder | - |
| `--scale <factor>` | | Display scale (e.g., 0.5 for half size) | 1.0 |
| `--cooldown <ms>` | | Interval between inferences in ms | 1000 |
| `--diagnose, -d` | | Device diagnostics mode | - |

Specify either `--camera` or `--video`. If both are omitted, camera 0 is used.

### Source Files

| File | Description |
|------|-------------|
| `main.cpp` | Main application (camera/video input, OpenCV display, interactive mode, argument parsing) |
| `backend.cpp` | VLM inference backend (Hailo device management, inference loop, keyword classification) |
| `backend.h` | Backend class header |
| `CMakeLists.txt` | Build configuration (auto-detection of HailoRT, OpenCV, nlohmann/json) |

---

## Python Version

An early implementation, originally developed for camera input only.

### Required Software

| Software | Required Version | Notes |
|---|---|---|
| Python | 3.10 | HailoRT may not support 3.11 or later |
| HailoRT | 5.2.0 | Includes Python wheel |

### Setup (Windows)

```powershell
# Set execution policy (first time only)
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned

# Check Python version
python --version

# Create and activate virtual environment
python -m venv venv-win
.\venv-win\Scripts\activate

# Install HailoRT Python wheel (adjust filename for your environment)
pip install hailort-5.2.0-cp310-cp310-win_amd64.whl

# Install dependencies
pip install -r requirements.txt
```

### Run

```powershell
cd Python
.\venv-win\Scripts\activate

# Camera input
python app.py --prompts ..\Prompts\prompt_person.json

# Video file input
python app.py --prompts ..\Prompts\prompt_person.json --video ..\Videos\video.mp4

# Video folder playback (scaled to 40%)
python app.py `
    --prompts ..\Prompts\prompt_retail_behavior.json `
    --video ..\Videos\ `
    --scale 0.4

# Device diagnostics
python app.py --diagnose
```

### Command Line Arguments

| Argument | Required | Description | Default |
|----------|----------|-------------|---------|
| `--prompts, -p <file>` | ○ | Path to prompt JSON file | - |
| `--hef, -m <file>` | | Path to hef model file | `../hef/Qwen2-VL-2B-Instruct.hef` |
| `--camera, -c <id>` | △ | Camera ID | 0 |
| `--video, -v <path>` | △ | Path to video file or folder | - |
| `--scale <factor>` | | Display scale (e.g., 0.5 for half size) | 1.0 |
| `--cooldown <ms>` | | Interval between inferences in ms | 1000 |
| `--diagnose, -d` | | Device diagnostics mode | - |

### Source Files

| File | Description |
|------|-------------|
| `app.py` | Main application (camera input, OpenCV display, interactive mode) |
| `backend.py` | VLM inference backend (worker process via multiprocessing) |
| `requirements.txt` | Python dependencies (numpy, opencv-python) |

---

## Prompt Files

Prompt files are in JSON format and define detection targets for monitoring. The format is shared between C++ and Python versions.

### Basic Structure

```json
{
    "use_cases": {
        "category_name": {
            "options": ["option1", "option2"],
            "details": "Specific instructions for the model"
        }
    },
    "hailo_system_prompt": "System prompt",
    "hailo_user_prompt": "{details}"
}
```

The `{details}` placeholder in `hailo_user_prompt` is automatically replaced with the `details` from the first use_case.

### Keyword-Based Classification

A high-accuracy classification method tailored for 2B model characteristics. Categories are determined by matching keywords in the model's free-form response.

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

The order of `options` determines matching priority (first has highest priority). If no keyword matches, the first option is used as fallback.

### Included Prompts

| File | Purpose | Classification |
|------|---------|----------------|
| `prompt_hat.json` | Hat detection | Direct |
| `prompt_person.json` | Person presence detection | Direct |
| `prompt_phone.json` | Phone usage detection | Direct |
| `prompt_retail_behavior.json` | Retail behavior analysis (empty/pickup/browsing) | Keyword |
| `prompt_retail_stock.json` | Shelf stock detection | Keyword |

---

## Usage

Basic operations are the same for both C++ and Python versions.

### Display Layout

After launching the app, two OpenCV windows are displayed:

- **Video**: Real-time camera or video feed
- **Frame**: Frame used for the last inference (frozen in interactive mode)

### Monitoring Mode

The app automatically runs in monitoring mode after launch. Frames are periodically captured and analyzed by the VLM, with results displayed in the console.

```
[12:52:20] [INFO] pickup [raw: examining product] | 1.98s
[12:52:24] [OK] empty [raw: no person visible] | 1.72s
[12:52:28] [INFO] browsing [raw: person walking] | 1.85s
```

| Tag | Meaning |
|-----|---------|
| `[OK]` | No Event Detected |
| `[INFO]` | Event detected |
| `[WARN]` | Error or timeout |

### Interactive Mode (Ask Mode)

1. Press **Enter** to pause monitoring and freeze the current frame
2. Type your question and press **Enter** (empty input defaults to "Describe the image")
3. The VLM analyzes the image and displays its response
4. Press **Enter** to return to monitoring mode

### Key Controls

| Key | Action |
|-----|--------|
| `Enter` | Switch to interactive mode / Submit question / Resume monitoring |
| `q` | Exit application |
| `Ctrl+C` | Force exit |

---

## Troubleshooting

### Device Not Found

If `hailortcli scan` doesn't show the device, verify that the Hailo-10H is properly connected. For PCIe / M.2 connections, a PC restart may be required. For Thunderbolt connections, check the adapter's power supply.

### Slowdown or Freeze During Long Operation

This is caused by Hailo-10H overheating. The following mitigations are built in:

- **Context Clear**: `vlm.clear_context()` is called after each inference
- **Cooldown**: Adjustable inference interval via `--cooldown` (default 1000ms)
- **Error Recovery**: Generator is recreated on inference errors

For desktop PCs, ensure adequate airflow around the PCIe slot.

### CMake Cannot Find OpenCV

Verify that the `OpenCV_DIR` environment variable is set correctly:

```powershell
echo $env:OpenCV_DIR
# Example: C:\opencv\build
```

If not set, specify it directly when running cmake:

```powershell
cmake .. -DCMAKE_BUILD_TYPE=Release -DOpenCV_DIR="C:\opencv\build"
```

### Python Version Cannot Find hef

The default path is `../hef/Qwen2-VL-2B-Instruct.hef` (relative to the `Python/` folder). Use the `--hef` option to specify a different path.

---

## Comparison: C++ vs Python

| Feature | C++ | Python |
|---------|-----|--------|
| Input Sources | Camera / Video / Folder | Camera / Video / Folder |
| Display Scaling | ○ `--scale` | ○ `--scale` |
| hef Path | ○ `--hef` | ○ `--hef` |
| Cooldown | ○ `--cooldown` | ○ `--cooldown` |
| Keyword Classification | ○ | ○ |
| Diagnostics Mode | ○ `--diagnose` | ○ `--diagnose` |
| Debug Output [raw:] | ○ | ○ |
| Multiprocessing | Threads (std::thread) | Processes (multiprocessing) |
| Inference Speed | ~2.0-2.5 sec/frame | ~2.0-2.5 sec/frame |

Inference speed is dominated by Hailo-10H hardware processing, so there is no significant difference between C++ and Python.

---

## License

HailoRT SDK is subject to Hailo Technologies Ltd. license terms. The Qwen2-VL-2B-Instruct model is subject to Alibaba Cloud license terms.