"""Original media editor using the common project/output boundary."""

import math
import struct
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import time
import re
from PIL import Image, ImageTk, ImageDraw, ImageOps

try:
    import cv2
except ImportError:
    cv2 = None
from .media import pack_bitmap, export_media


class AssetBuilderApp:
    def __init__(self, root, config):
        self.root = root
        self.config = config
        self.root.title("NanoUI Asset Builder Pro 3.4 - 动效与色彩版")
        self.root.geometry("1150x850")
        self.root.configure(bg="#121212")

        self.is_destroyed = False
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

        # 媒体状态
        self.source_path = None
        self.source_img = None
        self.cap = None
        self.total_frames = 1
        self.fps_src = 30.0
        self.current_frame_idx = 0
        self.is_playing = False

        self.crop_rect = [0, 0, 128, 64]
        self.drag_mode = None
        self.drag_start = (0, 0)
        self.sw = 100

        # UI 变量
        self.target_w = tk.IntVar(value=128)
        self.target_h = tk.IntVar(value=64)
        self.target_fps = tk.IntVar(value=15)
        self.start_frame = tk.IntVar(value=0)
        self.end_frame = tk.IntVar(value=0)
        self.enable_dither = tk.BooleanVar(value=True)
        self.enable_invert = tk.BooleanVar(value=False)
        self.asset_name = tk.StringVar(value="my_media")

        self._build_ui()
        self._update_loop()

    def on_close(self):
        self.is_destroyed = True
        if self.cap:
            self.cap.release()
        self.root.destroy()

    def _build_ui(self):
        left_frame = tk.Frame(self.root, bg="#121212")
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=15, pady=10)

        header_f = tk.Frame(left_frame, bg="#121212")
        header_f.pack(fill=tk.X)
        tk.Label(
            header_f, text="🎬 媒体舞台", bg="#121212", fg="#00FFAA", font=("微软雅黑", 14, "bold")
        ).pack(side=tk.LEFT)
        self.lbl_time = tk.Label(
            header_f,
            text="00:00.000 / 00:00.000",
            bg="#121212",
            fg="#888888",
            font=("Consolas", 10),
        )
        self.lbl_time.pack(side=tk.RIGHT)

        self.canvas = tk.Canvas(
            left_frame, bg="#000000", highlightthickness=1, highlightbackground="#333333"
        )
        self.canvas.pack(fill=tk.BOTH, expand=True, pady=10)
        self.canvas.bind("<ButtonPress-1>", self.on_mouse_down)
        self.canvas.bind("<B1-Motion>", self.on_mouse_move)
        self.canvas.bind("<ButtonRelease-1>", self.on_mouse_up)

        ctrl_f = tk.Frame(left_frame, bg="#121212")
        ctrl_f.pack(fill=tk.X)
        self.btn_play = tk.Button(
            ctrl_f, text="▶ 播放", width=8, bg="#333333", fg="white", command=self.toggle_play
        )
        self.btn_play.pack(side=tk.LEFT)
        self.slider_frame = tk.Scale(
            ctrl_f,
            from_=0,
            to=100,
            orient=tk.HORIZONTAL,
            bg="#121212",
            fg="#00FFAA",
            troughcolor="#222222",
            highlightthickness=0,
            command=self.manual_seek,
        )
        self.slider_frame.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=10)

        right_frame = tk.Frame(self.root, bg="#1E1E1E", width=380)
        right_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=10, pady=10)

        tk.Label(
            right_frame,
            text="⚙ 配置面板",
            bg="#1E1E1E",
            fg="#00FFAA",
            font=("微软雅黑", 12, "bold"),
        ).pack(anchor=tk.W, pady=10)
        tk.Label(right_frame, text="资产变量名:", bg="#1E1E1E", fg="white").pack(anchor=tk.W)
        tk.Entry(
            right_frame,
            textvariable=self.asset_name,
            width=35,
            bg="#000000",
            fg="#00FFAA",
            insertbackground="white",
        ).pack(anchor=tk.W, pady=5)

        cfg_f = tk.Frame(right_frame, bg="#1E1E1E")
        cfg_f.pack(fill=tk.X, pady=10)
        labels = ["目标宽度:", "目标高度:", "播放FPS:", "开始帧:", "结束帧:"]
        vars = [self.target_w, self.target_h, self.target_fps, self.start_frame, self.end_frame]
        for i, (label, var) in enumerate(zip(labels, vars)):
            tk.Label(cfg_f, text=label, bg="#1E1E1E", fg="white").grid(
                row=i, column=0, sticky=tk.W, pady=3
            )
            tk.Entry(cfg_f, textvariable=var, width=12).grid(
                row=i, column=1, pady=3, padx=10, sticky=tk.W
            )

        trim_btn_f = tk.Frame(right_frame, bg="#1E1E1E")
        trim_btn_f.pack(fill=tk.X)
        tk.Button(
            trim_btn_f,
            text="将当前设为起点",
            font=("微软雅黑", 8),
            command=lambda: self.start_frame.set(self.current_frame_idx),
        ).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2)
        tk.Button(
            trim_btn_f,
            text="将当前设为终点",
            font=("微软雅黑", 8),
            command=lambda: self.end_frame.set(self.current_frame_idx),
        ).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2)

        tk.Checkbutton(
            right_frame,
            text="开启抖动 (Dither)",
            variable=self.enable_dither,
            bg="#1E1E1E",
            fg="white",
            selectcolor="#000000",
            command=self.update_preview,
        ).pack(anchor=tk.W, pady=(10, 0))
        tk.Checkbutton(
            right_frame,
            text="反转颜色 (Invert)",
            variable=self.enable_invert,
            bg="#1E1E1E",
            fg="white",
            selectcolor="#000000",
            command=self.update_preview,
        ).pack(anchor=tk.W, pady=(0, 10))

        self.lbl_size_est = tk.Label(
            right_frame,
            text="预估占用: 0 KB",
            bg="#1E1E1E",
            fg="#00FFAA",
            font=("Consolas", 11, "bold"),
        )
        self.lbl_size_est.pack(anchor=tk.W, pady=(0, 10))

        self.preview_canvas = tk.Canvas(
            right_frame,
            width=256,
            height=128,
            bg="black",
            highlightthickness=1,
            highlightbackground="#00FFAA",
        )
        self.preview_canvas.pack(pady=5)

        tk.Button(
            right_frame,
            text="📁 导入媒体",
            bg="#007ACC",
            fg="white",
            font=("微软雅黑", 10, "bold"),
            height=2,
            command=self.load_media,
        ).pack(fill=tk.X, pady=20)

        export_f = tk.Frame(right_frame, bg="#1E1E1E")
        export_f.pack(fill=tk.X, side=tk.BOTTOM, pady=30)
        tk.Button(
            export_f,
            text="🚀 统一导出至 nanoui_assets",
            bg="#E67E22",
            fg="white",
            font=("微软雅黑", 11, "bold"),
            height=2,
            command=self.auto_export,
        ).pack(fill=tk.X)
        tk.Label(
            export_f,
            text=f"头文件: {self.config.output_h}",
            bg="#1E1E1E",
            fg="#555555",
            font=("微软雅黑", 8),
        ).pack(pady=2)
        tk.Label(
            export_f,
            text=f"源文件: {self.config.output_cpp}",
            bg="#1E1E1E",
            fg="#555555",
            font=("微软雅黑", 8),
        ).pack()

        self.target_w.trace_add("write", self._on_size_changed)
        self.target_h.trace_add("write", self._on_size_changed)
        self.target_fps.trace_add("write", lambda *args: self._update_size_estimate())
        self.start_frame.trace_add("write", lambda *args: self._update_size_estimate())
        self.end_frame.trace_add("write", lambda *args: self._update_size_estimate())

    def _on_size_changed(self, *args):
        self._update_size_estimate()
        if self.source_img:
            self.reset_crop_box()
            self.update_preview()

    def _update_size_estimate(self):
        try:
            w = self.target_w.get()
            h = self.target_h.get()
            t_fps = self.target_fps.get()
            start = self.start_frame.get()
            end = self.end_frame.get()

            if w <= 0 or h <= 0 or start > end:
                self.lbl_size_est.config(text="预估占用: 参数错误", fg="#FF4444")
                return

            if self.cap:
                step = max(1, round(self.fps_src / t_fps))
                frames = len(range(start, end + 1, step))
            else:
                frames = 1

            bytes_per_frame = w * math.ceil(h / 8.0)
            total_bytes = 12 + (bytes_per_frame * frames)

            kb = total_bytes / 1024.0
            if kb > 100:
                color = "#FF4444"
            elif kb > 50:
                color = "#FFDD00"
            else:
                color = "#00FFAA"

            if kb > 1024:
                self.lbl_size_est.config(text=f"预估: {kb/1024.0:.2f} MB (太大!)", fg=color)
            else:
                self.lbl_size_est.config(text=f"预估占用: {kb:.1f} KB", fg=color)
        except Exception:
            pass

    def _format_time(self, frame_idx):
        ms = (frame_idx / self.fps_src) * 1000
        mins = int(ms // 60000)
        secs = int((ms % 60000) // 1000)
        milli = int(ms % 1000)
        return f"{mins:02d}:{secs:02d}.{milli:03d}"

    def toggle_play(self):
        if not self.cap:
            return
        self.is_playing = not self.is_playing
        self.btn_play.config(text="⏸ 暂停" if self.is_playing else "▶ 播放")

    def _update_loop(self):
        if self.is_destroyed:
            return
        if self.is_playing and self.cap:
            self.current_frame_idx += 1
            if self.current_frame_idx >= self.total_frames:
                self.current_frame_idx = 0
            self.slider_frame.set(self.current_frame_idx)
            self.render_frame_at(self.current_frame_idx)
            delay = int(1000 / self.fps_src)
        else:
            delay = 100
        self.root.after(delay, self._update_loop)

    def load_media(self):
        path = filedialog.askopenfilename()
        if not path:
            return
        self.source_path = path
        self.is_playing = False
        self.btn_play.config(text="▶ 播放")
        if path.lower().endswith((".mp4", ".avi", ".mov")):
            if cv2 is None:
                messagebox.showerror("无法导入视频", "视频需要 OpenCV；静态图片只需要 Pillow。")
                return
            if self.cap:
                self.cap.release()
            self.cap = cv2.VideoCapture(path)
            self.total_frames = int(self.cap.get(cv2.CAP_PROP_FRAME_COUNT))
            self.fps_src = self.cap.get(cv2.CAP_PROP_FPS) or 30.0
            self.slider_frame.configure(to=self.total_frames - 1)
            self.start_frame.set(0)
            self.end_frame.set(self.total_frames - 1)
            self.render_frame_at(0)
        else:
            if self.cap:
                self.cap.release()
                self.cap = None
            self.source_img = Image.open(path).convert("RGB")
            self.total_frames = 1
            self.slider_frame.configure(to=0)
            self.render_frame_at(0)
        self.reset_crop_box()

    def manual_seek(self, val):
        if self.is_playing:
            return
        self.current_frame_idx = int(val)
        self.render_frame_at(self.current_frame_idx)

    def render_frame_at(self, idx):
        if self.is_destroyed:
            return
        if self.cap:
            self.cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
            ret, frame = self.cap.read()
            if ret:
                self.source_img = Image.fromarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
        if not self.source_img:
            return
        try:
            self.canvas.update()
            cw, ch = self.canvas.winfo_width(), self.canvas.winfo_height()
            if cw < 10:
                return
            r_img = self.source_img.width / self.source_img.height
            sw, sh = (cw, int(cw / r_img)) if r_img > (cw / ch) else (int(ch * r_img), ch)
            self.sw, self.sh = sw, sh
            self.sx, self.sy = (cw - sw) // 2, (ch - sh) // 2
            disp_img = self.source_img.resize((sw, sh), Image.Resampling.LANCZOS)
            self.tk_img = ImageTk.PhotoImage(disp_img)
            self.canvas.delete("bg")
            self.canvas.create_image(self.sx, self.sy, anchor=tk.NW, image=self.tk_img, tags="bg")
            self.lbl_time.config(
                text=f"{self._format_time(idx)} / {self._format_time(self.total_frames)}"
            )
            self.draw_crop_ui()
            self.update_preview()
        except:
            pass

    def reset_crop_box(self):
        try:
            tw, th = self.target_w.get(), self.target_h.get()
            if th <= 0 or tw <= 0:
                return
            ratio = tw / th
            bw = self.sw * 0.6
            bh = bw / ratio
            self.crop_rect = [self.sx + (self.sw - bw) / 2, self.sy + (self.sh - bh) / 2, bw, bh]
            self.draw_crop_ui()
        except Exception:
            pass

    def draw_crop_ui(self):
        self.canvas.delete("ui")
        x, y, w, h = self.crop_rect
        self.canvas.create_rectangle(x, y, x + w, y + h, outline="#00FFAA", width=2, tags="ui")
        self.canvas.create_rectangle(
            x + w - 12, y + h - 12, x + w, y + h, fill="#00FFAA", tags="ui"
        )

    def on_mouse_down(self, event):
        x, y, w, h = self.crop_rect
        if x + w - 20 < event.x < x + w + 10 and y + h - 20 < event.y < y + h + 10:
            self.drag_mode = "resize"
        elif x < event.x < x + w and y < event.y < y + h:
            self.drag_mode = "move"
        else:
            self.drag_mode = None
        self.drag_start = (event.x, event.y)

    def on_mouse_move(self, event):
        if not self.drag_mode:
            return
        dx, dy = event.x - self.drag_start[0], event.y - self.drag_start[1]
        if self.drag_mode == "move":
            self.crop_rect[0] += dx
            self.crop_rect[1] += dy
        elif self.drag_mode == "resize":
            ratio = self.target_w.get() / self.target_h.get()
            self.crop_rect[2] += dx
            self.crop_rect[3] = self.crop_rect[2] / ratio
        self.drag_start = (event.x, event.y)
        self.draw_crop_ui()
        self.update_preview()

    def on_mouse_up(self, event):
        self.drag_mode = None

    def process_pil_frame(self, pil_img):
        img_w, img_h = pil_img.size
        rx, ry = (self.crop_rect[0] - self.sx) / self.sw, (self.crop_rect[1] - self.sy) / self.sh
        rw, rh = self.crop_rect[2] / self.sw, self.crop_rect[3] / self.sh
        box = (
            max(0, int(rx * img_w)),
            max(0, int(ry * img_h)),
            min(img_w, int((rx + rw) * img_w)),
            min(img_h, int((ry + rh) * img_h)),
        )
        bw = (
            pil_img.crop(box)
            .resize((self.target_w.get(), self.target_h.get()), Image.Resampling.LANCZOS)
            .convert("L")
        )

        if self.enable_invert.get():
            bw = ImageOps.invert(bw)

        return bw.convert("1", dither=Image.FLOYDSTEINBERG if self.enable_dither.get() else 0)

    def update_preview(self):
        if self.is_destroyed or not self.source_img:
            return
        try:
            tw, th = self.target_w.get(), self.target_h.get()
            if tw <= 0 or th <= 0:
                return

            bw = self.process_pil_frame(self.source_img)

            preview_w = 256
            preview_h = int(preview_w * (th / tw))

            self.preview_canvas.config(width=preview_w, height=preview_h)
            disp_preview = bw.resize((preview_w, preview_h), Image.Resampling.NEAREST)
            self.tk_preview = ImageTk.PhotoImage(disp_preview)
            self.preview_canvas.delete("all")
            self.preview_canvas.create_image(0, 0, anchor=tk.NW, image=self.tk_preview)
        except Exception as e:
            pass

    def get_frame_bytes_fast(self, pil_img):
        return pack_bitmap(pil_img)

    def auto_export(self):
        try:
            self._export()
        except Exception as error:
            messagebox.showerror("导出失败", str(error))

    def _export(self):
        if not self.source_img:
            return
        tw, th = self.target_w.get(), self.target_h.get()
        t_fps, name = self.target_fps.get(), self.asset_name.get()
        start, end = self.start_frame.get(), self.end_frame.get()

        if tw <= 0 or th <= 0 or t_fps <= 0 or start < 0 or end < start:
            raise ValueError("宽高、FPS 必须大于零；帧范围必须有效。")
        frames_bytes = []
        if self.cap:
            step = max(1, round(self.fps_src / t_fps))
            for i in range(start, end + 1, step):
                self.cap.set(cv2.CAP_PROP_POS_FRAMES, i)
                ret, frame = self.cap.read()
                if not ret:
                    break
                img = Image.fromarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
                frames_bytes.append(self.get_frame_bytes_fast(self.process_pil_frame(img)))
        else:
            frames_bytes.append(self.get_frame_bytes_fast(self.process_pil_frame(self.source_img)))

        size = export_media(self.config, name, tw, th, t_fps, frames_bytes)
        messagebox.showinfo("完成", f"资产 {name} 已导出：{size} 字节\n{self.config.output_cpp}")
