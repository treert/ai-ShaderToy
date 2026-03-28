#!/usr/bin/env python3
"""
json2dir.py — ShaderToy JSON 转目录格式工具

将 ShaderToy 导出的 JSON shader 文件转换为项目支持的目录格式：
  image.glsl + buf_a~d.glsl + common.glsl + channels.json

用法:
  python json2dir.py <input.json> [-o <output_dir>] [--force]

示例:
  python json2dir.py assets/shaders/a_black_hole.json
  python json2dir.py assets/shaders/a_black_hole.json -o assets/shaders/black_hole
  python json2dir.py assets/shaders/my_sdf.json --force
"""

import json
import argparse
import os
import sys
import re
from pathlib import Path


# Buffer 名称映射
BUFFER_NAMES = ["buf_a", "buf_b", "buf_c", "buf_d"]
BUFFER_LABELS = ["Buffer A", "Buffer B", "Buffer C", "Buffer D"]


def parse_buffer_index(name: str) -> int:
    """从 buffer 名称的最后一个字符解析索引 (A=0, B=1, C=2, D=3)。
    支持 "Buffer A", "Buf A", "A" 等格式。"""
    if not name:
        return -1
    last_char = name.strip()[-1].upper()
    if 'A' <= last_char <= 'D':
        return ord(last_char) - ord('A')
    return -1


def parse_buffer_src_index(src: str) -> int:
    """从 src 字符串中解析 buffer 索引（fallback 方案）。
    - 从末尾向前扫描第一个 A-D 字符
    - 或尝试解析数字 ID (257=A, 258=B, 259=C, 260=D)"""
    if not src:
        return -1
    # 从末尾向前扫描 A-D
    for ch in reversed(src):
        c = ch.upper()
        if 'A' <= c <= 'D':
            return ord(c) - ord('A')
    # 尝试数字 ID
    try:
        num = int(src)
        if 257 <= num <= 260:
            return num - 257
    except ValueError:
        pass
    return -1


def resolve_input_buffer_index(input_id: str, input_path: str, output_id_map: dict) -> int:
    """解析 buffer 类型 input 的 buffer 索引，三级 fallback：
    1. output id 映射表
    2. filepath 中的 bufferNN 正则
    3. ParseBufferSrcIndex 旧逻辑"""
    # 1. output id 映射表
    if input_id and input_id in output_id_map:
        return output_id_map[input_id]
    # 2. 正则匹配 bufferNN
    m = re.search(r'buffer0*(\d+)', input_path)
    if m:
        return int(m.group(1))
    # 3. fallback
    return parse_buffer_src_index(input_path if input_path else input_id)


def get_field(obj: dict, primary: str, fallback: str, default="") -> str:
    """兼容两套字段名，优先读 primary，fallback 读 secondary。"""
    val = obj.get(primary, "")
    if isinstance(val, str) and val:
        return val
    return obj.get(fallback, default)


def convert_json_to_dir(json_path: str, output_dir: str, force: bool = False):
    """主转换逻辑。"""
    json_path = Path(json_path)
    if not json_path.exists():
        print(f"错误: 文件不存在: {json_path}", file=sys.stderr)
        return False

    # 读取 JSON
    with open(json_path, "r", encoding="utf-8") as f:
        try:
            data = json.load(f)
        except json.JSONDecodeError as e:
            print(f"错误: JSON 解析失败: {e}", file=sys.stderr)
            return False

    # 兼容两种 JSON 格式: { "Shader": { ... } } 和 { "renderpass": [...] }
    if "Shader" in data:
        shader_obj = data["Shader"]
    elif "renderpass" in data:
        shader_obj = data
    else:
        print("错误: JSON 缺少 'Shader' 或 'renderpass' 字段", file=sys.stderr)
        return False

    # 提取项目名称
    project_name = ""
    if "info" in shader_obj and "name" in shader_obj["info"]:
        project_name = shader_obj["info"]["name"]
    if not project_name:
        project_name = json_path.stem

    # 检查 renderpass
    renderpasses = shader_obj.get("renderpass")
    if not renderpasses or not isinstance(renderpasses, list):
        print("错误: JSON 缺少 'renderpass' 数组", file=sys.stderr)
        return False

    # ---- 第一遍：扫描 buffer/cubemap outputs，建立 output id → index 映射 ----
    CUBEMAP_PASS_INDEX = 10  # 特殊标记
    output_id_map = {}  # output_id -> buffer_index or CUBEMAP_PASS_INDEX
    buf_count = 0
    for rp in renderpasses:
        rp_type = get_field(rp, "type", "type")
        if rp_type == "buffer":
            name = rp.get("name", "")
            buf_idx = parse_buffer_index(name)
            if buf_idx < 0:
                buf_idx = buf_count
            buf_count += 1

            outputs = rp.get("outputs", [])
            if isinstance(outputs, list):
                for out in outputs:
                    out_id = out.get("id", "")
                    if out_id:
                        output_id_map[out_id] = buf_idx
        elif rp_type == "cubemap":
            outputs = rp.get("outputs", [])
            if isinstance(outputs, list):
                for out in outputs:
                    out_id = out.get("id", "")
                    if out_id:
                        output_id_map[out_id] = CUBEMAP_PASS_INDEX

    # ---- 第二遍：解析各 renderpass ----
    common_code = None
    image_code = None
    image_inputs = []  # [(channel, binding_info), ...]
    # buffer_slots[0..3] = { "code": ..., "inputs": [...] } or None
    buffer_slots = [None, None, None, None]
    cube_a = None  # { "code": ..., "inputs": [...] } or None

    for rp in renderpasses:
        rp_type = get_field(rp, "type", "type")
        code = rp.get("code", "")

        if rp_type == "common":
            common_code = code
            continue

        if rp_type == "image":
            image_code = code
            image_inputs = _parse_inputs(rp, output_id_map, CUBEMAP_PASS_INDEX)
            continue

        if rp_type == "buffer":
            name = rp.get("name", "")
            buf_idx = parse_buffer_index(name)
            if buf_idx < 0 or buf_idx >= 4:
                print(f"警告: 无法识别 buffer 名称 '{name}'，跳过", file=sys.stderr)
                continue
            buffer_slots[buf_idx] = {
                "code": code,
                "inputs": _parse_inputs(rp, output_id_map, CUBEMAP_PASS_INDEX),
            }
            continue

        if rp_type == "cubemap":
            cube_a = {
                "code": code,
                "inputs": _parse_inputs(rp, output_id_map, CUBEMAP_PASS_INDEX),
            }
            continue

        if rp_type == "sound":
            print(f"警告: 不支持的 pass 类型 'sound'，跳过", file=sys.stderr)
            continue

    if image_code is None:
        print("错误: JSON 中没有 'image' 类型的 renderpass", file=sys.stderr)
        return False

    # ---- 确定输出目录 ----
    if output_dir:
        out_path = Path(output_dir)
    else:
        out_path = json_path.parent / json_path.stem

    if out_path.exists() and not force:
        print(f"输出目录已存在: {out_path}")
        print("使用 --force 参数覆盖，或指定其他输出目录 (-o)")
        return False

    out_path.mkdir(parents=True, exist_ok=True)

    # ---- 写入文件 ----
    files_written = []

    # image.glsl
    _write_file(out_path / "image.glsl", image_code)
    files_written.append("image.glsl")

    # buf_a~d.glsl
    for i in range(4):
        if buffer_slots[i] is not None:
            filename = f"{BUFFER_NAMES[i]}.glsl"
            _write_file(out_path / filename, buffer_slots[i]["code"])
            files_written.append(filename)

    # cube_a.glsl (Cube A pass)
    if cube_a is not None:
        _write_file(out_path / "cube_a.glsl", cube_a["code"])
        files_written.append("cube_a.glsl")

    # common.glsl
    if common_code:
        _write_file(out_path / "common.glsl", common_code)
        files_written.append("common.glsl")

    # channels.json — 生成通道绑定配置
    channels = {}
    _add_pass_channels(channels, "image", image_inputs)
    for i in range(4):
        if buffer_slots[i] is not None:
            _add_pass_channels(channels, BUFFER_NAMES[i], buffer_slots[i]["inputs"])
    if cube_a is not None:
        _add_pass_channels(channels, "cube_a", cube_a["inputs"])

    if channels:
        channels_path = out_path / "channels.json"
        with open(channels_path, "w", encoding="utf-8") as f:
            json.dump(channels, f, indent=4, ensure_ascii=False)
            f.write("\n")
        files_written.append("channels.json")

    # ---- 输出摘要 ----
    buffer_count = sum(1 for s in buffer_slots if s is not None)
    print(f"\n转换完成！")
    print(f"  项目名称: {project_name}")
    print(f"  输出目录: {out_path}")
    parts = [f"1 image", f"{buffer_count} buffer(s)"]
    if cube_a:
        parts.append("Cube A")
    if common_code:
        parts.append("common")
    print(f"  Pass 数量: {' + '.join(parts)}")
    print(f"  生成文件: {', '.join(files_written)}")

    # 显示通道绑定摘要
    if channels:
        print(f"  通道绑定:")
        for pass_name, bindings in channels.items():
            for ch_name, ch_value in bindings.items():
                print(f"    {pass_name}.{ch_name} = {ch_value}")

    return True


def _parse_inputs(rp: dict, output_id_map: dict, cubemap_pass_index: int = 10) -> list:
    """解析 renderpass 的 inputs 数组，返回 [(channel, binding_info), ...]。
    binding_info = {"type": "buffer", "buffer": "buf_a"} 
                 | {"type": "cubemap_pass", "buffer": "cube_a"}
                 | {"type": "texture", "path": "..."}
                 | None (keyboard 等忽略的类型)"""
    inputs = rp.get("inputs", [])
    if not isinstance(inputs, list):
        return []

    result = []
    for inp in inputs:
        channel = inp.get("channel", -1)
        if not isinstance(channel, int) or channel < 0 or channel >= 4:
            continue

        input_type = get_field(inp, "type", "ctype")
        input_path = get_field(inp, "filepath", "src")
        input_id = inp.get("id", "")

        if input_type == "buffer":
            # 检查是否引用了 cubemap pass 的输出
            if input_id and input_id in output_id_map and output_id_map[input_id] == cubemap_pass_index:
                result.append((channel, {"type": "buffer", "buffer": "cube_a"}))
            else:
                buf_idx = resolve_input_buffer_index(input_id, input_path, output_id_map)
                if 0 <= buf_idx <= 3:
                    result.append((channel, {"type": "buffer", "buffer": BUFFER_NAMES[buf_idx]}))
                else:
                    print(f"  警告: 无法解析 buffer 引用 (id={input_id}, path={input_path})",
                          file=sys.stderr)

        elif input_type == "texture":
            result.append((channel, {"type": "texture", "path": input_path}))

        elif input_type == "cubemap":
            result.append((channel, {"type": "texture", "path": input_path}))

        elif input_type == "keyboard":
            # 静默忽略
            pass

        elif input_type:
            print(f"  警告: 未知 input 类型 '{input_type}'，跳过", file=sys.stderr)

    return result


def _add_pass_channels(channels: dict, pass_name: str, inputs: list):
    """将 inputs 列表转换为 channels.json 的 pass 条目。"""
    if not inputs:
        return
    pass_channels = {}
    for channel, binding in inputs:
        ch_key = f"iChannel{channel}"
        if binding["type"] == "buffer":
            pass_channels[ch_key] = binding["buffer"]
        elif binding["type"] == "texture":
            pass_channels[ch_key] = binding["path"]
    if pass_channels:
        channels[pass_name] = pass_channels


def _write_file(path: Path, content: str):
    """写入文件，自动处理换行符。"""
    # JSON 中的代码通常用 \n 作为换行，直接写入
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
        # 确保文件末尾有换行
        if content and not content.endswith("\n"):
            f.write("\n")


def main():
    parser = argparse.ArgumentParser(
        description="将 ShaderToy JSON 格式的 shader 转换为目录格式",
        epilog="示例: python json2dir.py assets/shaders/a_black_hole.json -o output/black_hole"
    )
    parser.add_argument("input", help="输入的 ShaderToy JSON 文件路径")
    parser.add_argument("-o", "--output", default=None,
                        help="输出目录路径（默认: 与 JSON 同目录，以文件名为目录名）")
    parser.add_argument("--force", action="store_true",
                        help="如果输出目录已存在，强制覆盖")

    args = parser.parse_args()

    success = convert_json_to_dir(args.input, args.output, args.force)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
