"""Start one camera from cameras.yaml, for bring-up and debugging.

    ros2 launch multi_lx_camera single_camera.launch.py camera:=camera_s10_front
"""

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.substitutions import LaunchConfiguration
import os


def generate_launch_description():
    share = get_package_share_directory('multi_lx_camera')
    return LaunchDescription([
        DeclareLaunchArgument('camera', description='camera name from cameras.yaml'),
        DeclareLaunchArgument(
            'cameras', default_value=os.path.join(share, 'config', 'cameras.yaml')),
        DeclareLaunchArgument('rviz', default_value='false'),
        IncludeLaunchDescription(
            os.path.join(share, 'launch', 'multi_camera.launch.py'),
            launch_arguments={
                'only': LaunchConfiguration('camera'),
                'cameras': LaunchConfiguration('cameras'),
                'rviz': LaunchConfiguration('rviz'),
            }.items()),
    ])
