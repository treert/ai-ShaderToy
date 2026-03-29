#pragma once

#include <string>

/// 打开文件选择对话框，过滤 .glsl / .json 文件
/// @return 选中的文件路径（UTF-8），取消返回空字符串
std::string OpenShaderFileDialog();

/// 打开文件夹选择对话框
/// @return 选中的文件夹路径（UTF-8），取消返回空字符串
std::string OpenShaderFolderDialog();

/// 弹出单个对话框让用户选择 shader 文件或文件夹，验证后返回路径。
/// 对话框为文件选择模式（过滤 .glsl/.json），同时提供 "Select Current Folder" 按钮
/// 用于选择当前浏览的文件夹作为 shader 目录。验证失败时弹 MessageBox 提示用户。
/// @return 验证通过的路径，取消或验证失败返回空字符串
std::string BrowseAndValidateShader();
