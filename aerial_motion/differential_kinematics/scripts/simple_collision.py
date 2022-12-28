#!/usr/bin/env python

import sys
import time
import rospy
import math
from visualization_msgs.msg import MarkerArray, Marker
import pandas as pd

if __name__ == "__main__":

    rospy.init_node("simple_collision")
    file = rospy.get_param('~obstacle_world_file')
    if file is None:
        rospy.logerr("no valid obsracle world file")
    df = pd.read_csv(file, header=None)

    obj_type = rospy.get_param("~type", 3) # 3: Sphere
    # position_x = rospy.get_param("~position_x", 0)
    # position_y = rospy.get_param("~position_y", 0)
    # scale_x = rospy.get_param("~scale_x", 0.1)
    # scale_y = rospy.get_param("~scale_y", 0.1)
    # scale_z = rospy.get_param("~scale_z", 0.1)

    pub = rospy.Publisher("/env_collision", MarkerArray, queue_size=10)

    time.sleep(1)
    env_obj = MarkerArray()
    for i in range(len(df)):
        env_obj.markers.append(Marker())
        env_obj.markers[i].id = i
        env_obj.markers[i].header.frame_id = "world"
        env_obj.markers[i].type = obj_type
        env_obj.markers[i].action = 0
        env_obj.markers[i].pose.position.x = df.loc[i, 1]
        env_obj.markers[i].pose.position.y = df.loc[i, 2]
        env_obj.markers[i].pose.position.z = 0
        env_obj.markers[i].pose.orientation.w = 1
        env_obj.markers[i].scale.x = df.loc[i, 8]
        env_obj.markers[i].scale.y = df.loc[i, 8]
        env_obj.markers[i].scale.z = 0.3
        env_obj.markers[i].color.r = 1
        env_obj.markers[i].color.a = 1

    while not rospy.is_shutdown():

        env_obj.markers[0].header.stamp = rospy.get_rostime()

        pub.publish(env_obj)
        time.sleep(0.1)
