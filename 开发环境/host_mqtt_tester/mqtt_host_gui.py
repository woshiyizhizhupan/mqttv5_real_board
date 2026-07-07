from __future__ import annotations

import json
import ssl
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from typing import Any

import paho.mqtt.client as mqtt

from mqtt_tester.log_buffer import LogBuffer
from mqtt_tester.mqtt_loopback import is_success_reason_code
from mqtt_tester.sim_board_server import build_city_device_topics


class MqttHostGui(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("HC32F460 MQTT5 上位机测试工具")
        self.geometry("980x680")
        self.log_buffer = LogBuffer()
        self.client: mqtt.Client | None = None
        self.connected = False
        self.topic_names: list[str] = []
        self.topic_values: list[str] = []
        self._build_ui()
        self._refresh_topics()

    def _build_ui(self) -> None:
        root = ttk.Frame(self, padding=10)
        root.pack(fill=tk.BOTH, expand=True)

        conn = ttk.LabelFrame(root, text="连接")
        conn.pack(fill=tk.X)
        self.host = self._entry(conn, "Host", "39.103.154.108", 0, 0)
        self.port = self._entry(conn, "Port", "1883", 0, 2, width=8)
        self.city_id = self._entry(conn, "CityId", "tjw", 1, 0)
        self.pole_id = self._entry(conn, "PoleId", "pole001", 1, 2)
        self.device_name = self._entry(conn, "DeviceName", "GM400-452089", 2, 0)
        self.device_id = self._entry(conn, "DeviceId", "GM400-452089", 2, 2)
        self.username = self._entry(conn, "Username", "GM400-452089", 3, 0)
        self.password = self._entry(conn, "Password", "public", 3, 2, show="*")
        self.qos = self._entry(conn, "QoS", "2", 4, 0, width=8)
        self.use_tls = tk.BooleanVar(value=False)
        ttk.Checkbutton(conn, text="TLS", variable=self.use_tls).grid(row=4, column=2, sticky=tk.W, padx=4, pady=4)
        self.ca_file = self._entry(conn, "CA", "", 5, 0, width=34)
        ttk.Button(conn, text="选择 CA", command=lambda: self._choose_file(self.ca_file)).grid(row=5, column=2, padx=4)
        self.cert_file = self._entry(conn, "Client Cert", "", 6, 0, width=34)
        self.key_file = self._entry(conn, "Client Key", "", 6, 2, width=34)
        ttk.Button(conn, text="刷新主题", command=self._refresh_topics).grid(row=7, column=0, padx=4, pady=6, sticky=tk.W)
        ttk.Button(conn, text="连接", command=self._connect).grid(row=7, column=1, padx=4, pady=6, sticky=tk.W)
        ttk.Button(conn, text="断开", command=self._disconnect).grid(row=7, column=2, padx=4, pady=6, sticky=tk.W)

        topics = ttk.LabelFrame(root, text="五个业务主题")
        topics.pack(fill=tk.X, pady=8)
        self.topic_box = tk.Listbox(topics, height=6)
        self.topic_box.pack(fill=tk.X, padx=6, pady=6)

        pub = ttk.LabelFrame(root, text="发布")
        pub.pack(fill=tk.X)
        self.topic_select = ttk.Combobox(pub, state="readonly")
        self.topic_select.grid(row=0, column=0, sticky=tk.EW, padx=6, pady=6)
        pub.columnconfigure(0, weight=1)
        self.payload = tk.Text(pub, height=5)
        self.payload.grid(row=1, column=0, columnspan=2, sticky=tk.EW, padx=6, pady=6)
        self.payload.insert(
            "1.0",
            json.dumps({"id": "gui-ping", "cmd": "ping"}, ensure_ascii=False),
        )
        ttk.Button(pub, text="发布", command=self._publish).grid(row=0, column=1, padx=6, pady=6)
        presets = ttk.Frame(pub)
        presets.grid(row=2, column=0, columnspan=2, sticky=tk.W, padx=6, pady=4)
        ttk.Button(presets, text="ping", command=lambda: self._set_command_payload("cmd_down", {"id": "gui-ping", "cmd": "ping"})).pack(side=tk.LEFT, padx=3)
        ttk.Button(presets, text="get_status", command=lambda: self._set_command_payload("cmd_down", {"id": "gui-status", "cmd": "get_status"})).pack(side=tk.LEFT, padx=3)
        ttk.Button(presets, text="ota_status", command=lambda: self._set_command_payload("ota_down", {"id": "gui-ota-status", "cmd": "ota_status"})).pack(side=tk.LEFT, padx=3)

        logs = ttk.LabelFrame(root, text="日志")
        logs.pack(fill=tk.BOTH, expand=True, pady=8)
        self.log_text = tk.Text(logs, height=16, state=tk.DISABLED)
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)

    def _entry(self, parent: ttk.Frame, label: str, value: str, row: int, col: int, width: int = 20, show: str | None = None) -> ttk.Entry:
        ttk.Label(parent, text=label).grid(row=row, column=col, sticky=tk.W, padx=4, pady=4)
        entry = ttk.Entry(parent, width=width, show=show)
        entry.insert(0, value)
        entry.grid(row=row, column=col + 1, sticky=tk.W, padx=4, pady=4)
        return entry

    def _choose_file(self, entry: ttk.Entry) -> None:
        path = filedialog.askopenfilename()
        if path:
            entry.delete(0, tk.END)
            entry.insert(0, path)

    def _refresh_topics(self) -> None:
        try:
            topics = build_city_device_topics(
                self.city_id.get().strip(),
                self.pole_id.get().strip(),
                self.device_name.get().strip(),
                self.device_id.get().strip() or None,
            )
        except ValueError as exc:
            messagebox.showerror("主题错误", str(exc))
            return
        named_topics = [
            ("telemetry_up", topics.telemetry_up),
            ("cmd_down", topics.cmd_down),
            ("event_up", topics.event_up),
            ("ota_down", topics.ota_down),
            ("debug_up", topics.debug_up),
        ]
        self.topic_names = [name for name, _ in named_topics]
        self.topic_values = [topic for _, topic in named_topics]
        self.topic_box.delete(0, tk.END)
        for name, topic in named_topics:
            self.topic_box.insert(tk.END, f"{name}: {topic}")
        self.topic_select["values"] = self.topic_values
        if self.topic_values:
            self._select_topic_name("cmd_down")

    def _set_command_payload(self, topic_name: str, payload: dict[str, Any]) -> None:
        self._select_topic_name(topic_name)
        self.payload.delete("1.0", tk.END)
        self.payload.insert("1.0", json.dumps(payload, ensure_ascii=False))

    def _select_topic_name(self, topic_name: str) -> None:
        if topic_name in self.topic_names:
            self.topic_select.current(self.topic_names.index(topic_name))
        elif self.topic_values:
            self.topic_select.current(0)

    def _connect(self) -> None:
        if self.connected:
            self._log("WARN", "已经连接")
            return
        self._refresh_topics()
        self.client = self._new_client()
        if self.username.get():
            self.client.username_pw_set(self.username.get(), self.password.get() or None)
        if self.use_tls.get():
            self.client.tls_set(
                ca_certs=self.ca_file.get() or None,
                certfile=self.cert_file.get() or None,
                keyfile=self.key_file.get() or None,
                tls_version=ssl.PROTOCOL_TLS_CLIENT,
            )
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect
        try:
            port = int(self.port.get())
            self.client.connect(self.host.get(), port, keepalive=60)
            self.client.loop_start()
            self._log("INFO", f"正在连接 {self.host.get()}:{port}")
        except Exception as exc:
            self._log("ERROR", f"连接失败: {exc}")

    def _disconnect(self) -> None:
        if self.client:
            self.client.disconnect()
            self.client.loop_stop()
        self.connected = False
        self._log("INFO", "已断开")

    def _publish(self) -> None:
        if not self.client or not self.connected:
            self._log("ERROR", "尚未连接 MQTT")
            return
        topic = self.topic_select.get()
        payload = self.payload.get("1.0", tk.END).strip()
        qos = self._qos_value()
        info = self.client.publish(topic, payload, qos=qos)
        self._log("TX", f"{topic} {payload} rc={info.rc}")

    def _on_connect(self, client: mqtt.Client, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        if is_success_reason_code(reason_code):
            self.connected = True
            for topic in self.topic_values:
                client.subscribe(topic, qos=self._qos_value())
            self._log_from_thread("INFO", "MQTT5 已连接，五个主题已订阅")
        else:
            self._log_from_thread("ERROR", f"连接被拒绝: {reason_code}")

    def _on_message(self, client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
        payload = message.payload.decode("utf-8", errors="replace")
        self._log_from_thread("RX", f"{message.topic} {payload}")

    def _on_disconnect(self, client: mqtt.Client, userdata: Any, disconnect_flags: Any, reason_code: Any, properties: Any = None) -> None:
        self.connected = False
        self._log_from_thread("INFO", f"MQTT 已断开: {reason_code}")

    def _new_client(self) -> mqtt.Client:
        client_id = f"pc-gui-{int(time.time())}"
        return mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
            protocol=mqtt.MQTTv5,
        )

    def _qos_value(self) -> int:
        try:
            value = int(self.qos.get())
        except ValueError:
            return 2
        return max(0, min(2, value))

    def _log(self, level: str, message: str) -> None:
        entry = self.log_buffer.append(level, message)
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.insert(tk.END, entry + "\n")
        self.log_text.see(tk.END)
        self.log_text.configure(state=tk.DISABLED)

    def _log_from_thread(self, level: str, message: str) -> None:
        self.after(0, lambda: self._log(level, message))


def main() -> None:
    app = MqttHostGui()
    app.mainloop()


if __name__ == "__main__":
    main()
