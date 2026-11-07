import os
import cv2
import numpy as np
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from PIL import Image, ImageTk

class OLEDMediaConverter:
    def __init__(self, root):
        self.root = root
        self.root.title("OLED Media Converter (128x64)")
        self.root.geometry("1150x750")

        # Application State
        self.filepath = ""
        self.cap = None
        self.source_frames = []       
        self.current_frame_idx = 0    
        self.roi = None               
        
        # UI/View State
        self.main_view = "original"   
        self.base_scale = 1.0
        self.current_scale = 1.0
        self.zoom_var = tk.DoubleVar(value=1.0)
        
        # Tool & Brush State
        self.tool_mode = tk.StringVar(value="roi")
        self.brush_scope = tk.StringVar(value="current") # "current" or "all"
        self.brush_size = tk.IntVar(value=15)
        
        # Per-frame Paint Layers (Lists of numpy arrays)
        self.paint_layers = []       
        self.paint_alphas = []       
        
        self.undo_stack = []          
        self.redo_stack = []
        self.last_brush_pt = None
        
        # Canvas Interaction State
        self.rect_id = None
        self.start_x = self.start_y = 0
        self.is_moving_roi = False
        self.drag_offset_x = self.drag_offset_y = 0

        # Tuning Variables
        self.fps_var = tk.IntVar(value=10)
        self.bright_var = tk.IntVar(value=0)
        self.contrast_var = tk.DoubleVar(value=1.0)
        self.thresh_var = tk.IntVar(value=127)

        self.tk_main_img = None
        self.tk_preview_img = None

        self.setup_ui()
        
        # Global Keyboard & Mouse Shortcuts
        self.root.bind("<Control-z>", lambda e: self.undo_brush())
        self.root.bind("<Control-y>", lambda e: self.redo_brush())
        
        # Global Mousewheel binding (Robust scrolling fix)
        self.root.bind_all("<MouseWheel>", self.on_mousewheel)
        self.root.bind_all("<Button-4>", self.on_mousewheel) # Linux scroll up
        self.root.bind_all("<Button-5>", self.on_mousewheel) # Linux scroll down

    def setup_ui(self):
        # 1. Left Panel (Fixed Width, Scrollable Content)
        self.left_panel = ttk.Frame(self.root, width=340)
        self.left_panel.pack(side=tk.LEFT, fill=tk.Y)
        self.left_panel.pack_propagate(False)

        ttk.Button(self.left_panel, text="Export Output Folder", command=self.process_and_export).pack(side=tk.BOTTOM, fill=tk.X, padx=10, pady=15)

        self.ctrl_canvas = tk.Canvas(self.left_panel, highlightthickness=0)
        self.ctrl_scrollbar = ttk.Scrollbar(self.left_panel, orient="vertical", command=self.ctrl_canvas.yview)
        
        self.ctrl_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.ctrl_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=2)

        self.ctrl_frame = ttk.Frame(self.ctrl_canvas)
        self.ctrl_frame.bind("<Configure>", lambda e: self.ctrl_canvas.configure(scrollregion=self.ctrl_canvas.bbox("all")))
        self.ctrl_canvas.create_window((0, 0), window=self.ctrl_frame, anchor="nw", width=310)
        self.ctrl_canvas.configure(yscrollcommand=self.ctrl_scrollbar.set)

        # --- Controls inside scrollable frame ---
        ttk.Button(self.ctrl_frame, text="1. Select Media", command=self.load_media).pack(fill=tk.X, pady=(10, 5))
        
        # Frame Timeline Navigation
        nav_frame = ttk.LabelFrame(self.ctrl_frame, text="Frame Timeline", padding=5)
        nav_frame.pack(fill=tk.X, pady=(0, 10))
        
        self.frame_lbl = ttk.Label(nav_frame, text="Frame: 0 / 0", font=("Arial", 9, "bold"))
        self.frame_lbl.pack(anchor=tk.W)
        
        self.frame_slider = ttk.Scale(nav_frame, from_=0, to=0, orient=tk.HORIZONTAL, command=self.on_frame_scrub)
        self.frame_slider.pack(fill=tk.X, pady=5)
        
        btn_row1 = ttk.Frame(nav_frame)
        btn_row1.pack(fill=tk.X, pady=(0, 5))
        ttk.Button(btn_row1, text="⏪ Prev", command=self.prev_frame).pack(side=tk.LEFT, expand=True, padx=1)
        ttk.Button(btn_row1, text="Next ⏩", command=self.next_frame).pack(side=tk.LEFT, expand=True, padx=1)
        
        btn_row2 = ttk.Frame(nav_frame)
        btn_row2.pack(fill=tk.X, pady=(0, 5))
        ttk.Button(btn_row2, text="➕ Duplicate", command=self.duplicate_frame).pack(side=tk.LEFT, expand=True, padx=1)
        ttk.Button(btn_row2, text="🗑️ Delete", command=self.delete_frame).pack(side=tk.LEFT, expand=True, padx=1)

        btn_row3 = ttk.Frame(nav_frame)
        btn_row3.pack(fill=tk.X)
        ttk.Button(btn_row3, text="⬅️ Move Frame", command=self.move_frame_backward).pack(side=tk.LEFT, expand=True, padx=1)
        ttk.Button(btn_row3, text="Move Frame ➡️", command=self.move_frame_forward).pack(side=tk.LEFT, expand=True, padx=1)

        # Tool Mode Selection
        tool_frame = ttk.LabelFrame(self.ctrl_frame, text="Tools & Views", padding=5)
        tool_frame.pack(fill=tk.X, pady=(0, 10))
        
        ttk.Radiobutton(tool_frame, text="Move / Draw ROI", variable=self.tool_mode, value="roi").grid(row=0, column=0, columnspan=2, sticky=tk.W)
        ttk.Radiobutton(tool_frame, text="Brush (Paint Black)", variable=self.tool_mode, value="brush_black").grid(row=1, column=0, columnspan=2, sticky=tk.W)
        ttk.Radiobutton(tool_frame, text="Brush (Paint White)", variable=self.tool_mode, value="brush_white").grid(row=2, column=0, columnspan=2, sticky=tk.W)
        ttk.Radiobutton(tool_frame, text="Eraser (Restore Original)", variable=self.tool_mode, value="redraw").grid(row=3, column=0, columnspan=2, sticky=tk.W)
        
        scope_frame = ttk.Frame(tool_frame)
        scope_frame.grid(row=4, column=0, columnspan=2, sticky=tk.W, pady=(5,0))
        ttk.Label(scope_frame, text="Apply Brush To:").pack(side=tk.LEFT)
        ttk.Radiobutton(scope_frame, text="Current", variable=self.brush_scope, value="current").pack(side=tk.LEFT, padx=(5,2))
        ttk.Radiobutton(scope_frame, text="All Frames", variable=self.brush_scope, value="all").pack(side=tk.LEFT)

        ttk.Label(tool_frame, text="Zoom Canvas:").grid(row=5, column=0, sticky=tk.W, pady=(5,0))
        ttk.Scale(tool_frame, from_=1.0, to=5.0, variable=self.zoom_var, orient=tk.HORIZONTAL, command=self.on_zoom).grid(row=6, column=0, columnspan=2, sticky=tk.EW)
        
        brush_frame = ttk.Frame(tool_frame)
        brush_frame.grid(row=7, column=0, columnspan=2, sticky=tk.EW, pady=(5,0))
        ttk.Label(brush_frame, text="Brush Size:").pack(side=tk.LEFT)
        ttk.Scale(brush_frame, from_=2, to=100, variable=self.brush_size, orient=tk.HORIZONTAL).pack(side=tk.LEFT, fill=tk.X, expand=True)
        
        btn_frame = ttk.Frame(tool_frame)
        btn_frame.grid(row=8, column=0, columnspan=2, sticky=tk.EW, pady=(5,0))
        ttk.Button(btn_frame, text="Undo", command=self.undo_brush, width=6).pack(side=tk.LEFT, padx=(0,2))
        ttk.Button(btn_frame, text="Redo", command=self.redo_brush, width=6).pack(side=tk.LEFT, padx=(0,2))
        ttk.Button(btn_frame, text="Clear Mask(s)", command=self.clear_brush, width=12).pack(side=tk.LEFT)

        ttk.Button(tool_frame, text="Swap Main Screen / Preview 🔁", command=self.swap_views).grid(row=9, column=0, columnspan=2, pady=10, sticky=tk.EW)

        # Image Tuning
        tune_frame = ttk.LabelFrame(self.ctrl_frame, text="Image Tuning", padding=5)
        tune_frame.pack(fill=tk.X, pady=(0, 10))
        
        ttk.Label(tune_frame, text="Video Extract FPS:").pack(anchor=tk.W)
        ttk.Scale(tune_frame, from_=1, to=17, variable=self.fps_var, orient=tk.HORIZONTAL).pack(fill=tk.X)
        ttk.Button(tune_frame, text="Apply FPS (Re-extracts video)", command=self.extract_video_frames).pack(fill=tk.X, pady=(2, 10))
        
        ttk.Label(tune_frame, text="Brightness:").pack(anchor=tk.W)
        ttk.Scale(tune_frame, from_=-100, to=100, variable=self.bright_var, orient=tk.HORIZONTAL, command=self.update_previews).pack(fill=tk.X, pady=(0, 5))
        
        ttk.Label(tune_frame, text="Contrast:").pack(anchor=tk.W)
        ttk.Scale(tune_frame, from_=0.1, to=3.0, variable=self.contrast_var, orient=tk.HORIZONTAL, command=self.update_previews).pack(fill=tk.X, pady=(0, 5))
        
        ttk.Label(tune_frame, text="Threshold:").pack(anchor=tk.W)
        ttk.Scale(tune_frame, from_=0, to=255, variable=self.thresh_var, orient=tk.HORIZONTAL, command=self.update_previews).pack(fill=tk.X, pady=(0, 5))
        
        # Live Preview Box
        ttk.Label(self.ctrl_frame, text="Thumbnail Preview (Click to Swap):").pack(anchor=tk.W)
        self.preview_lbl = tk.Label(self.ctrl_frame, background="black", cursor="hand2")
        self.preview_lbl.pack(pady=5)
        self.preview_lbl.bind("<Button-1>", lambda e: self.swap_views())

        # 2. Right Panel (Main Canvas)
        self.canvas_frame = ttk.Frame(self.root)
        self.canvas_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)
        
        self.vbar = ttk.Scrollbar(self.canvas_frame, orient=tk.VERTICAL)
        self.hbar = ttk.Scrollbar(self.canvas_frame, orient=tk.HORIZONTAL)
        
        self.canvas = tk.Canvas(self.canvas_frame, bg="#1e1e1e", cursor="crosshair",
                                xscrollcommand=self.hbar.set, yscrollcommand=self.vbar.set)
        self.vbar.config(command=self.canvas.yview)
        self.hbar.config(command=self.canvas.xview)
        
        self.vbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.hbar.pack(side=tk.BOTTOM, fill=tk.X)
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        self.canvas.bind("<ButtonPress-1>", self.on_mouse_down)
        self.canvas.bind("<B1-Motion>", self.on_mouse_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_mouse_up)
        self.canvas.bind("<Configure>", lambda e: self.update_previews() if self.source_frames else None)
        
        self.canvas_img_id = self.canvas.create_image(0, 0, anchor=tk.NW)

    def on_mousewheel(self, event):
        # Only scroll if the mouse pointer is actively hovering over the left panel
        try:
            x, y = self.root.winfo_pointerx(), self.root.winfo_pointery()
            widget = self.root.winfo_containing(x, y)
            if widget and str(self.left_panel) in str(widget):
                if event.num == 4:
                    self.ctrl_canvas.yview_scroll(-1, "units")
                elif event.num == 5:
                    self.ctrl_canvas.yview_scroll(1, "units")
                else:
                    self.ctrl_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
        except Exception:
            pass

    # --- Loading and Frame Extraction ---
    def load_media(self):
        self.filepath = filedialog.askopenfilename(filetypes=[("Media Files", "*.png *.jpg *.jpeg *.gif *.mp4 *.avi *.webm")])
        if not self.filepath: return
        
        ext = os.path.splitext(self.filepath)[1].lower()
        if ext in ['.png', '.jpg', '.jpeg']:
            frame = cv2.imread(self.filepath)
            self.source_frames = [frame]
            self.cap = None
            self.current_frame_idx = 0
            self.update_nav_ui()
            self.roi = None
            if self.rect_id: 
                self.canvas.delete(self.rect_id)
                self.rect_id = None
            self.main_view = "original"
            self.zoom_var.set(1.0)
            self.init_paint_layers()  
        else:
            self.cap = cv2.VideoCapture(self.filepath)
            self.roi = None
            if self.rect_id: 
                self.canvas.delete(self.rect_id)
                self.rect_id = None
            self.main_view = "original"
            self.zoom_var.set(1.0)
            self.extract_video_frames()

    def extract_video_frames(self):
        if self.cap is None: return
        self.cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
        source_fps = self.cap.get(cv2.CAP_PROP_FPS)
        if source_fps == 0 or np.isnan(source_fps): source_fps = 15
        
        frame_interval = max(1, int(source_fps / self.fps_var.get()))
        
        self.source_frames = []
        count = 0
        while True:
            ret, frame = self.cap.read()
            if not ret: break
            if count % frame_interval == 0:
                self.source_frames.append(frame)
            count += 1
            
        if not self.source_frames:
            messagebox.showerror("Error", "Failed to extract frames.")
            return

        self.current_frame_idx = 0
        self.update_nav_ui()
        self.init_paint_layers() 

    def init_paint_layers(self):
        if not self.source_frames: return
        ref_shape = self.source_frames[0].shape
        self.paint_layers = [np.zeros(ref_shape, dtype=np.uint8) for _ in self.source_frames]
        self.paint_alphas = [np.zeros(ref_shape[:2], dtype=np.uint8) for _ in self.source_frames]
        self.undo_stack.clear()
        self.redo_stack.clear()
        self.update_previews()

    # --- Frame Navigation & Editing ---
    def update_nav_ui(self):
        total = len(self.source_frames)
        if total == 0: return
        self.frame_lbl.config(text=f"Frame: {self.current_frame_idx + 1} / {total}")
        self.frame_slider.config(to=total - 1)
        self.frame_slider.set(self.current_frame_idx)

    def on_frame_scrub(self, val):
        if not self.source_frames: return
        idx = int(float(val))
        if idx != self.current_frame_idx and 0 <= idx < len(self.source_frames):
            self.current_frame_idx = idx
            self.update_nav_ui()
            self.update_previews()

    def prev_frame(self):
        if self.current_frame_idx > 0:
            self.current_frame_idx -= 1
            self.update_nav_ui()
            self.update_previews()

    def next_frame(self):
        if self.current_frame_idx < len(self.source_frames) - 1:
            self.current_frame_idx += 1
            self.update_nav_ui()
            self.update_previews()

    def duplicate_frame(self):
        if not self.source_frames: return
        idx = self.current_frame_idx
        self.source_frames.insert(idx + 1, self.source_frames[idx].copy())
        self.paint_layers.insert(idx + 1, self.paint_layers[idx].copy())
        self.paint_alphas.insert(idx + 1, self.paint_alphas[idx].copy())
        
        self.undo_stack.clear()
        self.redo_stack.clear()
        
        self.current_frame_idx += 1
        self.update_nav_ui()
        self.update_previews()

    def delete_frame(self):
        if not self.source_frames or len(self.source_frames) <= 1:
            messagebox.showwarning("Warning", "Cannot delete the last remaining frame.")
            return
        idx = self.current_frame_idx
        self.source_frames.pop(idx)
        self.paint_layers.pop(idx)
        self.paint_alphas.pop(idx)
        
        self.undo_stack.clear()
        self.redo_stack.clear()
        
        if self.current_frame_idx >= len(self.source_frames):
            self.current_frame_idx = len(self.source_frames) - 1
        self.update_nav_ui()
        self.update_previews()

    def move_frame_backward(self):
        if not self.source_frames or self.current_frame_idx == 0: return
        idx = self.current_frame_idx
        
        # Swap images and paint layers
        self.source_frames[idx], self.source_frames[idx-1] = self.source_frames[idx-1], self.source_frames[idx]
        self.paint_layers[idx], self.paint_layers[idx-1] = self.paint_layers[idx-1], self.paint_layers[idx]
        self.paint_alphas[idx], self.paint_alphas[idx-1] = self.paint_alphas[idx-1], self.paint_alphas[idx]
        
        self.undo_stack.clear()
        self.redo_stack.clear()
        
        self.current_frame_idx -= 1
        self.update_nav_ui()
        self.update_previews()

    def move_frame_forward(self):
        if not self.source_frames or self.current_frame_idx >= len(self.source_frames) - 1: return
        idx = self.current_frame_idx
        
        # Swap images and paint layers
        self.source_frames[idx], self.source_frames[idx+1] = self.source_frames[idx+1], self.source_frames[idx]
        self.paint_layers[idx], self.paint_layers[idx+1] = self.paint_layers[idx+1], self.paint_layers[idx]
        self.paint_alphas[idx], self.paint_alphas[idx+1] = self.paint_alphas[idx+1], self.paint_alphas[idx]
        
        self.undo_stack.clear()
        self.redo_stack.clear()
        
        self.current_frame_idx += 1
        self.update_nav_ui()
        self.update_previews()

    # --- View Swapping & Brushing ---
    def clear_brush(self):
        if not self.source_frames: return
        ref_shape = self.source_frames[0].shape
        
        if self.brush_scope.get() == "all":
            self.paint_layers = [np.zeros(ref_shape, dtype=np.uint8) for _ in self.source_frames]
            self.paint_alphas = [np.zeros(ref_shape[:2], dtype=np.uint8) for _ in self.source_frames]
            self.undo_stack.clear()
            self.redo_stack.clear()
        else:
            idx = self.current_frame_idx
            if idx < len(self.paint_layers):
                self.undo_stack.append(("single", idx, self.paint_layers[idx].copy(), self.paint_alphas[idx].copy()))
                self.redo_stack.clear()
                self.paint_layers[idx] = np.zeros(ref_shape, dtype=np.uint8)
                self.paint_alphas[idx] = np.zeros(ref_shape[:2], dtype=np.uint8)
            
        self.update_previews()

    def swap_views(self):
        if not self.source_frames: return
        if self.roi is None and self.main_view == "original":
            messagebox.showinfo("Info", "Please draw an ROI first before switching views.")
            return
            
        self.main_view = "output" if self.main_view == "original" else "original"
        
        if self.rect_id:
            state = 'normal' if self.main_view == "original" else 'hidden'
            self.canvas.itemconfigure(self.rect_id, state=state)
            
        self.zoom_var.set(1.0)
        self.update_previews()

    def on_zoom(self, *args):
        self.update_previews()

    def get_img_coords(self, event):
        cx = self.canvas.canvasx(event.x)
        cy = self.canvas.canvasy(event.y)
        
        if self.main_view == "original":
            return int(cx / self.current_scale), int(cy / self.current_scale)
        else:
            out_x = cx / self.current_scale
            out_y = cy / self.current_scale
            roi_x, roi_y, roi_w, roi_h = self.roi
            orig_x = int(roi_x + out_x * (roi_w / 128.0))
            orig_y = int(roi_y + out_y * (roi_h / 64.0))
            return orig_x, orig_y

    def on_mouse_down(self, event):
        if not self.source_frames: return
        cx = self.canvas.canvasx(event.x)
        cy = self.canvas.canvasy(event.y)

        if self.tool_mode.get() == "roi" and self.main_view == "original":
            if self.roi and self.rect_id:
                sx, sy = self.roi[0] * self.current_scale, self.roi[1] * self.current_scale
                sw, sh = self.roi[2] * self.current_scale, self.roi[3] * self.current_scale
                if sx <= cx <= sx + sw and sy <= cy <= sy + sh:
                    self.is_moving_roi = True
                    self.drag_offset_x = cx - sx
                    self.drag_offset_y = cy - sy
                    return
            
            self.is_moving_roi = False
            self.start_x, self.start_y = cx, cy
            if self.rect_id: self.canvas.delete(self.rect_id)
            self.rect_id = self.canvas.create_rectangle(cx, cy, cx, cy, outline="#00ff00", width=2)
            
        elif self.tool_mode.get() in ["brush_black", "brush_white", "redraw"]:
            scope = self.brush_scope.get()
            if scope == "all":
                saved_state = ("all", None, [l.copy() for l in self.paint_layers], [a.copy() for a in self.paint_alphas])
            else:
                idx = self.current_frame_idx
                saved_state = ("single", idx, self.paint_layers[idx].copy(), self.paint_alphas[idx].copy())
            
            self.undo_stack.append(saved_state)
            if len(self.undo_stack) > 10: self.undo_stack.pop(0)
            self.redo_stack.clear()
            
            self.last_brush_pt = self.get_img_coords(event)
            self.apply_brush(event)

    def on_mouse_drag(self, event):
        if not self.source_frames: return
        cx = self.canvas.canvasx(event.x)
        cy = self.canvas.canvasy(event.y)

        if self.tool_mode.get() == "roi" and self.main_view == "original":
            if self.is_moving_roi:
                new_sx = cx - self.drag_offset_x
                new_sy = cy - self.drag_offset_y
                sw, sh = self.roi[2] * self.current_scale, self.roi[3] * self.current_scale
                self.canvas.coords(self.rect_id, new_sx, new_sy, new_sx + sw, new_sy + sh)
            else:
                w = abs(cx - self.start_x)
                h = w / 2 
                x2 = self.start_x + (w if cx > self.start_x else -w)
                y2 = self.start_y + (h if cy > self.start_y else -h)
                self.canvas.coords(self.rect_id, self.start_x, self.start_y, x2, y2)
                
        elif self.tool_mode.get() in ["brush_black", "brush_white", "redraw"]:
            self.apply_brush(event)

    def on_mouse_up(self, event):
        if not self.source_frames: return
        
        if self.tool_mode.get() == "roi" and self.main_view == "original":
            if self.rect_id:
                coords = self.canvas.coords(self.rect_id)
                if coords:
                    x1, y1, x2, y2 = coords
                    self.roi = (
                        int(min(x1, x2) / self.current_scale),
                        int(min(y1, y2) / self.current_scale),
                        int(abs(x2 - x1) / self.current_scale),
                        int(abs(y2 - y1) / self.current_scale)
                    )
                    self.update_previews()
                    
        elif self.tool_mode.get() in ["brush_black", "brush_white", "redraw"]:
            self.last_brush_pt = None

    def apply_brush(self, event):
        if not self.paint_layers: return
        curr_pt = self.get_img_coords(event)
        
        if self.main_view == "original":
            r = int(self.brush_size.get() / self.current_scale)
        else:
            r = int((self.brush_size.get() / self.current_scale) * (self.roi[2] / 128.0))
        r = max(1, r)

        mode = self.tool_mode.get()
        if mode == "brush_black":
            color, alpha = (0, 0, 0), 255
        elif mode == "brush_white":
            color, alpha = (255, 255, 255), 255
        else: 
            color, alpha = (0, 0, 0), 0

        scope = self.brush_scope.get()
        indices = range(len(self.source_frames)) if scope == "all" else [self.current_frame_idx]

        for i in indices:
            layer = self.paint_layers[i]
            p_alpha = self.paint_alphas[i]

            if self.last_brush_pt is not None:
                if mode != "redraw":
                    cv2.line(layer, self.last_brush_pt, curr_pt, color, thickness=r*2, lineType=cv2.LINE_AA)
                cv2.line(p_alpha, self.last_brush_pt, curr_pt, alpha, thickness=r*2, lineType=cv2.LINE_AA)
                
            if mode != "redraw":
                cv2.circle(layer, curr_pt, r, color, -1, lineType=cv2.LINE_AA)
            cv2.circle(p_alpha, curr_pt, r, alpha, -1, lineType=cv2.LINE_AA)
        
        self.last_brush_pt = curr_pt
        self.update_previews()

    def undo_brush(self):
        if self.undo_stack:
            state = self.undo_stack.pop()
            
            if state[0] == "all":
                self.redo_stack.append(("all", None, [l.copy() for l in self.paint_layers], [a.copy() for a in self.paint_alphas]))
                self.paint_layers = state[2]
                self.paint_alphas = state[3]
            else:
                idx = state[1]
                self.redo_stack.append(("single", idx, self.paint_layers[idx].copy(), self.paint_alphas[idx].copy()))
                self.paint_layers[idx] = state[2]
                self.paint_alphas[idx] = state[3]
                
            self.update_previews()

    def redo_brush(self):
        if self.redo_stack:
            state = self.redo_stack.pop()
            
            if state[0] == "all":
                self.undo_stack.append(("all", None, [l.copy() for l in self.paint_layers], [a.copy() for a in self.paint_alphas]))
                self.paint_layers = state[2]
                self.paint_alphas = state[3]
            else:
                idx = state[1]
                self.undo_stack.append(("single", idx, self.paint_layers[idx].copy(), self.paint_alphas[idx].copy()))
                self.paint_layers[idx] = state[2]
                self.paint_alphas[idx] = state[3]
                
            self.update_previews()

    # --- Processing & Rendering ---
    def get_processed_output(self, frame_bgr, layer, alpha):
        alpha_3d = alpha[:, :, np.newaxis] / 255.0
        composite_bgr = (frame_bgr * (1 - alpha_3d) + layer * alpha_3d).astype(np.uint8)
        
        x, y, w, h = self.roi
        max_h, max_w = composite_bgr.shape[:2]
        x1, y1 = max(0, x), max(0, y)
        x2, y2 = min(max_w, x + w), min(max_h, y + h)
        
        crop = composite_bgr[y1:y2, x1:x2]
        if crop.size == 0: return np.zeros((64, 128), dtype=np.uint8)
            
        resized = cv2.resize(crop, (128, 64))
        gray = cv2.cvtColor(resized, cv2.COLOR_BGR2GRAY)
        adjusted = cv2.convertScaleAbs(gray, alpha=self.contrast_var.get(), beta=self.bright_var.get())
        _, mono = cv2.threshold(adjusted, self.thresh_var.get(), 255, cv2.THRESH_BINARY)
        return mono

    def update_previews(self, *args):
        if not self.source_frames: return
        self.root.update_idletasks()
        
        idx = self.current_frame_idx
        current_bgr = self.source_frames[idx]
        current_layer = self.paint_layers[idx]
        current_alpha = self.paint_alphas[idx]
        
        alpha_3d = current_alpha[:, :, np.newaxis] / 255.0
        composite_bgr = (current_bgr * (1 - alpha_3d) + current_layer * alpha_3d).astype(np.uint8)
        img_original = cv2.cvtColor(composite_bgr, cv2.COLOR_BGR2RGB)
        
        img_output = None
        if self.roi and self.roi[2] > 0 and self.roi[3] > 0:
            mono = self.get_processed_output(current_bgr, current_layer, current_alpha)
            img_output = cv2.cvtColor(mono, cv2.COLOR_GRAY2RGB)

        cw, ch = self.canvas.winfo_width(), self.canvas.winfo_height()
        if cw <= 1: cw, ch = 700, 600

        if self.main_view == "original":
            h, w = img_original.shape[:2]
            self.base_scale = min(cw / w, ch / h)
            self.current_scale = self.base_scale * self.zoom_var.get()
            
            new_w, new_h = max(1, int(w * self.current_scale)), max(1, int(h * self.current_scale))
            main_disp = cv2.resize(img_original, (new_w, new_h))
            
            if img_output is not None:
                thumb_scale = 280 / 128.0
                thumb_disp = cv2.resize(img_output, (280, int(64 * thumb_scale)), interpolation=cv2.INTER_NEAREST)
            else:
                thumb_disp = np.zeros((140, 280, 3), dtype=np.uint8)
                
        else:
            if img_output is None: return
            h, w = 64, 128
            self.base_scale = min(cw / w, ch / h)
            self.current_scale = self.base_scale * self.zoom_var.get()
            
            new_w, new_h = max(1, int(w * self.current_scale)), max(1, int(h * self.current_scale))
            main_disp = cv2.resize(img_output, (new_w, new_h), interpolation=cv2.INTER_NEAREST)
            
            th, tw = img_original.shape[:2]
            thumb_scale = 280 / tw
            thumb_disp = cv2.resize(img_original, (280, int(th * thumb_scale)))

        self.tk_main_img = ImageTk.PhotoImage(image=Image.fromarray(main_disp))
        self.canvas.itemconfig(self.canvas_img_id, image=self.tk_main_img)
        self.canvas.config(scrollregion=self.canvas.bbox(tk.ALL))
        
        self.tk_preview_img = ImageTk.PhotoImage(image=Image.fromarray(thumb_disp))
        self.preview_lbl.config(image=self.tk_preview_img)
        
        if self.main_view == "original" and self.roi and self.rect_id:
            x, y, rw, rh = self.roi
            sx, sy = x * self.current_scale, y * self.current_scale
            sw, sh = rw * self.current_scale, rh * self.current_scale
            self.canvas.coords(self.rect_id, sx, sy, sx + sw, sy + sh)
            self.canvas.lift(self.rect_id)

    # --- Export Processing ---
    def process_and_export(self):
        if not self.filepath or not self.roi or not self.source_frames:
            messagebox.showwarning("Warning", "Please load media and select an ROI.")
            return
            
        filename = os.path.splitext(os.path.basename(self.filepath))[0]
        out_dir = os.path.join(os.path.dirname(self.filepath), filename)
        os.makedirs(out_dir, exist_ok=True)
        
        frames = []
        for idx, bgr_frame in enumerate(self.source_frames):
            layer = self.paint_layers[idx]
            alpha = self.paint_alphas[idx]
            mono = self.get_processed_output(bgr_frame, layer, alpha)
            frames.append(mono)
            cv2.imwrite(os.path.join(out_dir, f"frame_{idx:04d}.png"), mono)
                
        if not frames: return
        
        spritesheet = cv2.vconcat(frames)
        cv2.imwrite(os.path.join(out_dir, "spritesheet.png"), spritesheet)
        self.export_c_header(frames, filename, out_dir)
        messagebox.showinfo("Success", f"Exported {len(frames)} frame(s) to:\n{out_dir}")

    def export_c_header(self, frames, filename, out_dir):
        c_code = [f"#include <pgmspace.h>\n\n// {filename} - {len(frames)} frames (128x64)\n"]
        array_names = []
        
        for idx, frame in enumerate(frames):
            name = f"{filename}_frame_{idx}"
            array_names.append(name)
            c_code.append(f"const unsigned char {name}[] PROGMEM = {{")
            
            height, width = frame.shape
            for y in range(height):
                row = []
                for x_byte in range(width // 8):
                    byte_val = 0
                    for bit in range(8):
                        if frame[y, x_byte * 8 + bit] > 127:
                            byte_val |= (1 << (7 - bit))
                    row.append(f"0x{byte_val:02X}")
                c_code.append("  " + ", ".join(row) + ",")
            c_code.append("};\n")
            
        c_code.append(f"const unsigned char* const {filename}_frames[] PROGMEM = {{")
        c_code.append("  " + ", ".join(array_names))
        c_code.append("};")
        
        with open(os.path.join(out_dir, f"{filename}.h"), "w") as f:
            f.write("\n".join(c_code))

if __name__ == "__main__":
    root = tk.Tk()
    app = OLEDMediaConverter(root)
    root.mainloop()