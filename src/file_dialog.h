#pragma once

#include <string>

/// 打开文件选择对话框，过滤 .glsl / .json 文件
/// @return 选中的文件路径（UTF-8），取消返回空字符串
std::string OpenShaderFileDialog();

/// 打开文件夹选择对话框
/// @return 选中的文件夹路径（UTF-8），取消返回空字符串
std::string OpenShaderFolderDialog();

/// 弹出对话框让用户选择 shader 文件或文件夹，验证后返回路径。
/// 流程：先弹文件选择对话框（.glsl/.json），用户取消后自动弹文件夹选择对话框。
/// 验证失败时弹 MessageBox 提示用户。
/// @return 验证通过的路径，取消或验证失败返回空字符串
std::string BrowseAndValidateShader();
