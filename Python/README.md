# Hailo VLM Interactive Application

An interactive computer vision application using Hailo's Vision Language Model (VLM) for real-time image analysis and question answering.

## Features

- **Real-time video processing** with Hailo AI acceleration
- **Camera or video file input** - USB camera, single video file, or folder of videos
- **Interactive Q&A mode** - press Enter to ask questions about the current frame
- **Dual window display** - continuous video feed and captured frame analysis
- **Keyword-based response classification** - flexible categorization using keywords
- **Display scaling** - adjustable window size for high-resolution videos
- **Cooldown control** - configurable pause between inferences

## Requirements

- Hailo AI processor and SDK (HailoRT 5.2.0+)
- Python 3.8+
- OpenCV
- NumPy
- Hailo Platform libraries

## Installation

```powershell
pip install -r requirements.txt
```

## Files

- `app.py` - Main application with interactive video processing
- `backend.py` - Hailo VLM backend with multiprocessing support
- `prompt_*.json` - Configuration for system prompts and use cases

## Usage

### Basic Usage (Camera)

```powershell
python app.py --prompts prompt_retail_behavior.json
```

### Video File Input

```powershell
python app.py --prompts prompt.json --video video.mp4
```

### Video Folder (Playlist)

```powershell
python app.py --prompts prompt.json --video /path/to/videos/
```

### Full Options

```powershell
python app.py `
    --prompts prompt.json `
    --video /path/to/videos/ `
    --hef Qwen2-VL-2B-Instruct.hef `
    --scale 0.5 `
    --cooldown 2000
```

### Device Diagnostics

```powershell
python app.py --diagnose
```

## Command Line Arguments

| Argument | Short | Description | Default |
|----------|-------|-------------|---------|
| `--prompts` | `-p` | Prompts JSON file (required) | - |
| `--camera` | `-c` | Camera ID | 0 |
| `--video` | `-v` | Video file or folder | - |
| `--hef` | `-m` | HEF model path | Qwen2-VL-2B-Instruct.hef |
| `--scale` | - | Display scale (0.5=half) | 1.0 |
| `--cooldown` | - | Pause between inferences (ms) | 1000 |
| `--diagnose` | `-d` | Device diagnostics | - |

## Interactive Mode

1. Press `Enter` to capture current frame and enter Q&A mode
2. Type your question (or press Enter for "Describe the image")
3. Press `Enter` to continue monitoring

## Prompt JSON Format

```json
{
    "use_cases": {
        "Retail behavior": {
            "options": ["empty", "pickup", "browsing"],
            "keywords": {
                "empty": ["no person", "nobody"],
                "pickup": ["grabbing", "picking", "reaching"],
                "browsing": ["walking", "standing", "looking"]
            },
            "details": "Describe what the person is doing..."
        }
    },
    "hailo_system_prompt": "You are a store camera...",
    "hailo_user_prompt": "{details}"
}
```

## Output Format

```
[HH:MM:SS] [TAG] answer [raw: model_response] | time
```

- `[OK]` - No event detected
- `[INFO]` - Event detected
- `[WARN]` - Error or timeout