// -*- mode: c++ -*-
/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2017, JSK Lab
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/o2r other materials provided
 *     with the distribution.
 *   * Neither the name of the JSK Lab nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

#ifndef MOTION_DATA_REPLAY_H_
#define MOTION_DATA_REPLAY_H_

/* ros */
#include <ros/ros.h>
#include <pluginlib/class_loader.h>
#include <tf/transform_listener.h>

/* basic header */
#include <gap_passing/se2/motion_control.h>
#include <hydrus/transform_control.h>
#include <gap_passing/Keyposes.h>
#include <gap_passing/Endposes.h>


/* moveit for FCL and visualization */
#include <moveit/robot_model_loader/robot_model_loader.h>
#include <moveit/planning_interface/planning_interface.h>
#include <moveit/planning_scene/planning_scene.h>
#include <moveit/kinematic_constraints/utils.h>
#include <moveit_msgs/DisplayTrajectory.h>
#include <moveit_msgs/PlanningScene.h>
#include <std_msgs/Empty.h>
#include <std_msgs/Float64MultiArray.h>

/* ompl */
#include <ompl/control/SpaceInformation.h>
#include <ompl/base/StateStorage.h>
#include <ompl/base/spaces/SE2StateSpace.h>
#include <ompl/control/ODESolver.h>
#include <ompl/control/spaces/RealVectorControlSpace.h>
#include <ompl/control/SimpleSetup.h>
#include <ompl/config.h>
#include <ompl/geometric/PathGeometric.h>
#include <ompl/geometric/planners/rrt/RRTConnect.h>
#include <ompl/geometric/planners/sbl/SBL.h>
#include <ompl/geometric/planners/kpiece/LBKPIECE1.h>
#include <ompl/geometric/planners/rrt/RRTstar.h>
#include <ompl/geometric/SimpleSetup.h>
#include <ompl/base/spaces/DubinsStateSpace.h>
#include <ompl/base/DiscreteMotionValidator.h>
#include <ompl/base/samplers/ObstacleBasedValidStateSampler.h>
#include <ompl/base/objectives/PathLengthOptimizationObjective.h>
#include <ompl/base/objectives/StateCostIntegralObjective.h>

/* experiment data replay */
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/JointState.h>

/* utils */
#include <iostream>
#include <valarray>
#include <limits>
#include <vector>
#include <sstream>

namespace se2
{
  class MotionDataReplay
  {

  public:
    MotionDataReplay(ros::NodeHandle nh, ros::NodeHandle nhp);
    ~MotionDataReplay();

    inline tf::Vector3 world2GapCoord(tf::Vector3 world_info)
    {
      return tf::Vector3(-world_info.getY(), world_info.getX(), world_info.getZ());
    }
    inline tf::Vector3 gap2WorldCoord(tf::Vector3 gap_coord_info)
    {
      return tf::Vector3(gap_coord_info.getY(),-gap_coord_info.getX(), gap_coord_info.getZ());

    }

    static const int RRT_START_MODE = 0;
    static const int LBKPIECE1_MODE = 1;

    static const int NO_OVERLAP = 0;
    static const int Y_AXIS_OVERLAP = 1; //gap coord
    static const int X_AXIS_OVERLAP = 2; //gap coord

  private:

    ros::NodeHandle nh_;
    ros::NodeHandle nhp_;
    ros::Publisher planning_scene_diff_pub_;
    ros::Subscriber real_robot_state_sub_;
    ros::Publisher  flight_nav_;
    ros::Subscriber robot_move_start_sub_;
    ros::Subscriber desired_state_sub_;

    boost::shared_ptr<TransformController> transform_controller_;

    /* simulator flag */
    bool simulator_;

    //*** optimation objective
    ompl::base::PathLengthOptimizationObjective* path_length_opt_objective_;

    //*** planning control
    MotionControl* motion_control_;

    robot_model_loader::RobotModelLoader *robot_model_loader_;
    robot_model::RobotModelPtr kinematic_model_;
    planning_scene::PlanningScene* planning_scene_;
    //planning_scene::PlanningScenePtr planning_scene_;
    moveit_msgs::CollisionObject collision_object_;
    moveit_msgs::PlanningScene planning_scene_msg_;
    collision_detection::AllowedCollisionMatrix acm_;

    double  motion_sequence_rate_;
    ros::Timer motion_sequence_timer_;
    float calculation_time_;

    double tolerance_;

    ros::ServiceServer keyposes_server_;
    std::vector<std::vector<double> > keyposes_cog_vec_;
    ros::ServiceClient endposes_client_;

    //real robot 
    bool real_robot_move_base_;
    bool get_init_state_;
    double mocap_center_to_joint_x_, mocap_center_to_joint_y_;

    //gap env
    //int gap_overlap_type_;
    double gap_left_x_, gap_left_y_;
    double gap_x_offset_;
    double gap_y_offset_;
    double gap_left_width_;
    double gap_right_width_;
    tf::Vector3 left_half_corner;
    tf::Vector3 right_half_corner;

    //*** experiment data replay
    bool experiment_odom_recv_flag_;
    bool experiment_joint_states_recv_flag_;
    bool replay_experiment_data_flag_;
    planning_scene::PlanningScene* experiment_scene_;
    moveit_msgs::PlanningScene experiment_scene_msg_;
    ros::Publisher experiment_scene_diff_pub_;
    ros::Subscriber experiment_robot_cog_odom_sub_;
    ros::Subscriber experiment_robot_joint_states_sub_;
    nav_msgs::Odometry experiment_robot_cog_odom_;
    sensor_msgs::JointState experiment_robot_joint_states_;
    void experimentRobotOdomCallback(const nav_msgs::OdometryConstPtr& msg);
    void experimentRobotJointStatesCallback(const sensor_msgs::JointStateConstPtr& joint_msg);

    //original planning
    std::vector<double> start_state_;
    std::vector<double> goal_state_;

    double state_validity_check_res_;
    int valid_segment_count_factor_;

    ompl::base::StateSpacePtr config_space_;
    ompl::base::SpaceInformationPtr space_information_;
    ompl::base::PathPtr path_;
    ompl::geometric::RRTstar* rrt_start_planner_;
    bool solved_;
    double solving_time_limit_;

    //visualization
    ompl::base::StateStorage* plan_states_;
    int planning_mode_;
    int ompl_mode_;

    double stability_cost_thre_;
    double length_cost_thre_;
    int semi_stable_states_;

    int joint_num_;

    double length_opt_weight_;
    double stability_opt_weight_;

    bool play_log_path_;//if true, use file, if false, get form realtime thing

    double best_cost_;

    bool move_start_flag_;

    std::vector<double> deisred_state_;

    void motionSequenceFunc(const ros::TimerEvent &e);

    bool isStateValid(const ompl::base::State *state);

    std::vector<double> cog2root(std::vector<double> &keypose); // transfer cog based keypose to root based
    std::vector<double> root2cog(std::vector<double> &keypose); // transfer root based keypose to cog based

    void rosParamInit();

    void gapEnvInit();
    void Planning();
    ompl::base::Cost onlyJointPathLimit();

    ompl::base::ValidStateSamplerPtr allocValidStateSampler(const ompl::base::SpaceInformation *si)
    {
      return ompl::base::ValidStateSamplerPtr(new ompl::base::ObstacleBasedValidStateSampler(si));
    }
    bool getKeyposes(gap_passing::Keyposes::Request &req, gap_passing::Keyposes::Response &res);

    void moveStartCallback(const std_msgs::Empty msg);
    void desiredStateCallback(const std_msgs::Float64MultiArrayConstPtr& msg);
  };

};
#endif
