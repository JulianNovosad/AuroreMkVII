# Full System Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Wire web UI → state machine, add YOLO26n detection, oval sweep pattern for autonomous mode.

**Architecture:** CommandSocket (UNIX) bridges Node.js browser commands to C++ state machine. Yolo26Detector runs in a dedicated non-RT detect thread. SweepPattern drives gimbal in SEARCH state. KCF tracker unchanged for TRACKING.

**Tech Stack:** C++17, ONNX Runtime 1.21, OpenCV, Node.js 18, bash

---

### Task 1: CMakeLists — add ONNX Runtime and new source files
### Task 2: SweepPattern class (pure logic)
### Task 3: Yolo26Detector class (ONNX Runtime)
### Task 4: CommandSocket class (UNIX domain socket)
### Task 5: Node.js — add command socket client and command forwarding
### Task 6: main.cpp — detect thread + CommandSocket + sweep integration
### Task 7: Model export script
