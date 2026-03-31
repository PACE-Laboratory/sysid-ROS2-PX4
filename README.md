# sysid-ROS2-PX4

PX4-ROS2 offboard control for automated test inputs.

## What this repository does

This repository provides a ROS2 package (`sysid`) with an offboard-control node (`albatross`) used for system identification of PX4-based UAVs. It is currently tailored towards statically stable fixed-wing aircraft; future additions include approaches for multirotor and eVTOL aircraft.

At runtime, the node:
- Publishes PX4 offboard mode heartbeats (`/fmu/in/offboard_control_mode`) with `direct_actuator=true`.
- Publishes direct actuator commands for servos and motors (`/fmu/in/actuator_servos`, `/fmu/in/actuator_motors`).
- Subscribes to pilot inputs and vehicle state (`/fmu/out/manual_control_setpoint`, `/fmu/out/vehicle_status`).
- Loads a multisine excitation signal from a CSV file and adds it to pilot commands when in Offboard mode.

## Repository layout

- `src/sysid/` – ROS 2 package (`CMakeLists.txt`, `package.xml`, and node [e.g., `albatross.cpp`]).
- `src/signals/` – excitation input CSV files.
- `src/px4_msgs/` – PX4 message definitons (submodule). *Ensure submodule branch matches PX4 version.*
- `src/px4_ros_com/` – PX4-ROS2 bridge (submodule). *Ensure submodule branch matches PX4 version.*
- `startup_local.bash` – template bash script for bare-metal startup.
- `startup_docker.bash` – template bash script for docker container startup.
- `Dockerfile` + `compose.yaml` – containerized deployment.

---

## ROS 2 package details (`sysid`)

> Note: the CSV file path is currently hardcoded in the node source. For example,
> `/home/pace/src/sysid-ROS2-PX4/src/signals/ms_albatross_3s1p_T30_f005-075-2_100hz.csv`.

### PX4-ROS2 interfaces used

**Publishers**
- `/fmu/in/offboard_control_mode` (`px4_msgs/msg/OffboardControlMode`)
- `/fmu/in/actuator_servos` (`px4_msgs/msg/ActuatorServos`)
- `/fmu/in/actuator_motors` (`px4_msgs/msg/ActuatorMotors`)

**Subscribers**
- `/fmu/out/manual_control_setpoint` (`px4_msgs/msg/ManualControlSetpoint`)
- `/fmu/out/vehicle_status` (`px4_msgs/msg/VehicleStatus`)

---

## Local build and usage

Example workflow on a ROS2 host:

```bash
# from repo root
source /opt/ros/jazzy/setup.bash
colcon build
source install/setup.bash
ros2 run sysid fox
```

Make sure PX4 DDS bridging is available (typically through `MicroXRCEAgent`) before running the node.

## Docker deployment approach

This repo includes a containerized approach.

What the container does:
- Starts from `ros:humble-ros-base`.
- Builds and installs `Micro-XRCE-DDS-Agent` inside the image.
- Copies this repo's ROS workspace into `/ros_ws` and runs `colcon build`.
- Uses `/ros_ws/startup.bash` (from `startup_docker.bash`) as the container command.

Run with Docker Compose:
```bash
docker compose up --build -d
```

The compose service is `privileged: true` so serial devices (like `/dev/ttyUSB0`) are accessible for the PX4-ROS2 bridge (i.e., the MicroXRCEAgent connection).

---

## Bare-Metal Automatic Startup
**1. Create the following three files.**

#### `/etc/systemd/system/microxrceagent.service`
```ini
[Unit]
Description=Micro XRCE Agent
After=remote-fs.target

[Service]
ExecStart=/usr/local/bin/MicroXRCEAgent serial --dev /dev/ttyUSB0 -b 921600
Restart=always
KillMode=control-group

[Install]
WantedBy=multi-user.target
```

#### `/etc/systemd/system/ros2_startup.service`
```ini
[Unit]
Description=ros2_startup
After=microxrceagent.service remote-fs.target syslog.target
Wants=microxrceagent.service

[Service]
User=pace
WorkingDirectory=/home/pace
ExecStart=/bin/bash /home/pace/ros2_startup.bash
Restart=on-failure
KillMode=control-group

[Install]
WantedBy=multi-user.target
```

#### `/home/pace/ros2_startup.bash`
```bash
#!/bin/bash
source /opt/ros/jazzy/setup.bash
source /home/pace/src/sysid-ROS2-PX4/install/setup.bash
ros2 run sysid albatross

```

**2. Mark the bash script as executable and enable the systemd services.**
```bash
sudo chmod +x /home/pace/ros2_startup.bash
sudo systemctl daemon-reload
sudo systemctl enable --now microxrceagent.service ros2_startup.service
```


