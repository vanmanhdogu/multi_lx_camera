"""Start one lx_camera_node process per camera listed in config/cameras.yaml.

    ros2 launch multi_lx_camera multi_camera.launch.py
    ros2 launch multi_lx_camera multi_camera.launch.py only:=camera_s10_front,camera_s10_back
    ros2 launch multi_lx_camera multi_camera.launch.py cameras:=/path/to/my_cameras.yaml rviz:=true

One process per camera, not a component container: the vendor SDK takes an
exclusive per-device lock and keeps some process-wide settings, so separate
processes keep a crash or hang in one camera from taking the others down.
"""

import os

import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

POSE_KEYS = ('x', 'y', 'z', 'roll', 'pitch', 'yaw')


def load_cameras(path, only):
    with open(path) as f:
        cameras = (yaml.safe_load(f) or {}).get('cameras', [])
    names = [c['name'] for c in cameras]
    if len(names) != len(set(names)):
        raise RuntimeError(f'{path}: camera names must be unique')
    ips = [c.get('ip') for c in cameras if c.get('ip')]
    if len(ips) != len(set(ips)):
        raise RuntimeError(f'{path}: camera IPs must be unique')
    if only:
        wanted = [n.strip() for n in only.split(',') if n.strip()]
        unknown = set(wanted) - set(names)
        if unknown:
            raise RuntimeError(f'only:= names not in {path}: {sorted(unknown)}')
        cameras = [c for c in cameras if c['name'] in wanted]
    return cameras


def camera_node(cam, common_yaml):
    pose = cam.get('pose') or {}
    params = {
        'camera_name': cam['name'],
        'ip': str(cam.get('ip') or ''),
        'sn': str(cam.get('sn') or ''),
    }
    # The driver declares the pose as floats; a YAML `0` would be an int and
    # be rejected, so coerce here.
    params.update({k: float(pose.get(k, 0.0)) for k in POSE_KEYS})
    params.update(cam.get('params') or {})
    return Node(
        package='multi_lx_camera',
        executable='lx_camera_node',
        name='lx_camera',
        namespace=cam['name'],
        output='screen',
        emulate_tty=True,
        respawn=True,
        respawn_delay=5.0,
        parameters=[common_yaml, params],
    )


def launch_setup(context):
    share = get_package_share_directory('multi_lx_camera')
    cameras_yaml = LaunchConfiguration('cameras').perform(context)
    common_yaml = LaunchConfiguration('common').perform(context)
    only = LaunchConfiguration('only').perform(context)

    actions = [camera_node(c, common_yaml)
               for c in load_cameras(cameras_yaml, only)]
    actions.append(Node(
        package='rviz2', executable='rviz2', name='multi_lx_camera_rviz',
        arguments=['-d', os.path.join(share, 'rviz', 'multi_camera.rviz')],
        condition=IfCondition(LaunchConfiguration('rviz'))))
    return actions


def generate_launch_description():
    share = get_package_share_directory('multi_lx_camera')
    return LaunchDescription([
        DeclareLaunchArgument(
            'cameras', default_value=os.path.join(share, 'config', 'cameras.yaml'),
            description='YAML listing the cameras'),
        DeclareLaunchArgument(
            'common', default_value=os.path.join(share, 'config', 'common.yaml'),
            description='ROS parameter file shared by all camera nodes'),
        DeclareLaunchArgument(
            'only', default_value='',
            description='Comma-separated camera names to start (default: all)'),
        DeclareLaunchArgument('rviz', default_value='false'),
        OpaqueFunction(function=launch_setup),
    ])
