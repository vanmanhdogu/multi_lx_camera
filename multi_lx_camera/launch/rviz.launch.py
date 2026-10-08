"""RViz with all four clouds in base_link, without starting any driver."""

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node
import os


def generate_launch_description():
    share = get_package_share_directory('multi_lx_camera')
    return LaunchDescription([
        Node(package='rviz2', executable='rviz2', name='multi_lx_camera_rviz',
             arguments=['-d', os.path.join(share, 'rviz', 'multi_camera.rviz')]),
    ])
