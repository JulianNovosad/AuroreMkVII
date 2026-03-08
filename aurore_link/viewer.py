#!/usr/bin/env python3
"""
Aurore MkVII Telemetry Viewer

OpenCV-based MVP viewer displaying:
- Current mode (AUTO/FREECAM)
- FCS state
- Frame count
- Gimbal angles (AZ, EL)
- Track centroid and confidence
"""

import sys
import cv2
import numpy as np

from aurore_pb2 import Telemetry, ProtoFcsState, OperatingMode
from client import AuroreClient


FCS_STATE_NAMES = {
    ProtoFcsState.PROTO_BOOT: "BOOT",
    ProtoFcsState.PROTO_IDLE_SAFE: "IDLE_SAFE",
    ProtoFcsState.PROTO_FREECAM: "FREECAM",
    ProtoFcsState.PROTO_SEARCH: "SEARCH",
    ProtoFcsState.PROTO_TRACKING: "TRACKING",
    ProtoFcsState.PROTO_ARMED: "ARMED",
    ProtoFcsState.PROTO_FAULT: "FAULT",
}

MODE_NAMES = {
    OperatingMode.AUTO: "AUTO",
    OperatingMode.FREECAM_MODE: "FREECAM",
}


class TelemetryViewer:
    """OpenCV viewer for Aurore telemetry."""

    def __init__(self, host: str = "127.0.0.1"):
        self.client = AuroreClient(host=host)
        self.client.on_telemetry = self._on_telemetry

        self._latest_telemetry: Telemetry | None = None
        self._frame_count = 0
        self._running = True

        cv2.namedWindow("Aurore MkVII Telemetry", cv2.WINDOW_NORMAL)
        cv2.resizeWindow("Aurore MkVII Telemetry", 640, 480)

    def _on_telemetry(self, telemetry: Telemetry):
        """Callback for received telemetry."""
        self._latest_telemetry = telemetry
        self._frame_count += 1

    def _draw_overlay(self, frame: np.ndarray) -> np.ndarray:
        """Draw telemetry overlay on frame."""
        h, w = frame.shape[:2]

        if self._latest_telemetry is None:
            cv2.putText(
                frame,
                "Waiting for telemetry...",
                (w // 2 - 120, h // 2),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 255, 255),
                2,
            )
            return frame

        t = self._latest_telemetry
        y = 30
        line_height = 25

        mode_name = MODE_NAMES.get(t.health.mode, "UNKNOWN")
        mode_color = (0, 255, 0) if t.health.mode == OperatingMode.AUTO else (0, 255, 255)
        cv2.putText(
            frame, f"Mode: {mode_name}", (10, y), cv2.FONT_HERSHEY_SIMPLEX, 0.6, mode_color, 2
        )
        y += line_height

        fcs_state = FCS_STATE_NAMES.get(t.health.fcs_state, "UNKNOWN")
        state_color = (0, 255, 0) if t.health.fcs_state not in (
            ProtoFcsState.PROTO_FAULT,
            ProtoFcsState.PROTO_BOOT,
        ) else (0, 0, 255)
        cv2.putText(
            frame, f"FCS State: {fcs_state}", (10, y), cv2.FONT_HERSHEY_SIMPLEX, 0.6, state_color, 2
        )
        y += line_height

        cv2.putText(
            frame,
            f"Frames: {self._frame_count}",
            (10, y),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.6,
            (255, 255, 255),
            2,
        )
        y += line_height

        cv2.putText(
            frame,
            f"Gimbal AZ: {t.gimbal.az_deg:7.2f} deg",
            (10, y),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.6,
            (255, 255, 255),
            2,
        )
        y += line_height

        cv2.putText(
            frame,
            f"Gimbal EL: {t.gimbal.el_deg:7.2f} deg",
            (10, y),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.6,
            (255, 255, 255),
            2,
        )
        y += line_height * 2

        if t.track.valid:
            cv2.putText(
                frame,
                f"Track: ({t.track.centroid_x:.1f}, {t.track.centroid_y:.1f})",
                (10, y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (0, 255, 255),
                2,
            )
            y += line_height
            cv2.putText(
                frame,
                f"Confidence: {t.track.confidence:.2f}",
                (10, y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (0, 255, 255),
                2,
            )
            y += line_height
            cv2.putText(
                frame,
                f"Range: {t.track.range_m:.1f} m",
                (10, y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (0, 255, 255),
                2,
            )
        else:
            cv2.putText(
                frame,
                "Track: INVALID",
                (10, y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (0, 0, 255),
                2,
            )

        y += line_height * 2
        cv2.putText(
            frame,
            "Keys: a=AUTO  f=FREECAM  q=Quit",
            (10, h - 20),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (128, 128, 128),
            1,
        )

        return frame

    def run(self):
        """Main viewer loop."""
        if not self.client.connect():
            print("Failed to connect to Aurore")
            return

        blank_frame = np.zeros((480, 640, 3), dtype=np.uint8)

        try:
            while self._running:
                frame = blank_frame.copy()
                frame = self._draw_overlay(frame)
                cv2.imshow("Aurore MkVII Telemetry", frame)

                key = cv2.waitKey(16) & 0xFF
                if key == ord("q"):
                    self._running = False
                elif key == ord("a"):
                    self.client.send_mode_switch("AUTO")
                elif key == ord("f"):
                    self.client.send_mode_switch("FREECAM")
        finally:
            self.client.disconnect()
            cv2.destroyAllWindows()


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 viewer.py <host>")
        print("Example: python3 viewer.py 127.0.0.1")
        sys.exit(1)

    host = sys.argv[1]
    viewer = TelemetryViewer(host=host)
    viewer.run()


if __name__ == "__main__":
    main()
