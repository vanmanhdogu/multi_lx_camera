# multi_lx_camera

Runs four MRDVS S10 cameras on one host:
- `camera_s10_front`
- `camera_s10_back`
- `camera_s10_left`
- `camera_s10_right`

The driver is forked from `lx_camera_ros`. The single-camera package is left untouched.

## What changed from lx_camera_ros

| lx_camera_ros | multi_lx_camera |
|---|---|
| frame ids hardcoded `mrdvs_tof` / `mrdvs_rgb` / `mrdvs_imu` | `<camera_name>_tof` / `_rgb` / `_imu`; `camera_name` defaults to the namespace |
| IMU publisher is a global, so the last node wins | IMU publisher is per node, found through the SDK callback's `usr_data` |
| constructor never returns (blocking `Run()` loop) | grab runs on an executor timer (`poll_rate`), so services and frame grabs are serialized |
| services held in constructor locals | services are members |
| `base_link -> mrdvs_tof` re-sent on `/tf` every frame | `parent_frame -> <name>_tof` and `<name>_tof -> <name>_rgb` sent once on `/tf_static` |
| `ip` shorter than 8 chars = open by list index | `ip` or `sn` required; waits for *that* camera; refuses to start if the opened device has a different IP/SN |
| `kill -9` / crash leaves a ~10 s device lock | Ctrl-C stops the stream and calls `DcCloseDevice` |
| `LX_INT_ALGORITHM_MODE=3` dereferenced a null publisher | `LxCamera_Location` is created |

Topic names, services and SDK parameter names are unchanged, under the camera's namespace. For example:
- `/camera_s10_front/LxCamera_Cloud`
- `/camera_s10_front/LxCamera_Depth`
- `/camera_s10_front/LxCamera_LxInt`

`lx_camera_ros/docs/S10_PARAMETERS.md` still applies.

## Layout

```
config/cameras.yaml      the cameras: name, ip/sn, mount pose, per-camera overrides
config/common.yaml       defaults shared by every camera node
launch/multi_camera.launch.py   one process per camera   (only:=a,b  cameras:=  rviz:=)
launch/single_camera.launch.py  one camera by name       (camera:=camera_s10_front)
launch/rviz.launch.py           rviz only, 4 clouds in base_link
scripts/discover.sh      list cameras on the network, flag duplicate IPs (exit 2)
scripts/set_ip.sh        set a camera's IP by MAC
scripts/net_tune.sh      host socket buffer / NIC ring tuning (sudo)
scripts/check_multi.py   per-camera Hz / latency / errors, pass-fail  (ros2 run)
tools/                   lx_discover / lx_setip sources (link the SDK directly, no ROS)
```

## 1. Network setup

**Topology**: 4× S10 → router/switch on gigabit ports → Jetson `enP2p1s0`.
- Use the router's LAN ports for both the cameras and the PC, not the WAN port.
- Every port must negotiate 1000 Mb/s. One 100 Mb/s port caps that camera.
- Disable DHCP on the router for this subnet, or keep its pool away from `.101`–`.104` and `.89`.
- Broadcast must pass between ports, because discovery is a UDP broadcast. If `discover.sh` sees nothing through the router, try a plain unmanaged gigabit switch instead.

**Host address**: static and on the same subnet as the cameras, for example `192.168.100.89/24`:

```bash
nmcli con mod <connection> ipv4.method manual ipv4.addresses 192.168.100.89/24
nmcli con up <connection>
```

**Give each camera a unique IP.** Every S10 ships on the same address, so connect them **one at a time**:

```bash
cd ~/ros2_jazzy_ws/src/multi_lx_camera
scripts/discover.sh                                  # note the MAC and SN
scripts/set_ip.sh <MAC> 192.168.100.101 255.255.255.0
# power-cycle the camera, run discover.sh again to confirm, then the next one
```

The first run of either script builds the tools.

| camera | IP |
|---|---|
| camera_s10_front | .101 |
| camera_s10_back | .102 |
| camera_s10_left | .103 |
| camera_s10_right | .104 |

Record each camera's MAC and SN in `config/cameras.yaml`. With all four powered, `scripts/discover.sh` must list 4 devices and exit 0.

If a camera is not reachable by `ping` after the change, its netmask or subnet does not match the host's.

**Host tuning** (not persistent; to make it permanent, put the sysctls in `/etc/sysctl.d/`):

```bash
sudo scripts/net_tune.sh enP2p1s0          # add --jumbo only if every hop supports MTU 9000
```

**Bandwidth.** All four streams share one 1 Gbit/s link.
- The S10 depth frame is small (240×160) and RGB travels as JPEG. Two cameras with RGB measured about 140 Mbit/s on this rig, so four S10s fit.
- Turn RGB on per camera and watch `check_multi.py` and `ip -s link show enP2p1s0` (RX drops) while you do.
- RGB is off by default in `common.yaml`. Enable it per camera with `LX_BOOL_ENABLE_2D_STREAM: 1` and `LX_INT_RGBD_ALIGN_MODE: 2`.

## 2. Configure

Edit `config/cameras.yaml`:
- `ip`, and `sn` (optional). If `sn` is set, the node refuses to start when the device it opens has a different SN.
- `pose`: the mount of `<name>_tof` in `base_link`. x/y/z are in **metres**; roll/pitch/yaw are in **degrees**.
  - The point cloud is configured in the robot convention: `LX_INT_XYZ_COORDINATE: 1`, x forward, y left, z up, in metres.
  - So `yaw: 90` means the camera looks to the robot's left.
  - The depth image is not affected by this setting and stays in the camera's own layout.
- `params`: any `common.yaml` key, overridden for this camera.

If the URDF already describes the camera mounts, set `publish_tf: false` so the frames don't get two parents.

## 3. Build

```bash
cd ~/ros2_jazzy_ws
colcon build --packages-select multi_lx_camera
source install/setup.bash
```

The SDK is loaded at runtime from `/opt/MRDVS/lib/libLxCameraApi.so`. Use `-DLX_SDK_ROOT=...` if it lives elsewhere.

## 4. Test, step by step

1. **Network**: `ping` .101–.104, then `scripts/discover.sh` → 4 devices, exit code 0.
2. **One camera at a time**:
   ```bash
   ros2 launch multi_lx_camera single_camera.launch.py camera:=camera_s10_front
   ros2 topic hz /camera_s10_front/LxCamera_Cloud
   ros2 topic echo --once --field header /camera_s10_front/LxCamera_Cloud   # frame_id: camera_s10_front_tof
   ```
   - Check that the "Open device success" banner shows the MAC you expect.
   - Repeat for the other three cameras.
3. **Two, then four**:
   ```bash
   ros2 launch multi_lx_camera multi_camera.launch.py only:=camera_s10_front,camera_s10_back
   ros2 launch multi_lx_camera multi_camera.launch.py rviz:=true
   ros2 node list          # 4 × /camera_s10_*/lx_camera
   ros2 run tf2_tools view_frames   # base_link -> 4 × camera_s10_*_tof
   ```
4. **Health, 5 minutes**:
   ```bash
   ros2 run multi_lx_camera check_multi.py --duration 300 --target-fps 15
   ```
   - Exit 0 means every camera held ≥ 95 % of the target rate.
   - Meanwhile, `watch -n1 ip -s link show enP2p1s0` should show no growing RX drops.
5. **Geometry**: in RViz, put an object where two cameras overlap and adjust each `pose` until the clouds coincide.
6. **Robustness**:
   - Unplug one camera: the other three keep streaming.
   - That camera's node logs `LX_CMD_GET_NEW_FRAME` errors until the SDK reconnects on its own after you replug it.
   - If it never recovers, Ctrl-C only that launch; `respawn` restarts nodes that crash.
   - Ctrl-C and relaunch immediately: there should be no `-9 LX_E_CTRL_PERMISS_ERROR`.
7. **Bandwidth**: turn RGB on one camera at a time and re-run step 4.

## Troubleshooting

| symptom | cause / fix |
|---|---|
| `Camera <ip> not found among N device(s)` forever | wrong IP in `cameras.yaml`, camera off, or broadcast blocked by the router; run `discover.sh` |
| `Opened X but expected Y (duplicate IP?)` | two cameras share an IP; re-address with `set_ip.sh` |
| `-9 LX_E_CTRL_PERMISS_ERROR` on open | something else holds the camera (e.g. an `lx_camera_ros` node), or a killed node's lock has not expired; wait ~10 s |
| `ros2 param get` hangs on a node | the node is still waiting for its camera, and parameters are served only after it opens; check the log |
| clouds offset or rotated in RViz | `pose` values, or `LX_INT_XYZ_COORDINATE` not 1 |
| depth with holes / ghost surfaces where views overlap | iToF interference; keep `LX_BOOL_ENABLE_MULTI_MACHINE: 1` |
| frame loss on all cameras | link saturated; lower `LX_INT_3D_FPS`, disable RGB/amp, check port speeds |

The linker warns about OpenCV 4.6 vs 4.8. This comes from `cv_bridge` (apt, 4.6) and JetPack's OpenCV (4.8). The existing `lx_camera_ros` binary links the same way.
