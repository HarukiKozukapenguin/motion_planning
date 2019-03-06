// -*- mode: c++ -*-
/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2018, JSK Lab
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

#include <squeeze_navigation/planner/base_plugin.h>

#include <differential_kinematics/planner_core.h>
#include <aerial_motion_planning_msgs/multilink_state.h>

#include <pluginlib/class_loader.h>
/* special cost plugin for cartesian constraint */
#include <differential_kinematics/cost/cartesian_constraint.h>
/* special constraint plugin for collision avoidance */
#include <differential_kinematics/constraint/collision_avoidance.h>

/* utils */
#include <tf_conversions/tf_kdl.h>
#include <fnmatch.h>

enum Phase
  {
    CASE1,
    CASE2_1,
    CASE2_2,
    CASE3,
  };
namespace squeeze_motion_planner
{
  using namespace differential_kinematics;
  using CostContainer = std::vector<boost::shared_ptr<cost::Base> >;
  using ConstraintContainer = std::vector<boost::shared_ptr<constraint::Base> >;

  class DifferentialKinematics: public Base
  {
  public:
    DifferentialKinematics(): path_(0) {}
    ~DifferentialKinematics(){}

    void initialize(ros::NodeHandle nh, ros::NodeHandle nhp, boost::shared_ptr<HydrusRobotModel> robot_model_ptr)
    {
      Base::initialize(nh, nhp, robot_model_ptr);

      /* setup the planner core */
      planner_core_ptr_ = boost::shared_ptr<Planner> (new Planner(nh, nhp, robot_model_ptr_));
      planner_core_ptr_->registerUpdateFunc(std::bind(&DifferentialKinematics::updatePinchPoint, this));

      /* base vars */
      phase_ = CASE1;
      reference_point_ratio_ = 1.0;

      nhp_.param("delta_pinch_length", delta_pinch_length_, 0.02); // [m]
      nhp_.param("debug", debug_, true);

      /* set start pose */
      geometry_msgs::Pose pose;
      nhp_.param("start_state_x", pose.position.x, 0.0);
      nhp_.param("start_state_y", pose.position.y, 0.5);
      nhp_.param("start_state_z", pose.position.z, 0.0);
      double r, p, y;
      nhp_.param("start_state_roll", r, 0.0);
      nhp_.param("start_state_pitch", p, 0.0);
      nhp_.param("start_state_yaw", y, 0.0);
      pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(r,p,y);
      KDL::JntArray actuator_state(robot_model_ptr_->getActuatorMap().size());
      for(int i = 0; i < robot_model_ptr_->getLinkJointNames().size(); i++)
        nhp_.param(std::string("start_") + robot_model_ptr_->getLinkJointNames().at(i), actuator_state(robot_model_ptr_->getLinkJointIndex().at(i)), 0.0);
      start_state_.setStatesFromRoot(robot_model_ptr_, pose, actuator_state);

      /* set goal pose */
      nhp_.param("goal_state_x", pose.position.x, 0.0);
      nhp_.param("goal_state_y", pose.position.y, 0.5);
      nhp_.param("goal_state_z", pose.position.z, 0.0);
      nhp_.param("goal_state_roll", r, 0.0);
      nhp_.param("goal_state_pitch", p, 0.0);
      nhp_.param("goal_state_yaw", y, 0.0);
      pose.orientation = tf::createQuaternionMsgFromRollPitchYaw(r,p,y);
      for(int i = 0; i < robot_model_ptr_->getLinkJointNames().size(); i++)
        nhp_.param(std::string("goal_") + robot_model_ptr_->getLinkJointNames().at(i), actuator_state(robot_model_ptr_->getLinkJointIndex().at(i)), 0.0);
      goal_state_.setStatesFromRoot(robot_model_ptr_, pose, actuator_state);

      ROS_WARN("model: %d", planner_core_ptr_->getMultilinkType());

      /* hard-coding to set env */
      double wall_thickness;
      nhp_.param("wall_thickness", wall_thickness, 0.05);
      if(planner_core_ptr_->getMultilinkType() == motion_type::SE2)
        {
          ROS_WARN("correct model");
          /* setup env */
          double openning_width, env_width, env_length;
          nhp_.param("openning_width", openning_width, 0.8);
          nhp_.param("env_width", env_width, 4.0);
          nhp_.param("env_length", env_length, 6.0);
          /* openning side wall(s) */
          visualization_msgs::Marker wall;
          wall.type = visualization_msgs::Marker::CUBE;
          wall.action = visualization_msgs::Marker::ADD;
          wall.header.frame_id = "/world";
          wall.color.g = 1;
          wall.color.a = 1;
          wall.pose.orientation.w = 1;
          wall.scale.z = 2;

          wall.id = 1;
          wall.pose.position.x = 0;
          wall.pose.position.y = -env_width / 2;
          wall.pose.position.z = 0;
          wall.scale.x = env_length;
          wall.scale.y = wall_thickness;
          env_collision_.markers.push_back(wall);

          wall.id = 2;
          wall.pose.position.y = env_width / 2;
          env_collision_.markers.push_back(wall);

          wall.id = 3;
          wall.pose.position.x = 0;
          wall.pose.position.y = env_width / 4 + openning_width / 4;
          wall.pose.position.z = 0;
          wall.scale.x = wall_thickness;
          wall.scale.y = env_width / 2 - openning_width / 2;
          env_collision_.markers.push_back(wall);

          wall.id = 4;
          wall.pose.position.y = -env_width / 4 - openning_width / 4;
          env_collision_.markers.push_back(wall);

          openning_center_frame_.setOrigin(tf::Vector3(0, 0, 0));
          openning_center_frame_.setRotation(tf::createQuaternionFromRPY(0, M_PI / 2, 0)); // the head should be z axis
        }
      else if(planner_core_ptr_->getMultilinkType() == motion_type::SE3)
        {
          /* setup env */
          double openning_width, openning_height, env_width, env_length, ceiling_offset;
          nhp_.param("openning_width", openning_width, 0.8);
          nhp_.param("openning_height", openning_height, 0.8);
          nhp_.param("ceiling_offset", ceiling_offset, 0.6);
          nhp_.param("env_width", env_width, 6.0);
          /* openning side wall(s) */
          visualization_msgs::Marker wall;
          wall.type = visualization_msgs::Marker::CUBE;
          wall.action = visualization_msgs::Marker::ADD;
          wall.header.frame_id = "/world";
          wall.color.g = 1;
          wall.color.a = 1;
          wall.pose.orientation.w = 1;
          wall.scale.z = wall_thickness;
          wall.pose.position.z = openning_height;

          wall.id = 1;
          wall.pose.position.x = env_width / 4 + openning_width / 4;
          wall.pose.position.y = 0;
          wall.scale.x = env_width / 2 - openning_width / 2;
          wall.scale.y = env_width;
          env_collision_.markers.push_back(wall);

          wall.id = 2;
          wall.pose.position.x = -wall.pose.position.x;
          env_collision_.markers.push_back(wall);

          wall.id = 3;
          wall.pose.position.x = 0;
          wall.pose.position.y = env_width / 4 + openning_width / 4;
          wall.scale.x = openning_width;
          wall.scale.y = env_width / 2 - openning_width / 2;
          env_collision_.markers.push_back(wall);

          wall.id = 4;
          wall.pose.position.y = -wall.pose.position.y;
          env_collision_.markers.push_back(wall);

          wall.id = 5;
          wall.pose.position.x = 0;
          wall.pose.position.y = 0;
          wall.scale.x = env_width;
          wall.scale.y = env_width;
          wall.pose.position.z = openning_height + ceiling_offset; // + 0.5
          env_collision_.markers.push_back(wall);

          openning_center_frame_.setOrigin(tf::Vector3(0, 0, openning_height));
          openning_center_frame_.setRotation(tf::createQuaternionFromRPY(0, 0, 0));
        }
      env_collision_pub_ = nh_.advertise<visualization_msgs::MarkerArray>("/env_collision", 1);
    };

    const std::vector<MultilinkState>& getPathConst() const { return path_; }
    const MultilinkState& getStateConst(int index) const { path_.at(index); }

    bool plan(bool debug)
    {
      /* reset */
      path_.clear();

      /* declare the differential kinemtiacs const */
      pluginlib::ClassLoader<cost::Base>  cost_plugin_loader("differential_kinematics", "differential_kinematics::cost::Base");
      CostContainer cost_container;
      /* 1. statevel */
      cost_container.push_back(cost_plugin_loader.createInstance("differential_kinematics_cost/state_vel"));
      cost_container.back()->initialize(nh_, nhp_, planner_core_ptr_, "differential_kinematics_cost/state_vel", true /* orientation */, true /* full_body */);
      /* 2. reference point cartesian error constraint (cost) */
      cost_container.push_back(cost_plugin_loader.createInstance("differential_kinematics_cost/cartesian_constraint"));
      cost_container.back()->initialize(nh_, nhp_, planner_core_ptr_, "differential_kinematics_cost/cartesian_constraint", false /* orientation */, true /* full_body */);
      cartersian_constraint_ = boost::dynamic_pointer_cast<cost::CartersianConstraint>(cost_container.back());
      /* defualt reference point is the openning center */
      tf::Transform target_frame = openning_center_frame_;
      target_frame.setOrigin(target_frame.getOrigin() + openning_center_frame_.getBasis() * tf::Vector3(0, 0, delta_pinch_length_));
      cartersian_constraint_->updateTargetFrame(target_frame);
      ROS_WARN("target frame:[%f, %f, %f]", target_frame.getOrigin().x(),
               target_frame.getOrigin().y(), target_frame.getOrigin().z());

      /* declare the differential kinemtiacs constraint */
      pluginlib::ClassLoader<constraint::Base>  constraint_plugin_loader("differential_kinematics", "differential_kinematics::constraint::Base");
      ConstraintContainer constraint_container;
      /* 1.  state_limit */
      constraint_container.push_back(constraint_plugin_loader.createInstance("differential_kinematics_constraint/state_limit"));
      constraint_container.back()->initialize(nh_, nhp_, planner_core_ptr_, "differential_kinematics_constraint/state_limit", true /* orientation */, true /* full_body */);
      /* 2.  stability */
      constraint_container.push_back(constraint_plugin_loader.createInstance("differential_kinematics_constraint/stability"));
      constraint_container.back()->initialize(nh_, nhp_, planner_core_ptr_, "differential_kinematics_constraint/stability", true /* orientation */, true /* full_body */);
      /* 3. collision avoidance */
      constraint_container.push_back(constraint_plugin_loader.createInstance("differential_kinematics_constraint/collision_avoidance"));
      constraint_container.back()->initialize(nh_, nhp_, planner_core_ptr_, "differential_kinematics_constraint/collision_avoidance", true /* orientation */, true /* full_body */);
      boost::dynamic_pointer_cast<constraint::CollisionAvoidance>(constraint_container.back())->setEnv(env_collision_);

      /* 4. additional plugins for cost and constraint, if necessary */
      auto pattern_match = [&](std::string &pl, std::string &pl_candidate) -> bool
        {
          int cmp = fnmatch(pl.c_str(), pl_candidate.c_str(), FNM_CASEFOLD);
          if (cmp == 0)
            return true;
          else if (cmp != FNM_NOMATCH) {
            // never see that, i think that it is fatal error.
            ROS_FATAL("Plugin list check error! fnmatch('%s', '%s', FNM_CASEFOLD) -> %d",
                      pl.c_str(), pl_candidate.c_str(), cmp);
          }
          return false;
        };

      ros::V_string additional_constraint_list{};
      nhp_.getParam("additional_constraint_list", additional_constraint_list);
      for (auto &plugin_name : additional_constraint_list)
        {
          for (auto &name : constraint_plugin_loader.getDeclaredClasses())
            {
              if(!pattern_match(plugin_name, name)) continue;

              constraint_container.push_back(constraint_plugin_loader.createInstance(name));
              constraint_container.back()->initialize(nh_, nhp_, planner_core_ptr_, name, true, true);
              break;
            }
        }

      /* reset the init joint(actuator) state the init root pose for planner */
      tf::Transform root_pose;
      tf::poseMsgToTF(start_state_.getRootPoseConst(), root_pose);
      planner_core_ptr_->setTargetRootPose(root_pose);
      planner_core_ptr_->setTargetActuatorVector(start_state_.getActuatorStateConst());

      /* init the pinch point, shouch be the end point of end link */
      updatePinchPoint();

      /* start the planning */
      if(planner_core_ptr_->solver(cost_container, constraint_container, debug))
        {
          /* set the correct base link ( which is not root_link = link1), to be suitable for the control system */
          std::string base_link;
          nhp_.param("baselink", base_link, std::string("link1"));
          robot_model_ptr_->setBaselinkName(base_link);

          for(int index = 0; index < planner_core_ptr_->getRootPoseSequence().size(); index++)
            {

              geometry_msgs::Pose root_pose;
              tf::poseTFToMsg(planner_core_ptr_->getRootPoseSequence().at(index), root_pose);
              path_.push_back(MultilinkState(robot_model_ptr_, root_pose, planner_core_ptr_->getActuatorStateSequence().at(index)));
           }

          /* Temporary: add several extra point after squeezing */
          /* simply only change the target altitude of the CoG frame */
          for(int i = 1; i <= 10 ; i++) // 5 times
            {
              MultilinkState robot_state;
              geometry_msgs::Pose root_pose;
              tf::poseTFToMsg(planner_core_ptr_->getRootPoseSequence().back(), root_pose);
              root_pose.position.z += (0.005 * i); //0.005 [m]
              robot_state.setStatesFromRoot(robot_model_ptr_, root_pose,
                                            planner_core_ptr_->getActuatorStateSequence().back());
              path_.push_back(robot_state);
            }
          ROS_WARN("total path length: %d", (int)path_.size());

          return true;
        }
      else return false;
    }

    bool loadPath() { return false;}

    void checkCollision(MultilinkState state) {}

    void visualizeFunc()
    {
      /* collsion publishment */
      env_collision_pub_.publish(env_collision_);
    }

  private:

    ros::Publisher env_collision_pub_;

    boost::shared_ptr<Planner> planner_core_ptr_;

    /* path */
    MultilinkState start_state_;
    MultilinkState goal_state_;
    std::vector<MultilinkState> path_;

    tf::Transform openning_center_frame_;

    double delta_pinch_length_; //to propagate the pinch action

    bool debug_;
    Phase phase_;
    double reference_point_ratio_;

    boost::shared_ptr<cost::CartersianConstraint> cartersian_constraint_;

    visualization_msgs::MarkerArray env_collision_;

    bool updatePinchPoint()
    {
      /* case 1: total not pass: use the head */
      /* case 2: half pass: calcualte the pass point */
      /* case 3: final phase: the root link is close to the openning_center gap => finish */

      /* case3 */
      if((openning_center_frame_.inverse() * planner_core_ptr_->getTargetRootPose()).getOrigin().z() > -0.01)
        {
          if(phase_ < CASE2_2)
            {
              ROS_ERROR("CurrentPhase: %d, wrong phase:%d, ", phase_, CASE1);
              return false;
            }

          if(debug_) ROS_INFO("case 3");
          /* convergence */
          ROS_WARN("complete passing regarding to the cartersian constraint");
          //cartersian_constraint_->updateTargetFrame(planner_core_ptr_->getTargetRootPose());
          cartersian_constraint_->updateChain("root", std::string("link1"), KDL::Segment(std::string("pinch_point"), KDL::Joint(KDL::Joint::None), KDL::Frame(KDL::Vector(0, 0, 0))));
          return true;
        }

      /* update robot model (maybe duplicated, but necessary) */
      KDL::Rotation root_att;
      tf::quaternionTFToKDL(planner_core_ptr_->getTargetRootPose().getRotation(), root_att);
      robot_model_ptr_->setCogDesireOrientation(root_att);
      robot_model_ptr_->updateRobotModel(planner_core_ptr_->getTargetActuatorVector<KDL::JntArray>());

      /* fullFK */
      auto full_fk_result = planner_core_ptr_->getRobotModelPtr()->fullForwardKinematics(planner_core_ptr_->getTargetActuatorVector<KDL::JntArray>());

      tf::Transform previous_link_tf = planner_core_ptr_->getTargetRootPose();
      double previous_z;

      for(int index = 1; index <= robot_model_ptr_->getRotorNum(); index++)
        {
          tf::Transform current_link_tf;
          tf::transformKDLToTF(full_fk_result.at(std::string("link") + std::to_string(index)), current_link_tf);
          double current_z = (openning_center_frame_.inverse() * planner_core_ptr_->getTargetRootPose() * current_link_tf).getOrigin().z();
          if(current_z > 0)
            {
              /* case 2.2 */
              if(phase_ > CASE2_2 || phase_ == CASE1)
                {
                  ROS_ERROR("CurrentPhase: %d, wrong phase:%d, ", phase_, CASE1);
                  return false;
                }

              if(phase_ == CASE2_1) reference_point_ratio_ = 1;
              phase_ = CASE2_2;
              //if(debug_) ROS_INFO("case 2.2");
              assert(index > 1);

              double new_ratio = fabs(previous_z) / (fabs(previous_z) + current_z);
              if(debug_) ROS_INFO("case 2.2, current_z: %f, previous_z: %f, new_ratio: %f, reference_point_ratio_: %f", current_z, previous_z, new_ratio, reference_point_ratio_);
              if(new_ratio < reference_point_ratio_) reference_point_ratio_ = new_ratio;

              cartersian_constraint_->updateChain("root", std::string("link") + std::to_string(index-1), KDL::Segment(std::string("pinch_point"), KDL::Joint(KDL::Joint::None), KDL::Frame(KDL::Vector(robot_model_ptr_->getLinkLength() * new_ratio , 0, 0))));
              //if(debug_) ROS_WARN("under: link%d, z: %f; upper: link%d, z: %f", index -1, previous_z, index, current_z);
              return true;
            }
          previous_link_tf = current_link_tf;
          previous_z = current_z;
        }

      /* head of the robot */
      tf::Transform head_tf; head_tf.setOrigin(tf::Vector3(robot_model_ptr_->getLinkLength(), 0, 0));
      double head_z = (openning_center_frame_.inverse() * planner_core_ptr_->getTargetRootPose() * previous_link_tf * head_tf).getOrigin().z();

      //if(debug_) ROS_INFO("head_z: %f", head_z);

      if (head_z < 0)  /* case 1 */
        {
          if(phase_ > CASE1)
            {
              ROS_ERROR("CurrentPhase: %d, wrong phase:%d, ", phase_, CASE1);
              return false;
            }
          if(debug_) ROS_INFO("case 1, the head does not pass");

          cartersian_constraint_->updateChain("root", std::string("link") + std::to_string(robot_model_ptr_->getRotorNum()), KDL::Segment(std::string("pinch_point"), KDL::Joint(KDL::Joint::None), KDL::Frame(KDL::Vector(robot_model_ptr_->getLinkLength(), 0, 0))));
        }
      else /* case 2.1 */
        {
          if(phase_ > CASE2_1)
            {
              ROS_ERROR("CurrentPhase: %d, wrong phase:%d, ", phase_, CASE1);
              return false;
            }
          phase_ = CASE2_1;

          double new_ratio = fabs(previous_z) / (fabs(previous_z) + head_z);
          if(debug_) ROS_INFO("case 2.1, head_z: %f, previous_z: %f, new_ratio: %f, reference_point_ratio_: %f", head_z, previous_z, new_ratio, reference_point_ratio_);
          if(new_ratio < reference_point_ratio_) reference_point_ratio_ = new_ratio;

          cartersian_constraint_->updateChain("root", std::string("link") + std::to_string(robot_model_ptr_->getRotorNum()), KDL::Segment(std::string("pinch_point"), KDL::Joint(KDL::Joint::None), KDL::Frame(KDL::Vector(robot_model_ptr_->getLinkLength() * reference_point_ratio_, 0, 0))));
        }

      return true;
    }

  };
};

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(squeeze_motion_planner::DifferentialKinematics, squeeze_motion_planner::Base);
