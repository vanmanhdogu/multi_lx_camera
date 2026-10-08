# Network setup for the multi-camera rig (2 cameras now, 4 later)

This guide takes the rig from "cameras plugged in" to "every camera has its
own address and streams reliably". It is written in two phases:

- **Phase A — two cameras.** Bring up `camera_s10_front` and `camera_s10_back`
  and validate the whole chain.
- **Phase B — four cameras.** Add `camera_s10_left` and `camera_s10_right`
  without disturbing the first two.

Every command assumes you start in the package directory:

```bash
cd ~/ros2_jazzy_ws/src/multi_lx_camera
```

`README.md` covers the driver, configuration and launch files. This document
covers only the network, plus the checks that show the network is good.

---

## 0. Where the rig is today (2026-10-08)

Measured on this Jetson before writing this guide:

| item | value |
|---|---|
| camera NIC | `enP2p1s0`, MAC `4c:bb:47:13:c6:17` |
| NetworkManager profile on it | `mrdvs` (manual, `192.168.100.89/16`, no gateway, `never-default yes`) |
| link | 2500 Mb/s full duplex to the switch/router |
| Wi-Fi (internet) | `wlP1p1s0`, `192.168.77.204/24`, holds the default route |
| socket buffers | `rmem_max` = 10 MB, `netdev_max_backlog` = 1000 (not tuned yet) |

`scripts/discover.sh` currently finds two S10 cameras:

| SN | MAC | IP now | firmware |
|---|---|---|---|
| `C492A8C03314107` | `02:3d:f9:61:85:07` | 192.168.100.83 | V1.1.000000_260124_V0.0.8 |
| `C492A8C02D00107` | `02:80:b3:9a:0c:10` | 192.168.100.82 | V1.1.000000_260626_V0.1.7 |

Both have netmask `255.255.255.0` and gateway `192.168.100.1`.

They already have different IPs, so they can run together today. They are not
yet on the target plan below, though. Section 4 moves them there.

> **Factory default address.** Every S10 we have unboxed arrived on
> `192.168.100.82`. Assume each new camera arrives on `.82`, and that two new
> cameras will clash with each other. Section 5 explains how to handle that.

---

## 1. Target design

### 1.1 Topology

```
                   ┌──────────────────────────┐
 camera_s10_front ─┤                          │
 camera_s10_back  ─┤  gigabit switch / router │── enP2p1s0 (Jetson, 192.168.100.89)
 camera_s10_left  ─┤  (LAN ports only)        │
 camera_s10_right ─┤                          │
                   └──────────────────────────┘
   internet stays on Wi-Fi (wlP1p1s0); the camera LAN has no gateway
```

- Use **one flat Layer-2 segment**: every camera and the Jetson on the same
  switch or the same router LAN, with no VLANs between them.
- Camera discovery uses **UDP broadcast**. A device that filters broadcast
  between ports (some routers in "AP isolation" or "port isolation" mode) hides
  the cameras from `discover.sh` and from the driver. If you suspect this,
  swap in a plain **unmanaged gigabit switch**. That is the simplest and most
  predictable option.
- On a router, use **LAN ports only**. The WAN port is a different subnet and
  blocks broadcast.

### 1.2 Address plan

| device | IP | netmask | gateway | note |
|---|---|---|---|---|
| Jetson `enP2p1s0` | 192.168.100.89 | 255.255.255.0 (`/24`) | none | static |
| `camera_s10_front` | 192.168.100.101 | 255.255.255.0 | 192.168.100.1 | phase A |
| `camera_s10_back` | 192.168.100.102 | 255.255.255.0 | 192.168.100.1 | phase A |
| `camera_s10_left` | 192.168.100.103 | 255.255.255.0 | 192.168.100.1 | phase B |
| `camera_s10_right` | 192.168.100.104 | 255.255.255.0 | 192.168.100.1 | phase B |
| *(reserved)* | 192.168.100.82 | | | factory default; keep free so a new camera never clashes with a configured one |

Rules:

- **Never leave a configured camera on `.82`.** If you do, the next new camera
  you plug in clashes with it.
- If the router runs DHCP, either disable it on this LAN or shrink its pool so
  that it cannot hand out `.82`, `.89` or `.101`–`.104`.
- The gateway written into the cameras (`.1`) is never used, because nothing
  routes off this LAN. `set_ip.sh` fills it in by default, and that is fine.

### 1.3 Hardware checklist

- [ ] The switch has a **gigabit (or faster) port for every device**, with at
      least 5 ports for 4 cameras plus the Jetson. Spare ports are useful for a
      laptop during debugging.
- [ ] Cables are **Cat5e or better**. Cat5 or a damaged cable often
      negotiates only 100 Mb/s, which caps that one camera.
- [ ] Every camera has its own stable power supply, and the switch is powered
      separately from the cameras.
- [ ] Each camera has a **physical label with its MAC and its role** (front,
      back, …). You will need this as soon as you have four identical boxes.

---

## 2. Configure the Jetson NIC

### 2.1 Switch the host from `/16` to `/24`

Today the `mrdvs` profile uses `192.168.100.89/16`. This installs the route
`192.168.0.0/16 dev enP2p1s0`, which captures **every** `192.168.x.x` address
that is not more specifically routed elsewhere. As a result, any other
`192.168.*` network you later reach (a second Wi-Fi network, a VPN, a
customer's LAN) would be sent to the camera switch. A `/24` limits the route
to the camera subnet:

```bash
nmcli con mod mrdvs ipv4.method manual \
    ipv4.addresses 192.168.100.89/24 \
    ipv4.gateway "" \
    ipv4.never-default yes \
    ipv6.method disabled \
    connection.autoconnect yes \
    connection.interface-name enP2p1s0
nmcli con up mrdvs
```

Check the result:

```bash
ip -br addr show enP2p1s0        # 192.168.100.89/24
ip route | grep enP2p1s0         # 192.168.100.0/24 dev enP2p1s0 ... (and no "default via")
ip route get 8.8.8.8             # must still go out wlP1p1s0
```

If you prefer to keep `/16`, the cameras still work, because all of them sit
inside it. The `/24` change is about keeping other networks out of the camera
NIC, not about making the cameras work.

### 2.2 Remove stale profiles

NetworkManager has several old wired profiles that could claim this NIC
instead of `mrdvs`: `mrdv-cam`, `mrdv-cams`, `mrdvs-cam`, a second `mrdvs`,
`Wired connection 3/4`, and `netplan-NM-…`. If one of them autoconnects after
a reboot, the Jetson may come up with a different address and the cameras will
"disappear".

```bash
nmcli -f NAME,UUID,DEVICE,AUTOCONNECT con show | grep -Ei 'mrdv|wired|netplan'
```

Keep the **active** `mrdvs` profile, which is the one whose `DEVICE` column
shows `enP2p1s0`. For each of the others, either delete it or stop it from
autoconnecting. Use the UUID, because two profiles share the name `mrdvs`:

```bash
nmcli con mod <uuid> connection.autoconnect no     # or: nmcli con delete <uuid>
```

Do not touch `Wired connection 1/2` on `mgbe0_0` / `mgbe1_0` if anything else
uses those ports.

### 2.3 Check the link speed

```bash
ethtool enP2p1s0 | grep -E 'Speed|Duplex|Link detected'
# Speed: 2500Mb/s   Duplex: Full   Link detected: yes   (today)
```

The host link must be **≥ 1000 Mb/s full duplex**. `ethtool` sees only the
host-to-switch link. For the camera-to-switch links, look at the port LEDs on
the switch (usually a different colour for 1000 Mb/s) or at the router's
port-status page.

### 2.4 Kernel and NIC tuning

Four cameras send bursts at the same moment. The default receive buffers are
small enough to drop packets under that load.

**Try it (lost on reboot):**

```bash
sudo scripts/net_tune.sh enP2p1s0
```

This sets `rmem_max`/`rmem_default` = 32 MB and `netdev_max_backlog` = 10000,
raises the NIC RX ring to its maximum, and prints the link status.

**Make it permanent** once the rig is stable:

```bash
sudo tee /etc/sysctl.d/60-mrdvs-cameras.conf >/dev/null <<'EOF'
# multi_lx_camera: several GigE cameras on enP2p1s0
net.core.rmem_max = 33554432
net.core.rmem_default = 33554432
net.core.netdev_max_backlog = 10000
EOF
sudo sysctl --system | grep -E 'rmem|backlog'
```

To make the RX ring permanent, first read the maximum, then store it in the
profile (NetworkManager ≥ 1.26 supports this, and the Jetson has 1.46):

```bash
sudo ethtool -g enP2p1s0                  # "Pre-set maximums" -> RX: N
nmcli con mod mrdvs ethtool.ring-rx N
nmcli con up mrdvs
```

**Jumbo frames (MTU 9000): leave them off.** They help only if the Jetson
NIC, every switch port and every camera all support them, and we have not
verified that for the S10. The load is low enough that standard MTU 1500 is
sufficient (section 6).

---

## 3. Tools

| command | what it does |
|---|---|
| `scripts/discover.sh` | Broadcasts, then lists every MRDVS camera: IP, MAC, SN, firmware, netmask. Exit code **0** = all IPs distinct, **2** = IP conflict, **1** = SDK error. It works even when a camera is on a different subnet from the host. |
| `scripts/set_ip.sh <MAC> <IP> [mask] [gw]` | Writes a new static IP into the camera with that MAC. It **refuses** if the device it reaches is not that MAC (the duplicate-IP case). The new address takes effect **only after a power cycle**. |
| `sudo scripts/net_tune.sh [iface]` | The tuning from section 2.4. |
| `ros2 run multi_lx_camera check_multi.py` | Per camera: cloud and depth Hz, latency, temperature, and error count, with a pass/fail result. |

The first run of `discover.sh` or `set_ip.sh` compiles the helpers in
`tools/`.

The SDK writes `log/lx_camera.log` into the **current directory**, so run
these tools from a directory where that does not matter.

---

## 4. Phase A — two cameras

Goal: `camera_s10_front` = `.101` and `camera_s10_back` = `.102`, both
streaming at the target rate with no drops.

### A1. Decide which physical camera is which

Pick the camera that will be **front** and the one that will be **back**. Use
the MAC on its label, or plug them in one at a time and run `discover.sh`.
Write the result down:

| role | MAC | SN | old IP → new IP |
|---|---|---|---|
| front | `__:__:__:__:__:__` | | → 192.168.100.101 |
| back  | `__:__:__:__:__:__` | | → 192.168.100.102 |

(Today the two units are `02:3d:f9:61:85:07` / SN `C492A8C03314107` and
`02:80:b3:9a:0c:10` / SN `C492A8C02D00107`.)

### A2. Stop anything that uses the cameras

`set_ip.sh` needs exclusive access. Stop every `lx_camera_ros` /
`multi_lx_camera` node, then wait about 10 s so that the device lock expires:

```bash
pgrep -af lx_camera_node || echo "nothing running"
```

### A3. Re-address the cameras

The two units already have different IPs, so they can stay connected
together while you do this. `set_ip.sh` selects each camera by MAC and checks
that it reached the right one.

```bash
scripts/discover.sh                                     # confirm: 2 devices, exit 0

scripts/set_ip.sh <FRONT_MAC> 192.168.100.101 255.255.255.0
scripts/set_ip.sh <BACK_MAC>  192.168.100.102 255.255.255.0
```

Each call must end with `OK. Power-cycle this camera…`. If you see
`!! ABORTING: asked for MAC X but the network handed us Y`, two cameras share
an IP. Unplug one of them and repeat for the camera that is left.

**Power-cycle both cameras.** Unplug the power, not the Ethernet cable, wait
about 5 s, then plug the power back in. Give them about 20–30 s to boot.

### A4. Verify the addresses

```bash
scripts/discover.sh; echo "exit=$?"
```

Expected:

- `2 MRDVS device(s) found`
- `.101` with the front MAC and `.102` with the back MAC
- `netmask : 255.255.255.0` on both
- `All 2 cameras have distinct IPs`, `exit=0`

Then check reachability and that each IP maps to exactly one MAC:

```bash
ping -c3 192.168.100.101
ping -c3 192.168.100.102
ip neigh show dev enP2p1s0 | grep -E '\.10[1-4] '   # one line per IP, MAC matches the label
```

If `discover.sh` lists a camera but `ping` fails, the camera's subnet or
netmask does not match the host's. Re-run `set_ip.sh` with the correct values.

### A5. Record the cameras in `config/cameras.yaml`

```yaml
  - name: camera_s10_front
    ip: 192.168.100.101
    sn: "<FRONT_SN>"          # optional, but the node then refuses a wrong camera
    mac: "<FRONT_MAC>"        # documentation only; the driver does not read it
  - name: camera_s10_back
    ip: 192.168.100.102
    sn: "<BACK_SN>"
    mac: "<BACK_MAC>"
```

Leave `left` and `right` as they are. `only:=` keeps them from starting.

Rebuild so that the installed copy of the YAML is updated, unless you built
with `--symlink-install`:

```bash
cd ~/ros2_jazzy_ws && colcon build --packages-select multi_lx_camera && source install/setup.bash
```

### A6. Tune the host

```bash
sudo scripts/net_tune.sh enP2p1s0
```

(Or use the permanent version from section 2.4.)

### A7. One camera at a time

```bash
ros2 launch multi_lx_camera single_camera.launch.py camera:=camera_s10_front
```

In another terminal:

```bash
ros2 topic hz /camera_s10_front/LxCamera_Cloud          # ≈ 15 Hz (LX_INT_3D_FPS)
ros2 topic echo --once --field header.frame_id /camera_s10_front/LxCamera_Cloud
```

- The `Open device success` banner must show the **front MAC**.
- `frame_id` must be `camera_s10_front_tof`.

Stop the launch with Ctrl-C and repeat with `camera:=camera_s10_back`.

### A8. Both cameras together

```bash
ros2 launch multi_lx_camera multi_camera.launch.py \
    only:=camera_s10_front,camera_s10_back rviz:=true
```

In a second terminal, run a 5-minute health check:

```bash
ros2 run multi_lx_camera check_multi.py --duration 300 --target-fps 15 \
    --cameras camera_s10_front camera_s10_back
echo "exit=$?"        # 0 = both cameras held ≥ 95 % of 15 Hz
```

In a third terminal, watch the NIC counters during the same 5 minutes:

```bash
watch -n1 'ip -s link show enP2p1s0'
```

The RX `dropped`, `missed` and `errors` counters must **not grow** while the
cameras stream.

### A9. Measure the bandwidth

This gives the real per-camera load, which is the number you need for the
four-camera budget:

```bash
IF=enP2p1s0; a=$(cat /sys/class/net/$IF/statistics/rx_bytes); sleep 10; \
b=$(cat /sys/class/net/$IF/statistics/rx_bytes); \
echo "RX $(( (b-a)*8/10/1000000 )) Mbit/s"
```

Measure three times: with front only, with back only, and with both. Note the
results in the table in section 6.

Then turn RGB on for one camera (`LX_BOOL_ENABLE_2D_STREAM: 1` and
`LX_INT_RGBD_ALIGN_MODE: 2` under its `params:`), rebuild, relaunch, and
measure again. Earlier, two cameras with RGB measured about **140 Mbit/s** on
this rig.

### A10. Robustness checks

- **Unplug the back camera's Ethernet cable.** Front must keep streaming at
  its full rate. Back logs `LX_CMD_GET_NEW_FRAME` errors. Plug the cable back
  in: back must recover by itself.
- **Power-cycle one camera** while both nodes run. The same behaviour is
  expected.
- **Ctrl-C the launch and start it again immediately.** There must be no
  `-9 LX_E_CTRL_PERMISS_ERROR`.
- **Reboot the Jetson.** Afterwards `ip -br addr show enP2p1s0` must still
  show `192.168.100.89/24`, and `discover.sh` must find both cameras without
  any manual step.

**Phase A is done when** A4, A8 (exit 0, no growing drops), A9 and A10 all
pass.

---

## 5. Phase B — four cameras

Goal: add `camera_s10_left` = `.103` and `camera_s10_right` = `.104`.

### B1. Why the new cameras go in one at a time

New cameras arrive on `192.168.100.82`. With phase A finished, nothing else
uses `.82`, so **one** new camera can join the running network without a
conflict. **Two** new cameras would both be on `.82`. When that happens, the
SDK opens whichever one answers ARP first, and `set_ip.sh` will either abort
or, if you skipped its check, re-address the wrong unit.

So the rule is: **connect one new camera, re-address it, power-cycle it,
confirm, then connect the next one.** Front and back can stay connected
throughout.

### B2. Add the left camera

1. Stop all camera nodes (as in A2).
2. Connect **only** the new left camera to the switch and power it on.
3. Run discovery:
   ```bash
   scripts/discover.sh; echo "exit=$?"
   ```
   You should see 3 devices: `.101`, `.102`, and a new one (normally `.82`).
   Note its MAC and SN, and check them against the label.

   If the exit code is 2, the new camera clashes with something. Unplug
   everything except the new camera and continue from step 4 with only that
   camera connected.
4. Re-address it:
   ```bash
   scripts/set_ip.sh <LEFT_MAC> 192.168.100.103 255.255.255.0
   ```
5. Power-cycle the left camera, wait 30 s, then confirm:
   ```bash
   scripts/discover.sh     # 3 devices: .101 .102 .103, exit 0
   ping -c3 192.168.100.103
   ```

### B3. Add the right camera

Repeat B2 for the right camera, using `192.168.100.104`. At the end:

```bash
scripts/discover.sh; echo "exit=$?"
# 4 MRDVS device(s) found, .101 .102 .103 .104, all netmask 255.255.255.0
# All 4 cameras have distinct IPs ... exit=0
for i in 101 102 103 104; do ping -c2 -W1 192.168.100.$i >/dev/null && echo ".$i ok" || echo ".$i FAIL"; done
```

### B4. Update `config/cameras.yaml` and rebuild

Fill in `sn` and `mac` for `camera_s10_left` and `camera_s10_right`, check
their `ip` values, then rebuild as in A5.

### B5. Bring the system up in steps

Do not start all four at once on the first try. Add one camera per step, so
that if something breaks you know which camera did it:

```bash
# each new camera on its own first
ros2 launch multi_lx_camera single_camera.launch.py camera:=camera_s10_left
ros2 launch multi_lx_camera single_camera.launch.py camera:=camera_s10_right

# then 3, then 4
ros2 launch multi_lx_camera multi_camera.launch.py \
    only:=camera_s10_front,camera_s10_back,camera_s10_left
ros2 launch multi_lx_camera multi_camera.launch.py rviz:=true
```

With all four running:

```bash
ros2 node list | grep lx_camera            # 4 nodes
ros2 run multi_lx_camera check_multi.py --duration 300 --target-fps 15
echo "exit=$?"
watch -n1 'ip -s link show enP2p1s0'      # RX drops must stay flat
```

### B6. Bandwidth and robustness, again

- Repeat the bandwidth measurement from A9 with all four cameras running, then
  again with RGB on whichever cameras actually need it, enabling them one at a
  time.
- Repeat A10 (unplug one, power-cycle one, quick relaunch, reboot), each time
  with four cameras running. When one camera is unplugged, the **other three**
  must keep their full rate.

**Phase B is done when** `discover.sh` lists 4 devices with exit 0,
`check_multi.py` passes for 5 minutes with all four cameras, the RX drop
counters stay flat, and the robustness checks pass.

---

## 6. Bandwidth budget

All camera traffic converges on **one** link: switch → `enP2p1s0`. Today that
link runs at 2.5 Gb/s. Each camera's own link is 1 Gb/s.

Plan to keep the total **below about 60 % of the slowest link on the path**.
That leaves headroom for bursts, because all the cameras tend to send a frame
at the same moment. If the camera switch is gigabit-only, the budget is about
600 Mbit/s.

Fill this in from A9 and B6:

| configuration | measured RX (Mbit/s) | check_multi pass? | RX drops? |
|---|---|---|---|
| front only, depth | | | |
| front + back, depth | | | |
| front + back, + RGB on one | | | |
| 4 cameras, depth | | | |
| 4 cameras, + RGB on ___ | | | |

If you go over budget or see drops, in this order:

1. Turn off streams that nobody uses: `LX_BOOL_ENABLE_2D_STREAM` and
   `LX_BOOL_ENABLE_3D_AMP_STREAM`.
2. Lower `LX_INT_3D_FPS` on the cameras that need it least.
3. Make sure every port negotiated 1000 Mb/s (section 2.3).
4. Only then consider better hardware: a better switch, or a second NIC and a
   second camera subnet.

---

## 7. Troubleshooting (network)

| symptom | likely cause | what to do |
|---|---|---|
| `discover.sh` finds 0 devices | camera not powered or still booting; broadcast blocked; wrong NIC | Wait 30 s after power-on. Run `ip -br link` and check that `enP2p1s0` is UP. Try an unmanaged switch, or connect one camera directly to the Jetson. |
| `discover.sh` lists the camera but `ping` fails | camera on a different subnet or netmask from the host | `set_ip.sh <MAC> 192.168.100.10x 255.255.255.0`, then power-cycle |
| `discover.sh` exit 2, `IP CONFLICT` | two cameras on the same IP (usually two new units on `.82`) | Unplug all but one camera, re-address it, power-cycle it, repeat |
| `set_ip.sh` prints `ABORTING … network handed us …` | same as above | same as above |
| `set_ip.sh` prints `MAC … is not in the device list` | typo, or the camera is not visible | Copy the MAC from `discover.sh` output |
| new IP does not appear after `set_ip.sh` | camera not power-cycled | Remove power, not just the Ethernet cable |
| node logs `Camera <ip> not found among N device(s)` forever | IP in `cameras.yaml` differs from the real one; or the install copy of the YAML is stale | Compare with `discover.sh`; rebuild |
| node logs `Opened X but expected Y` | duplicate IP, or a wrong `sn` in the YAML | `discover.sh`, then fix |
| `-9 LX_E_CTRL_PERMISS_ERROR` | another process holds the camera (an old node, `set_ip.sh`, the vendor viewer) or a lock left by a killed node | `pgrep -af lx_camera`, stop it, wait about 10 s |
| one camera's rate is low, the others are fine | that port or cable negotiated 100 Mb/s; bad cable | Check the switch LEDs, swap the cable, swap the port |
| every camera drops frames together | shared link saturated, or host buffers too small | Section 6; `sudo scripts/net_tune.sh`; watch `ip -s link` |
| cameras gone after a reboot | another NM profile took `enP2p1s0`, so the host has a different IP | `ip -br addr show enP2p1s0`; section 2.2 |
| internet stops working once the camera LAN is up | a gateway is set on the `mrdvs` profile | `nmcli con mod mrdvs ipv4.gateway "" ipv4.never-default yes` |

---

## 8. Final record

Fill this in once phase B is done, and keep it in sync with
`config/cameras.yaml` and the labels on the cameras.

| role | IP | MAC | SN | firmware | switch port | cable label |
|---|---|---|---|---|---|---|
| front | 192.168.100.101 | | | | | |
| back  | 192.168.100.102 | | | | | |
| left  | 192.168.100.103 | | | | | |
| right | 192.168.100.104 | | | | | |
| Jetson `enP2p1s0` | 192.168.100.89/24 | 4c:bb:47:13:c6:17 | — | — | | |
