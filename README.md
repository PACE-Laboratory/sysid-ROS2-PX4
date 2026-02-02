# sysid-ROS2-PX4
PX4-ROS2 offboard control for automated test inputs

## Automatic Startup
Create the following three files.

#### `/etc/systemd/system/microxrceagent.service`
```
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
```
[Unit]
Description=ros2_startup
After=microxrceagent.service remote-fs.target syslog.target
Wants=microxrceagent.service

[Service]
User=nsl
WorkingDirectory=/home/nsl
ExecStart=/bin/bash /home/nsl/ros2_startup.bash
Restart=on-failure
KillMode=control-group

[Install]
WantedBy=multi-user.target
```

#### `/home/nsl/ros2_startup.bash`
```
#!/bin/bash
source /opt/ros/jazzy/setup.bash
source /home/nsl/src/sysid-ROS2-PX4/install/setup.bash
ros2 run sysid mtd

```

Mark the bash script as executable and enable the systemd services.
```
$ sudo chmod +x /home/nsl/ros2_startup.bash
$ sudo systemctl daemon-reload
$ sudo systemctl enable --now microxrceagent.service ros2_startup.service
```


