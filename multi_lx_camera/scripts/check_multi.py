#!/usr/bin/env python3
"""Per-camera health report for multi_lx_camera.

    ros2 run multi_lx_camera check_multi.py
    ros2 run multi_lx_camera check_multi.py --duration 300 --target-fps 15 \
        --cameras camera_s10_front camera_s10_back

Every --period seconds it prints, per camera: cloud and depth rate, the age of
the newest cloud (receive time minus header stamp), the camera's own reported
depth fps and temperature, and the number of LxCamera_Error messages. With
--duration it exits after that many seconds, 0 if every camera held
--min-ratio of --target-fps on its cloud topic, else 1.
"""

import argparse
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Image, PointCloud2
from std_msgs.msg import String

from multi_lx_camera.msg import FrameRate

DEFAULT_CAMERAS = ['camera_s10_front', 'camera_s10_back',
                   'camera_s10_left', 'camera_s10_right']


class CameraStats:
    def __init__(self):
        self.cloud = 0
        self.depth = 0
        self.errors = 0
        self.total_cloud = 0
        self.last_age = None
        self.last_error = ''
        self.reported_fps = None
        self.temperature = None


class CheckMulti(Node):
    def __init__(self, cameras):
        super().__init__('check_multi')
        self.stats = {c: CameraStats() for c in cameras}
        for cam in cameras:
            st = self.stats[cam]
            self.create_subscription(
                PointCloud2, f'/{cam}/LxCamera_Cloud',
                lambda m, st=st: self.on_cloud(st, m), qos_profile_sensor_data)
            self.create_subscription(
                Image, f'/{cam}/LxCamera_Depth',
                lambda m, st=st: setattr(st, 'depth', st.depth + 1),
                qos_profile_sensor_data)
            self.create_subscription(
                FrameRate, f'/{cam}/LxCamera_FrameRate',
                lambda m, st=st: self.on_rate(st, m), qos_profile_sensor_data)
            self.create_subscription(
                String, f'/{cam}/LxCamera_Error',
                lambda m, st=st: self.on_error(st, m), 10)

    def on_cloud(self, st, msg):
        st.cloud += 1
        st.total_cloud += 1
        stamp = msg.header.stamp.sec + msg.header.stamp.nanosec * 1e-9
        st.last_age = time.time() - stamp

    @staticmethod
    def on_rate(st, msg):
        st.reported_fps = msg.depth
        st.temperature = msg.temperature

    @staticmethod
    def on_error(st, msg):
        st.errors += 1
        st.last_error = msg.data

    def report(self, period):
        print(f'\n{"camera":<20}{"cloud Hz":>9}{"depth Hz":>9}{"age ms":>9}'
              f'{"cam fps":>9}{"temp C":>8}{"errors":>8}')
        for cam, st in self.stats.items():
            age = f'{st.last_age * 1e3:.0f}' if st.last_age is not None else '-'
            fps = f'{st.reported_fps:.1f}' if st.reported_fps is not None else '-'
            temp = f'{st.temperature:.1f}' if st.temperature is not None else '-'
            print(f'{cam:<20}{st.cloud / period:>9.1f}{st.depth / period:>9.1f}'
                  f'{age:>9}{fps:>9}{temp:>8}{st.errors:>8}')
            if st.last_error:
                print(f'    last error: {st.last_error}')
            st.cloud = st.depth = 0
            st.last_error = ''


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cameras', nargs='+', default=DEFAULT_CAMERAS)
    parser.add_argument('--period', type=float, default=5.0)
    parser.add_argument('--duration', type=float, default=0.0,
                        help='seconds to run; 0 = until Ctrl-C')
    parser.add_argument('--target-fps', type=float, default=15.0)
    parser.add_argument('--min-ratio', type=float, default=0.95)
    args, ros_args = parser.parse_known_args()

    rclpy.init(args=ros_args)
    node = CheckMulti(args.cameras)
    start = time.monotonic()
    next_report = start + args.period
    try:
        while rclpy.ok():
            rclpy.spin_once(node, timeout_sec=0.1)
            now = time.monotonic()
            if now >= next_report:
                node.report(args.period)
                next_report += args.period
            if args.duration and now - start >= args.duration:
                break
    except KeyboardInterrupt:
        pass

    elapsed = time.monotonic() - start
    ok = True
    if args.duration:
        print(f'\n==== summary over {elapsed:.0f} s, target {args.target_fps} Hz ====')
        for cam, st in node.stats.items():
            rate = st.total_cloud / elapsed
            passed = rate >= args.target_fps * args.min_ratio
            ok &= passed
            print(f'{cam:<20}{rate:>7.1f} Hz  {"PASS" if passed else "FAIL"}')
    node.destroy_node()
    rclpy.try_shutdown()
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
