import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import socket
import json
import threading
import requests
import time
import subprocess
import webbrowser
import os
from datetime import datetime

class ESP8266Manager:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP8266 STC-ISP 管理器")
        self.root.geometry("1000x700")  # 增加窗口尺寸
        self.root.minsize(950, 650)    # 设置最小尺寸
        self.root.resizable(True, True)
        
        # 设备列表，用字典存储，键为设备ID
        self.devices = {}
        
        # 当前选中的设备
        self.selected_device = None
        self.device_api_info = None  # 存储API返回的详细信息
        
        # 标记用户是否正在编辑串口设置
        self.editing_serial = False
        
        # 程序配置文件路径
        self.config_dir = os.path.join(os.path.expanduser("~"), ".esp8266_manager")
        self.config_file = os.path.join(self.config_dir, "config.json")
        
        # 创建UDP监听线程
        self.udp_running = True
        self.udp_thread = threading.Thread(target=self.udp_listener)
        self.udp_thread.daemon = True
        
        # 创建UI
        self.create_ui()
        
        # 检查是否是首次使用
        self.check_first_use()
        
        # 启动UDP监听
        self.udp_thread.start()
        
    def create_ui(self):
        # 创建主框架，这个框架支持滚动
        self.canvas = tk.Canvas(self.root)
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        # 添加滚动条
        scrollbar = ttk.Scrollbar(self.root, orient="vertical", command=self.canvas.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        # 配置Canvas
        self.canvas.configure(yscrollcommand=scrollbar.set)
        self.canvas.bind('<Configure>', lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        
        # 创建主框架，放在Canvas中
        main_frame = ttk.Frame(self.canvas, padding="5")
        self.canvas.create_window((0, 0), window=main_frame, anchor="nw")
        
        # 绑定鼠标滚轮到Canvas
        self.root.bind("<MouseWheel>", lambda event: self.canvas.yview_scroll(int(-1*(event.delta/120)), "units"))
        self.root.bind("<Button-4>", lambda event: self.canvas.yview_scroll(-1, "units"))
        self.root.bind("<Button-5>", lambda event: self.canvas.yview_scroll(1, "units"))
        
        # 左侧设备列表框架
        left_frame = ttk.LabelFrame(main_frame, text="发现的设备", padding="5")
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=False, padx=2, pady=2)
        
        # 设备列表
        self.device_tree = ttk.Treeview(left_frame, columns=("ID", "IP"), show="headings", height=15)
        self.device_tree.heading("ID", text="设备ID")
        self.device_tree.heading("IP", text="IP地址")
        self.device_tree.column("ID", width=150)
        self.device_tree.column("IP", width=120)
        self.device_tree.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)
        self.device_tree.bind("<<TreeviewSelect>>", self.on_device_select)
        
        # 滚动条
        scrollbar = ttk.Scrollbar(left_frame, orient="vertical", command=self.device_tree.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.device_tree.configure(yscrollcommand=scrollbar.set)
        
        # 按钮框架
        button_frame = ttk.Frame(left_frame)
        button_frame.pack(fill=tk.X, padx=2, pady=2)
        
        # 刷新按钮
        refresh_button = ttk.Button(button_frame, text="刷新设备列表", command=self.refresh_devices)
        refresh_button.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1, pady=2)
        
        # 连接AP向导按钮
        ap_wizard_button = ttk.Button(button_frame, text="连接设备AP向导", command=self.show_ap_wizard)
        ap_wizard_button.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=1, pady=2)
        
        # 右侧详细信息和配置框架
        right_frame = ttk.Frame(main_frame, padding="5")
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=2, pady=2)
        
        # 设备详情框架
        details_frame = ttk.LabelFrame(right_frame, text="设备详情", padding="5")
        details_frame.pack(fill=tk.BOTH, expand=False, padx=2, pady=2)
        
        # 设备信息文本框
        self.device_info = scrolledtext.ScrolledText(details_frame, height=8, width=50)
        self.device_info.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)
        self.device_info.config(state=tk.DISABLED)
        
        # 配置框架
        config_frame = ttk.LabelFrame(right_frame, text="设备配置", padding="5")
        config_frame.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)
        
        # WiFi配置
        wifi_frame = ttk.LabelFrame(config_frame, text="WiFi配置", padding="5")
        wifi_frame.pack(fill=tk.X, padx=2, pady=2)
        
        # 添加提示标签
        wifi_tip = ttk.Label(wifi_frame, text="请配置设备连接到与PC相同的WiFi网络，这样才能在STA模式下通信", 
                            foreground="blue", wraplength=400)
        wifi_tip.grid(row=0, column=0, columnspan=2, sticky=tk.W, padx=2, pady=2)
        
        ttk.Label(wifi_frame, text="SSID:").grid(row=1, column=0, sticky=tk.W, padx=2, pady=2)
        self.wifi_ssid = ttk.Entry(wifi_frame, width=30)
        self.wifi_ssid.grid(row=1, column=1, sticky=tk.W+tk.E, padx=2, pady=2)
        
        ttk.Label(wifi_frame, text="密码:").grid(row=2, column=0, sticky=tk.W, padx=2, pady=2)
        # 修改为明文显示
        self.wifi_password = ttk.Entry(wifi_frame, width=30)
        self.wifi_password.grid(row=2, column=1, sticky=tk.W+tk.E, padx=2, pady=2)
        
        wifi_save = ttk.Button(wifi_frame, text="保存WiFi设置", command=self.save_wifi)
        wifi_save.grid(row=3, column=0, columnspan=2, padx=2, pady=2, sticky=tk.W+tk.E)
        
        
        # 串口配置
        serial_frame = ttk.LabelFrame(config_frame, text="串口配置", padding="5")
        serial_frame.pack(fill=tk.X, padx=2, pady=2)
        
        ttk.Label(serial_frame, text="波特率:").grid(row=0, column=0, sticky=tk.W, padx=2, pady=2)
        self.baudrate = ttk.Combobox(serial_frame, values=["9600", "19200", "38400", "57600", "115200"])
        self.baudrate.grid(row=0, column=1, sticky=tk.W+tk.E, padx=2, pady=2)
        # 添加焦点事件处理
        self.baudrate.bind("<FocusIn>", lambda event: self.set_editing_serial(True))
        self.baudrate.bind("<FocusOut>", lambda event: self.set_editing_serial(False))
        
        ttk.Label(serial_frame, text="校验位:").grid(row=1, column=0, sticky=tk.W, padx=2, pady=2)
        self.parity = ttk.Combobox(serial_frame, values=["N", "E", "O"])
        self.parity.grid(row=1, column=1, sticky=tk.W+tk.E, padx=2, pady=2)
        # 添加焦点事件处理
        self.parity.bind("<FocusIn>", lambda event: self.set_editing_serial(True))
        self.parity.bind("<FocusOut>", lambda event: self.set_editing_serial(False))
        
        serial_save = ttk.Button(serial_frame, text="保存串口设置", command=self.save_serial)
        serial_save.grid(row=2, column=0, columnspan=2, padx=2, pady=2, sticky=tk.W+tk.E)
        
        # 操作按钮
        action_frame = ttk.Frame(config_frame, padding="5")
        action_frame.pack(fill=tk.X, padx=2, pady=2)
        
        # 改为两行按钮，提高空间利用率
        top_buttons = ttk.Frame(action_frame)
        top_buttons.pack(fill=tk.X, expand=True)
        
        restart_button = ttk.Button(top_buttons, text="重启设备", command=self.restart_device)
        restart_button.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2, pady=2)
        
        connect_serial = ttk.Button(top_buttons, text="连接到Telnet终端", command=self.connect_telnet)
        connect_serial.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2, pady=2)
        
        # 底部按钮
        bottom_buttons = ttk.Frame(action_frame)
        bottom_buttons.pack(fill=tk.X, expand=True)
        
        # 添加重置配置按钮
        reset_button = ttk.Button(bottom_buttons, text="重置设备配置", 
                                 command=self.reset_device_config,
                                 style="Danger.TButton")
        reset_button.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=2, pady=2)
        
        # 创建危险按钮样式
        self.root.style = ttk.Style()
        self.root.style.configure("Danger.TButton", foreground="red")
        
        # 日志框架
        log_frame = ttk.LabelFrame(right_frame, text="日志", padding="5")
        log_frame.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)
        
        self.log_text = scrolledtext.ScrolledText(log_frame, height=7)
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)
        self.log_text.config(state=tk.DISABLED)
        
    def load_config(self):
        """加载程序配置"""
        try:
            if not os.path.exists(self.config_dir):
                os.makedirs(self.config_dir)
                
            if os.path.exists(self.config_file):
                with open(self.config_file, 'r') as f:
                    return json.load(f)
        except Exception as e:
            self.log(f"加载配置文件失败: {str(e)}")
        
        # 默认配置
        return {
            "first_use": True
        }
        
    def save_config(self, config):
        """保存程序配置"""
        try:
            if not os.path.exists(self.config_dir):
                os.makedirs(self.config_dir)
                
            with open(self.config_file, 'w') as f:
                json.dump(config, f)
                
            return True
        except Exception as e:
            self.log(f"保存配置文件失败: {str(e)}")
            return False
            
    def check_first_use(self):
        """检查是否是首次使用"""
        config = self.load_config()
        
        if config.get("first_use", True):
            self.log("欢迎使用ESP8266管理工具！")
            self.log("首次使用：请点击'连接设备AP向导'按钮进行设置")
            
            # 显示首次使用向导
            self.show_ap_wizard()
            
            # 更新配置，标记已不是首次使用
            config["first_use"] = False
            self.save_config(config)
        
    def show_ap_wizard(self):
        """显示连接设备AP的向导"""
        wizard = tk.Toplevel(self.root)
        wizard.title("连接设备AP向导")
        wizard.geometry("550x450")
        wizard.transient(self.root)
        wizard.grab_set()
        
        # 向导内容
        ttk.Label(wizard, text="首次使用：连接到设备AP的步骤", font=("Arial", 12, "bold")).pack(pady=10)
        
        steps = ttk.Frame(wizard, padding=10)
        steps.pack(fill=tk.BOTH, expand=True)
        
        ttk.Label(steps, text="1. 确保设备已通电", wraplength=500).pack(anchor=tk.W, pady=5)
        ttk.Label(steps, text="2. 查看可用WiFi网络，寻找名为'ESP8266-AP-xxxx'的网络", wraplength=500).pack(anchor=tk.W, pady=5)
        ttk.Label(steps, text="3. 连接到该网络，密码为: 12345678", wraplength=500).pack(anchor=tk.W, pady=5)
        ttk.Label(steps, text="4. 连接成功后，返回此程序，查看设备列表", wraplength=500).pack(anchor=tk.W, pady=5)
        ttk.Label(steps, text="5. 如果设备未出现在列表中，点击'刷新设备列表'按钮", wraplength=500).pack(anchor=tk.W, pady=5)
        ttk.Label(steps, text="6. 选择设备，配置WiFi连接信息（与你PC相同的WiFi网络）", wraplength=500, foreground="blue").pack(anchor=tk.W, pady=5)
        
        note_frame = ttk.Frame(steps, padding=5)
        note_frame.pack(fill=tk.X, pady=10)
        
        ttk.Label(note_frame, text="注意:", font=("Arial", 9, "bold")).pack(side=tk.LEFT, anchor=tk.N)
        ttk.Label(note_frame, text="首次配置完成后，设备将自动连接到你指定的WiFi网络。"
                               "当设备和PC在同一网络时，你可以通过其STA IP地址访问设备。", 
                               wraplength=450).pack(side=tk.LEFT, anchor=tk.N, padx=5)
        
        # 打开设备网页按钮
        open_device_web = ttk.Button(steps, text="打开设备网页 (192.168.4.1)", command=lambda: webbrowser.open("http://192.168.4.1"))
        open_device_web.pack(pady=10)
        
        # 不再显示复选框
        self.show_wizard_again = tk.BooleanVar(value=False)
        check = ttk.Checkbutton(wizard, text="不再显示此向导", variable=self.show_wizard_again,
                               command=lambda: self.set_show_wizard(not self.show_wizard_again.get()))
        check.pack(pady=5)
        
        # 关闭按钮
        ttk.Button(wizard, text="关闭向导", command=wizard.destroy).pack(pady=10)
    
    def set_show_wizard(self, show_again):
        """设置是否再次显示向导"""
        config = self.load_config()
        config["first_use"] = show_again
        self.save_config(config)
        
    def show_available_wifi(self):
        """显示设备可用的WiFi列表"""
        ip = self.get_device_ip()
        if not ip:
            return
            
        try:
            # 创建一个新窗口
            wifi_window = tk.Toplevel(self.root)
            wifi_window.title("可用WiFi网络")
            wifi_window.geometry("400x400")
            wifi_window.transient(self.root)
            
            # 添加加载提示
            loading_label = ttk.Label(wifi_window, text="正在扫描WiFi网络...", font=("Arial", 10))
            loading_label.pack(pady=20)
            
            # 窗口加载时，避免阻塞UI线程，使用线程获取WiFi列表
            def get_wifi_list():
                try:
                    # 发送API请求获取WiFi列表
                    response = requests.get(f"http://{ip}/api/wifi/scan", timeout=10)
                    
                    if response.status_code == 200:
                        wifi_data = response.json()
                        
                        # 更新UI
                        wifi_window.after(0, lambda: update_wifi_ui(wifi_data))
                    else:
                        wifi_window.after(0, lambda: loading_label.config(
                            text=f"扫描失败: HTTP {response.status_code}"))
                except Exception as e:
                    wifi_window.after(0, lambda: loading_label.config(
                        text=f"扫描失败: {str(e)}"))
            
            def update_wifi_ui(wifi_data):
                # 移除加载标签
                loading_label.destroy()
                
                # 创建列表框架
                list_frame = ttk.Frame(wifi_window, padding=10)
                list_frame.pack(fill=tk.BOTH, expand=True)
                
                # 创建标题
                ttk.Label(list_frame, text="点击网络名称选择:", font=("Arial", 10, "bold")).pack(anchor=tk.W, pady=5)
                
                # 列表框和滚动条
                list_frame_inner = ttk.Frame(list_frame)
                list_frame_inner.pack(fill=tk.BOTH, expand=True)
                
                scrollbar = ttk.Scrollbar(list_frame_inner)
                scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
                
                # 创建列表框
                wifi_listbox = tk.Listbox(list_frame_inner, width=50, height=15)
                wifi_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
                
                # 配置滚动条
                scrollbar.config(command=wifi_listbox.yview)
                wifi_listbox.config(yscrollcommand=scrollbar.set)
                
                # 填充WiFi列表
                networks = wifi_data.get("networks", [])
                for network in networks:
                    ssid = network.get("ssid", "Unknown")
                    rssi = network.get("rssi", 0)
                    security = "🔒" if network.get("encrypted", False) else "  "
                    wifi_listbox.insert(tk.END, f"{security} {ssid} ({rssi} dBm)")
                
                # 选择处理
                def on_wifi_select(event):
                    selection = wifi_listbox.curselection()
                    if selection:
                        index = selection[0]
                        network = networks[index]
                        ssid = network.get("ssid", "")
                        self.wifi_ssid.delete(0, tk.END)
                        self.wifi_ssid.insert(0, ssid)
                        wifi_window.destroy()
                
                wifi_listbox.bind("<Double-1>", on_wifi_select)
                
                # 按钮框架
                button_frame = ttk.Frame(wifi_window)
                button_frame.pack(fill=tk.X, pady=10)
                
                # 关闭按钮
                ttk.Button(button_frame, text="关闭", command=wifi_window.destroy).pack(side=tk.RIGHT, padx=10)
                
                # 如果没有找到网络
                if not networks:
                    ttk.Label(list_frame, text="没有找到WiFi网络", foreground="red").pack(pady=10)
            
            # 启动线程获取WiFi列表
            threading.Thread(target=get_wifi_list, daemon=True).start()
            
        except Exception as e:
            messagebox.showerror("错误", f"扫描WiFi失败: {str(e)}")
        
    def udp_listener(self):
        """UDP广播监听线程"""
        self.log("启动UDP监听，端口8266...")
        
        # 创建UDP套接字
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            # 绑定到广播端口
            sock.bind(('', 8266))
            
            # 设置非阻塞
            sock.settimeout(1)
            
            while self.udp_running:
                try:
                    # 接收数据
                    data, addr = sock.recvfrom(1024)
                    
                    # 解析JSON
                    try:
                        device_info = json.loads(data.decode())
                        self.process_device_broadcast(device_info, addr)
                    except json.JSONDecodeError:
                        self.log(f"收到无效JSON数据: {data.decode()}")
                except socket.timeout:
                    pass
                    
        except Exception as e:
            self.log(f"UDP监听错误: {str(e)}")
        finally:
            sock.close()
                
    def process_device_broadcast(self, device_info, addr):
        """处理设备广播"""
        # 检查必要字段
        if 'device_id' in device_info:
            device_id = device_info['device_id']
            
            # 提取IP地址
            ip = None
            if 'sta_ip' in device_info and device_info.get('connected', False):
                ip = device_info['sta_ip']
            elif 'ap_ip' in device_info:
                ip = device_info['ap_ip']
            
            # 如果找到有效IP
            if ip:
                # 检查是否为新设备
                is_new = device_id not in self.devices
                
                # 更新或添加设备信息
                device_info['last_seen'] = datetime.now()
                device_info['addr'] = addr[0]
                self.devices[device_id] = device_info
                
                # 更新UI
                self.update_device_list()
                
                # 如果正在查看该设备，更新设备信息
                if self.selected_device == device_id:
                    self.update_device_info()
                
                # 如果是新设备，记录日志
                if is_new:
                    self.log(f"发现新设备: {device_id} 在 {ip}")
                    
    def update_device_list(self):
        """更新设备列表UI"""
        # 清空现有列表
        for item in self.device_tree.get_children():
            self.device_tree.delete(item)
            
        # 添加设备到列表
        for device_id, device_info in self.devices.items():
            # 获取IP地址
            ip = device_info.get('sta_ip', device_info.get('ap_ip', 'Unknown'))
            
            # 插入设备
            self.device_tree.insert('', tk.END, iid=device_id, values=(device_id, ip))
            
        # 如果当前选中设备在列表中，保持选中
        if self.selected_device in self.devices:
            self.device_tree.selection_set(self.selected_device)
            
    def on_device_select(self, event):
        """设备选择事件处理"""
        selection = self.device_tree.selection()
        if selection:
            self.selected_device = selection[0]
            # 获取最新的设备API信息
            self.fetch_device_api_info()
            
    def fetch_device_api_info(self):
        """获取设备的API详细信息"""
        if not self.selected_device or self.selected_device not in self.devices:
            return
            
        ip = self.get_device_ip()
        if not ip:
            return
            
        try:
            # 发送请求获取API信息
            response = requests.get(f"http://{ip}/api", timeout=5)
            
            if response.status_code == 200:
                self.device_api_info = response.json()
                
                # 只在首次选择设备时更新串口设置UI
                if not self.editing_serial and 'serial' in self.device_api_info:
                    serial_info = self.device_api_info['serial']
                    self.baudrate.set(serial_info.get('baudrate', '9600'))
                    self.parity.set(serial_info.get('parity', 'N'))
                
                # 更新设备信息显示
                self.update_device_info()
            else:
                self.log(f"获取设备API信息失败: HTTP {response.status_code}")
        except Exception as e:
            self.log(f"连接设备API失败: {str(e)}")
        
    def update_device_info(self):
        """更新设备详情显示"""
        if not self.selected_device or self.selected_device not in self.devices:
            return
            
        # 获取设备信息
        device_info = self.devices[self.selected_device]
        
        # 清空并启用文本框
        self.device_info.config(state=tk.NORMAL)
        self.device_info.delete(1.0, tk.END)
        
        # 格式化信息
        info_text = f"设备ID: {device_info.get('device_id', 'Unknown')}\n"
        info_text += f"AP SSID: {device_info.get('ap_ssid', 'Unknown')}\n"
        info_text += f"WiFi模式: {device_info.get('wifi_mode', 'Unknown')}\n"
        
        if device_info.get('connected', False):
            info_text += f"WiFi已连接: 是\n"
            info_text += f"连接到SSID: {device_info.get('ssid', 'Unknown')}\n"
            info_text += f"STA IP地址: {device_info.get('sta_ip', 'Unknown')}\n"
        else:
            info_text += f"WiFi已连接: 否\n"
            
        if 'ap_ip' in device_info:
            info_text += f"AP IP地址: {device_info.get('ap_ip', 'Unknown')}\n"
            
        # 如果有API信息，添加串口配置
        if self.device_api_info and 'serial' in self.device_api_info:
            serial_info = self.device_api_info['serial']
            info_text += f"\n串口配置:\n"
            info_text += f"波特率: {serial_info.get('baudrate', 'Unknown')}\n"
            info_text += f"校验位: {serial_info.get('parity', 'Unknown')}\n"
            
        # 格式化时间
        last_seen = device_info.get('last_seen')
        if isinstance(last_seen, datetime):
            time_str = last_seen.strftime('%H:%M:%S')
        else:
            time_str = str(last_seen)
            
        info_text += f"\n最后活跃时间: {time_str}\n"
        
        # 显示信息
        self.device_info.insert(tk.END, info_text)
        self.device_info.config(state=tk.DISABLED)
        
    def refresh_devices(self):
        """刷新设备列表，移除长时间未活动的设备"""
        current_time = datetime.now()
        to_remove = []
        
        for device_id, device_info in self.devices.items():
            last_seen = device_info.get('last_seen')
            if last_seen and (current_time - last_seen).total_seconds() > 30:
                to_remove.append(device_id)
                
        for device_id in to_remove:
            del self.devices[device_id]
            self.log(f"设备 {device_id} 已超时移除")
            
        self.update_device_list()
        self.log("设备列表已刷新")
        
    def get_device_ip(self):
        """获取当前选中设备的IP地址"""
        if not self.selected_device or self.selected_device not in self.devices:
            messagebox.showerror("错误", "请先选择一个设备")
            return None
            
        device_info = self.devices[self.selected_device]
        
        # 优先使用STA IP (如果已连接)
        if device_info.get('connected', False) and 'sta_ip' in device_info:
            return device_info['sta_ip']
        
        # 其次使用AP IP
        if 'ap_ip' in device_info:
            return device_info['ap_ip']
            
        return None
        
    def save_wifi(self):
        """保存WiFi设置"""
        ip = self.get_device_ip()
        if not ip:
            return
            
        ssid = self.wifi_ssid.get()
        password = self.wifi_password.get()
        
        if not ssid:
            messagebox.showerror("错误", "SSID不能为空")
            return
            
        try:
            # 构建JSON数据
            data = {
                "ssid": ssid,
                "password": password
            }
            
            # 发送请求
            response = requests.post(f"http://{ip}/api/wifi", json=data, timeout=5)
            
            if response.status_code == 200:
                result = response.json()
                if result.get('success', False):
                    messagebox.showinfo("成功", "WiFi设置已保存，设备将尝试连接到新网络")
                    self.log(f"WiFi设置已保存到设备 {self.selected_device}")
                    
                    # 提示用户WiFi切换
                    if messagebox.askyesno("WiFi切换", "设备将尝试连接到新WiFi。\n是否要重启设备使设置生效？"):
                        self.restart_device()
                else:
                    messagebox.showerror("错误", f"保存失败: {result.get('message', '未知错误')}")
            else:
                messagebox.showerror("错误", f"HTTP错误: {response.status_code}")
                
        except Exception as e:
            messagebox.showerror("错误", f"连接错误: {str(e)}")
            
    def save_serial(self):
        """保存串口设置"""
        ip = self.get_device_ip()
        if not ip:
            return
            
        baudrate = self.baudrate.get()
        parity = self.parity.get()
        
        try:
            # 构建JSON数据
            data = {
                "baudrate": baudrate,
                "parity": parity
            }
            
            # 发送请求
            response = requests.post(f"http://{ip}/api/serial", json=data, timeout=5)
            
            if response.status_code == 200:
                result = response.json()
                if result.get('success', False):
                    messagebox.showinfo("成功", "串口设置已保存")
                    self.log(f"串口设置(波特率:{baudrate}, 校验位:{parity})已保存到设备 {self.selected_device}")
                    
                    # 重新获取设备信息以更新显示
                    self.fetch_device_api_info()
                else:
                    messagebox.showerror("错误", f"保存失败: {result.get('message', '未知错误')}")
            else:
                messagebox.showerror("错误", f"HTTP错误: {response.status_code}")
                
        except Exception as e:
            messagebox.showerror("错误", f"连接错误: {str(e)}")
            
    def restart_device(self):
        """重启设备"""
        ip = self.get_device_ip()
        if not ip:
            return
            
        if messagebox.askyesno("确认", "确认要重启设备吗?"):
            try:
                # 发送重启请求
                response = requests.post(f"http://{ip}/api/restart", timeout=5)
                
                if response.status_code == 200:
                    messagebox.showinfo("成功", "设备正在重启")
                    self.log(f"设备 {self.selected_device} 正在重启")
                else:
                    messagebox.showerror("错误", f"HTTP错误: {response.status_code}")
                    
            except Exception as e:
                messagebox.showerror("错误", f"连接错误: {str(e)}")
    
    def reset_device_config(self):
        """重置设备配置"""
        ip = self.get_device_ip()
        if not ip:
            return
            
        if messagebox.askyesno("警告", "确认要重置设备所有配置吗？这将删除WiFi和串口设置，并重启设备。", icon='warning'):
            try:
                # 发送重置请求
                response = requests.post(f"http://{ip}/api/reset", timeout=5)
                
                if response.status_code == 200:
                    messagebox.showinfo("成功", "设备配置已重置，设备正在重启")
                    self.log(f"设备 {self.selected_device} 配置已重置，正在重启")
                else:
                    messagebox.showerror("错误", f"HTTP错误: {response.status_code}")
                    
            except Exception as e:
                messagebox.showerror("错误", f"连接错误: {str(e)}")
                
    def connect_telnet(self):
        """连接Telnet终端"""
        ip = self.get_device_ip()
        if not ip:
            return
            
        # 在Windows上启动telnet客户端
        try:
            subprocess.Popen(f"telnet {ip} 23")
            self.log(f"已启动Telnet客户端连接到 {ip}")
        except Exception as e:
            messagebox.showerror("错误", f"无法启动Telnet: {str(e)}")
                
    def log(self, message):
        """添加日志消息"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        log_entry = f"[{timestamp}] {message}\n"
        
        # 启用文本框
        self.log_text.config(state=tk.NORMAL)
        
        # 添加消息
        self.log_text.insert(tk.END, log_entry)
        
        # 滚动到底部
        self.log_text.see(tk.END)
        
        # 禁用文本框
        self.log_text.config(state=tk.DISABLED)
        
    def on_closing(self):
        """关闭窗口事件处理"""
        self.udp_running = False
        self.root.destroy()
        
    def set_editing_serial(self, editing):
        """设置是否正在编辑串口参数"""
        self.editing_serial = editing
        
if __name__ == "__main__":
    root = tk.Tk()
    app = ESP8266Manager(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()