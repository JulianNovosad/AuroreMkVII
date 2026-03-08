#!/usr/bin/env python3
"""
Aurore MkVII TCP Client Library

Provides connection to telemetry (9000) and command (9002) ports
with length-prefixed protobuf framing.
"""

import socket
import struct
import threading
from typing import Callable, Optional

import aurore_pb2
from aurore_pb2 import Telemetry, Command, ModeSwitch, FreecamTarget, OperatingMode


class AuroreClient:
    """TCP client for Aurore MkVII telemetry and command interface."""

    def __init__(
        self,
        host: str = "127.0.0.1",
        telemetry_port: int = 9000,
        command_port: int = 9002,
    ):
        self.host = host
        self.telemetry_port = telemetry_port
        self.command_port = command_port

        self._telemetry_socket: Optional[socket.socket] = None
        self._command_socket: Optional[socket.socket] = None
        self._telemetry_thread: Optional[threading.Thread] = None
        self._running = False

        self._on_telemetry: Optional[Callable[[Telemetry], None]] = None

    @property
    def on_telemetry(self) -> Optional[Callable[[Telemetry], None]]:
        """Callback for received telemetry messages."""
        return self._on_telemetry

    @on_telemetry.setter
    def on_telemetry(self, callback: Callable[[Telemetry], None]):
        self._on_telemetry = callback

    def connect(self) -> bool:
        """Connect to both telemetry and command ports."""
        try:
            self._telemetry_socket = socket.create_connection(
                (self.host, self.telemetry_port), timeout=5.0
            )
            self._command_socket = socket.create_connection(
                (self.host, self.command_port), timeout=5.0
            )
            self._running = True
            self._telemetry_thread = threading.Thread(
                target=self._telemetry_loop, daemon=True
            )
            self._telemetry_thread.start()
            return True
        except (socket.error, OSError) as e:
            print(f"Connection failed: {e}")
            self.disconnect()
            return False

    def disconnect(self):
        """Disconnect from both ports."""
        self._running = False

        if self._telemetry_thread is not None:
            self._telemetry_thread.join(timeout=2.0)
            self._telemetry_thread = None

        if self._telemetry_socket is not None:
            try:
                self._telemetry_socket.close()
            except socket.error:
                pass
            self._telemetry_socket = None

        if self._command_socket is not None:
            try:
                self._command_socket.close()
            except socket.error:
                pass
            self._command_socket = None

    def _recv_exact(self, sock: socket.socket, n: int) -> Optional[bytes]:
        """Receive exactly n bytes from socket."""
        data = b""
        while len(data) < n:
            try:
                chunk = sock.recv(n - len(data))
                if not chunk:
                    return None
                data += chunk
            except socket.error:
                if self._running:
                    continue
                return None
        return data

    def _telemetry_loop(self):
        """Main loop for receiving telemetry messages."""
        assert self._telemetry_socket is not None
        sock = self._telemetry_socket

        while self._running:
            length_bytes = self._recv_exact(sock, 4)
            if length_bytes is None:
                break

            length = struct.unpack(">I", length_bytes)[0]
            if length > 10 * 1024 * 1024:
                print(f"Telemetry message too large: {length} bytes")
                break

            data = self._recv_exact(sock, length)
            if data is None:
                break

            try:
                telemetry = Telemetry()
                telemetry.ParseFromString(data)
                if self._on_telemetry is not None:
                    self._on_telemetry(telemetry)
            except Exception as e:
                print(f"Failed to parse telemetry: {e}")

    def _send_command(self, command: Command):
        """Send a length-prefixed command message."""
        if self._command_socket is None:
            print("Not connected to command port")
            return False

        data = command.SerializeToString()
        length = struct.pack(">I", len(data))
        try:
            self._command_socket.sendall(length + data)
            return True
        except socket.error as e:
            print(f"Command send failed: {e}")
            return False

    def send_mode_switch(self, mode: str) -> bool:
        """
        Send mode switch command.

        Args:
            mode: "AUTO" or "FREECAM"

        Returns:
            True if sent successfully
        """
        command = Command()
        if mode.upper() == "AUTO":
            command.mode_switch.mode = OperatingMode.AUTO
        elif mode.upper() == "FREECAM":
            command.mode_switch.mode = OperatingMode.FREECAM_MODE
        else:
            print(f"Unknown mode: {mode}")
            return False

        return self._send_command(command)

    def send_freecam(self, az: float, el: float, velocity: float) -> bool:
        """
        Send freecam target command.

        Args:
            az: Azimuth angle in degrees
            el: Elevation angle in degrees
            velocity: Velocity in degrees per second

        Returns:
            True if sent successfully
        """
        command = Command()
        command.freecam.az_deg = az
        command.freecam.el_deg = el
        command.freecam.velocity_dps = velocity
        return self._send_command(command)

    def is_connected(self) -> bool:
        """Check if connected to both ports."""
        return (
            self._running
            and self._telemetry_socket is not None
            and self._command_socket is not None
        )
