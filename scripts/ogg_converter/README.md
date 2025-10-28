# 小智AI OGG 批量转换器

本脚本为OGG批量转换工具，支持将输入的音频文件转换为小智可使用的OGG格式
基于 Python 第三方库 `ffmpeg-python` 实现。
支持 OGG 与常见音频格式之间的互转，并提供响度相关设置。

注意：本工具依赖系统已安装 FFmpeg（可执行文件需在 PATH 中）。

# 创建并激活虚拟环境

```bash
# 创建虚拟环境
python -m venv venv
# 激活虚拟环境
# Mac/Linux
source venv/bin/activate
# Windows
venv\Scripts\activate
```

# 安装依赖

请在虚拟环境中执行

```bash
pip install ffmpeg-python
```

如果系统未安装 FFmpeg，请先安装：
- Windows（推荐）: `winget install ffmpeg`
- macOS（Homebrew）: `brew install ffmpeg`
- Linux（Debian/Ubuntu）: `sudo apt-get install ffmpeg`

# 运行脚本
本工具为 GUI 应用。

```bash
# 从仓库根目录运行（建议在已激活的虚拟环境中）
python scripts/ogg_converter/xiaozhi_ogg_converter.py
```

启动后：
- 选择“转换模式”（音频→OGG 或 OGG→音频）
- 通过“选择文件”添加待转换文件，勾选或直接“转换全部文件”
- 设置输出目录，点击“转换”按钮

输出文件默认写入 `scripts/ogg_converter/output/`（可在界面修改）

