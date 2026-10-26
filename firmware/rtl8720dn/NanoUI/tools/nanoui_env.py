import os
import sys

def get_nanoui_paths():
    # 脚本所在目录 (tools/)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # 根目录 (NanoUI/)
    root_dir = os.path.dirname(script_dir)
    
    # 源代码扫描目录
    source_dirs = [
        os.path.join(root_dir, 'src'), 
        os.path.join(root_dir, 'app'), 
        os.path.join(root_dir, 'app_example')
    ]
    
    # 统一输出到 Arduino 的 src/ 目录下
    output_dir = os.path.join(root_dir, 'src')
    fonts_dir = os.path.join(root_dir, 'assets', 'fonts', 'bdf')
    
    return {
        'root': root_dir,
        'sources': source_dirs,
        'output_h': os.path.join(output_dir, 'nanoui_assets.h'),
        'output_cpp': os.path.join(output_dir, 'nanoui_assets.cpp'),
        'fonts_dir': fonts_dir,
        'ttf_font': r'C:\Windows\Fonts\simhei.ttf'
    }
