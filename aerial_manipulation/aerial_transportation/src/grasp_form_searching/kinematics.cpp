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

#include <aerial_transportation/grasp_form_searching/grasp_form_search.h>

using namespace Eigen;

namespace grasp_form_search
{
  namespace
  {
    double joint_angle_limit_ = 0; /* the limit of joint angle */
    double contact_range_thresh_; /* the threshold for surface contact distance */
    bool kinematics_verbose_ = false;
  }

  void GraspFormSearch::kinematicsInit()
  {
    nhp_.param("kinematics_verbose", kinematics_verbose_, false);
    if(verbose_) std::cout << "[kinematics] verbose: " << kinematics_verbose_ << std::endl;
    nhp_.param("joint_angle_limit", joint_angle_limit_, 1.65);
    if(verbose_) std::cout << "[kinematics] joint_angle_limit: " << joint_angle_limit_ << std::endl;

    /* special for convex polygonal */
    nhp_.param("contact_range_thresh", contact_range_thresh_, 0.03); // [m], the surface contact between duct and object.
    if(verbose_) std::cout << "[kinematics] contact_range_thresh: " << contact_range_thresh_ << std::endl;

  }

  bool GraspFormSearch::cylinderInitEnvelopCheck(int& contact_num)
  {
    /*
      note that: we roughly calculate the contact number with cylinder,
      in the condition that the each link is perpendicular with contact point normal line.
    */

    /* the angle between the lines of neighbour contact points to centers */
    if(link_length_ == 0 || duct_radius_ == 0)
      {
        ROS_WARN("[cylinderInitEnvelopCheck]: invalid link length: %f, and duct radius: %f", link_length_, duct_radius_);
        return false;
      }

    double psi = atan2(link_length_ /2, duct_radius_ + object_radius_) * 2;
    int link_num = uav_kinematics_->getRotorNum();

    /* check whether the link num is enough */
    if(psi * (link_num -1 ) <= M_PI)
      {
        ROS_WARN("[cylinderInitEnvelopCheck]: the link_num of link_lenght is too short to envelope the whole cylinder, psi * (link_num -1 ): %f", psi * (link_num -1 ));
        return false;
      }

    contact_num = link_num;

   ROS_INFO("[cylinderInitEnvelopCheck]: contact_num: %d, psi: %f", contact_num_, psi);

    /* check whether the link num exceed the enveloping condition */
    double exceed_angle = psi * link_num - 2 * M_PI;
    if(exceed_angle > 1e-6)
      {
        contact_num -= ((int)exceed_angle / (int)psi + 1);
        ROS_WARN("[cylinderInitEnvelopCheck]: the link_num exceeds the whole cylinder, contact_num: %d", contact_num);
      }

    return true;
  }

  bool GraspFormSearch::convexPolygonNeighbourContacts(double curr_contact_d, double curr_phi, double curr_psi, double curr_side_len, double next_side_len,  double& theta /* joint_angle */, double& next_phi, Vector3d& joint_p, double& next_contact_d)
  {
    /* note: */
    /* the origin is the current vertex,  different with 2017 IJRR/ICRA (contact p is origin) */
    /* contact_d is different with paper(2017 ICRA, IJRR), contact_d + d_paper = side_length */

    /* check the enough surface contact */
    if(curr_contact_d < contact_range_thresh_ / 2 || (curr_side_len - curr_contact_d) < contact_range_thresh_ /2 )
      {
        if(kinematics_verbose_) ROS_WARN("[convexPolygonNeighbourContacts]: the current contact d is not enough for the surface contact, contact_d, side length and contact rage  is [%f, %f, %f] ", curr_contact_d, curr_side_len, contact_range_thresh_);
        return false;
      }

    auto jointColiisionCheck = [this](double curr_contact_d, double curr_phi, double curr_side_len) -> bool {
      /* check the current joint is out of the Convex polygon  */
      if(curr_phi > 0 &&
         -duct_radius_ + link_length_ / 2 * sin(curr_phi) > 0 &&
         (duct_radius_ / (curr_side_len - curr_contact_d )) < tan(curr_phi))
        return false;

      if(curr_phi < 0 &&
         -duct_radius_ + link_length_ / 2 * sin(-curr_phi) > 0 &&
         (duct_radius_ / curr_contact_d) < tan(-curr_phi))
        return false;

      return true;
    };

    if(!jointColiisionCheck(curr_contact_d, curr_phi, curr_side_len))
      {
        if(kinematics_verbose_) ROS_WARN("[convexPolygonNeighbourContacts]: the current joint is inside the convex polygon, phi  and contact_d, side length  is [%f, %f, %f] ", curr_phi,  curr_contact_d, curr_side_len);
        return false;
      }


    /* joint position based on current side */
    joint_p = Vector3d(curr_contact_d, -duct_radius_, 0) +  AngleAxisd(curr_phi, Vector3d::UnitZ()) * Vector3d(link_length_ / 2, 0, 0) ; // P_c_i -> P_l_i -> P_j_i


    double sin_psi = sin(curr_psi);
    double cos_psi = cos(curr_psi);
    double gamma = (sin_psi * (-duct_radius_ * sin_psi + joint_p(0) - curr_side_len) - duct_radius_ * cos_psi * cos_psi - joint_p(1) * cos_psi) / (link_length_ / 2);

    if(fabs(gamma) >= 1)
      {
        if(kinematics_verbose_)
          ROS_WARN("[convexPolygonNeighbourContacts] joint calc: joint angle(gamma) is not valid, gamma is %f", gamma);
        return false;
      }

    next_phi = asin(gamma);
    theta = next_phi - curr_phi + curr_psi;

    /* check the validation of joint angle */
    if(theta <= 0 || fabs(theta) >= joint_angle_limit_ || theta + curr_phi < 1.0e-6)
      {//case1: negative angle means no envelop, case2: angle limitation, case3: no contact with real side
        if(kinematics_verbose_)
          ROS_WARN("[convexPolygonNeighbourContacts] joint calc: joint angle is not valid, angle is %f", theta);
        return false;
      }

    Vector3d next_contact_p = joint_p + AngleAxisd(theta + curr_phi, Vector3d::UnitZ()) * Vector3d(link_length_ / 2, 0, 0) + AngleAxisd(curr_psi + M_PI/2, Vector3d::UnitZ()) * Vector3d(duct_radius_, 0, 0);

    /* check the y of the contact point whether is possitive */
    if(next_contact_p(1) < 1e-6)
      {
        if(kinematics_verbose_)
          ROS_WARN("[convexPolygonNeighbourContacts] next_contact: the contact point is not valid, contact point y below 0: %f", next_contact_p(1));
        return false;
      }

    next_contact_d = (next_contact_p - Vector3d(curr_side_len, 0, 0)).norm();

    /* check the validation of contact point */
    if(next_contact_d <= 0)
      {
        if(kinematics_verbose_)
          ROS_WARN("[convexPolygonNeighbourContacts] next_contact_d: %f, next_side_len: %f", next_contact_d, next_side_len) ;
        return false;
      }
    /* check the enough surface contact */
    if(next_contact_d < contact_range_thresh_ / 2 || (next_side_len - next_contact_d) < contact_range_thresh_ /2 )
      {
        if(kinematics_verbose_)
          ROS_WARN("[convexPolygonNeighbourContacts]: the nextent contact d is not enough for the surface contact, contact_d, side length and contact rage  is [%f, %f, %f] ", next_contact_d, next_side_len, contact_range_thresh_);
        return false;
      }

    if(!jointColiisionCheck(next_contact_d, next_phi, next_side_len))
      {
        if(kinematics_verbose_) ROS_WARN("[convexPolygonNeighbourContacts]: the nextent joint is inside the convex polygon, phi  and contact_d, side len is [%f, %f, %f] ", next_phi,  next_contact_d, next_side_len);
        return false;
      }

    return true;
  }

  bool GraspFormSearch::envelopingCalc(double first_contact_d, double first_phi, std::vector<double>& v_theta, std::vector<double>& v_phi, std::vector<double>& v_contact_d, std::vector<Vector3d>& v_contact_p, std::vector<Quaterniond>& v_contact_rot, std::vector<Vector3d>& v_joint_p, int start_link, int start_side /* convex */)
  {
    /* note: */
    /* 1. the first element of v_joint is the root of uav */
    /* 2. contact_d is different with paper(2017 ICRA, IJRR), contact_d + d_paper = side_length */

    v_theta.clear();
    v_phi.clear();
    v_contact_d.clear();
    v_contact_p.clear();
    v_joint_p.clear();
    v_contact_rot.clear();

    Vector3d joint_p; /* local */
    Vector3d next_contact_p;

    if(object_type_ == aerial_transportation::ObjectConfigure::Request::UNKNOWN)
      {
        if(kinematics_verbose_) ROS_WARN("[envelopingCalc]: invalid object type: %d", object_type_);
        return false;
      }

    switch(object_type_)
      {
      case aerial_transportation::ObjectConfigure::Request::CONVEX_POLYGONAL_COLUMN:
        {
          assert(start_link < contact_num_);

          double curr_phi = first_phi;
          double curr_contact_d = first_contact_d;

          v_theta.resize(contact_num_ - 1);
          v_phi.resize(contact_num_);
          v_contact_d.resize(contact_num_);
          v_contact_p.resize(contact_num_);
          v_joint_p.resize(contact_num_);
          v_contact_rot.resize(contact_num_);

          // v_contact_d.push_back(curr_contact_d);
          // v_phi.push_back(curr_phi_d);
          // change to two seperated process, center to end, and center to begin

          /* form start_link */
          for(int i = start_link; i < contact_num_; i ++)
            {
              int index = (i + start_side) % object_.size();

              v_phi.at(i) = curr_phi;
              v_contact_d.at(i) = curr_contact_d;

              v_contact_p.at(i) = (object_.at(index)->vertex_p_ +
                                   object_.at(index)->contact_rot_ * Vector3d(curr_contact_d, 0, 0));
              v_contact_rot.at(i) = object_.at(index)->contact_rot_;
              v_joint_p.at(i) = v_contact_p.at(i) + object_.at(index)->contact_rot_ * (Vector3d(0, -duct_radius_, 0) + AngleAxisd(curr_phi, Vector3d::UnitZ()) * Vector3d(-link_length_ / 2, 0, 0));

              if(kinematics_verbose_)
                ROS_INFO("[envelopingCalc]: convex, joint%d p: [%f, %f], contact%d p: [%f, %f], phi: %f, contact_d: %f, rot: [%f, %f, %f, %f]", i, v_joint_p.at(i)(0), v_joint_p.at(i)(1), i + 1, v_contact_p.at(i)(0), v_contact_p.at(i)(1), curr_phi, curr_contact_d, object_.at(index)->contact_rot_.x(), object_.at(index)->contact_rot_.y(), object_.at(index)->contact_rot_.z(), object_.at(index)->contact_rot_.w());

              if(i == contact_num_ -1) break;
              double theta;
              double next_phi ;
              double next_contact_d;

              if(!convexPolygonNeighbourContacts(curr_contact_d, curr_phi, object_.at((index + 1) % object_.size())->psi_, object_.at(index)->len_, object_.at((index + 1) % object_.size())->len_, theta , next_phi, joint_p, next_contact_d))
                {
                  if(kinematics_verbose_)
                    ROS_WARN("[envelopingCalc]: fail to calculate the enveloping for convex at joint%d", i + 1);
                  return false;
                }

              if(kinematics_verbose_)
                ROS_INFO("[envelopingCalc]: joint%d, angle: %f", i + 1,theta);

              curr_phi = next_phi;
              curr_contact_d = next_contact_d;
              v_theta.at(i) = theta;
            }

          curr_phi = first_phi;
          curr_contact_d = first_contact_d;

          for(int i = start_link; i > 0; i--)
            {
              int index = (i + start_side) % object_.size();
              assert(index > 0);

              double theta;
              double next_reverse_phi ;
              double next_contact_opposite_d;

              if(!convexPolygonNeighbourContacts(object_.at(index)->len_ - curr_contact_d, -curr_phi, object_.at(index)->psi_, object_.at(index)->len_, object_.at((index - 1) % object_.size())->len_, theta , next_reverse_phi, joint_p, next_contact_opposite_d))
                {
                  if(kinematics_verbose_)
                    ROS_WARN("[envelopingCalc]: fail to calculate the enveloping for convex at joint%d", i + 1);
                  return false;
                }

              if(kinematics_verbose_)
                ROS_INFO("[envelopingCalc]: joint%d, angle: %f", i,theta);

              curr_phi = -next_reverse_phi;
              curr_contact_d = object_.at((index - 1) % object_.size())->len_ - next_contact_opposite_d;
              v_theta.at(i-1) = theta;
              v_phi.at(i-1) = curr_phi;
              v_contact_d.at(i-1) = curr_contact_d;

              v_contact_p.at(i-1) = object_.at(index-1)->vertex_p_ + object_.at(index-1)->contact_rot_ * Vector3d(curr_contact_d, 0, 0);

              v_contact_rot.at(i-1) = object_.at(index-1)->contact_rot_;
              v_joint_p.at(i-1) = v_contact_p.at(i-1) +
                object_.at(index-1)->contact_rot_ * (Vector3d(0, -duct_radius_, 0) + AngleAxisd(curr_phi, Vector3d::UnitZ()) * Vector3d(-link_length_ / 2, 0, 0));

              if(kinematics_verbose_)
                ROS_INFO("[envelopingCalc]: convex, joint%d p: [%f, %f], contact%d p: [%f, %f]", i-1, v_joint_p.at(i-1)(0), v_joint_p.at(i-1)(1), i, v_contact_p.at(i-1)(0), v_contact_p.at(i-1)(1));

            }
          break;
        }
      case aerial_transportation::ObjectConfigure::Request::CYLINDER:
        {
          v_phi.push_back(first_phi);

          double abs_psi = 0;

          /* distance from center to each joint point */
          double d_1 = sqrt(link_length_ * link_length_ / 4 + (duct_radius_ + object_radius_) * (duct_radius_ + object_radius_) - link_length_ * (duct_radius_ + object_radius_) * cos(M_PI / 2 + first_phi));
          double d_2 = sqrt(link_length_ * link_length_ / 4 + (duct_radius_ + object_radius_) * (duct_radius_ + object_radius_) - link_length_ * (duct_radius_ + object_radius_) * cos(M_PI / 2 - first_phi));
          double theta_1 = M_PI - 2 * asin((duct_radius_ + object_radius_) / d_1 * sin(M_PI / 2 + first_phi));
          double theta_2 = M_PI - 2 * asin((duct_radius_ + object_radius_) / d_2 * sin(M_PI / 2 - first_phi));
          double psi_1 = 2 * asin(link_length_ / 2 / d_1 * sin(M_PI / 2 + first_phi));
          double psi_2 = 2 * asin(link_length_ / 2 / d_2 * sin(M_PI / 2 - first_phi));
          double phi_1 = first_phi;
          double phi_2 = -first_phi;
          if(kinematics_verbose_)
            ROS_INFO("[envelopingCalc]: cylinder, d1: %f, d2: %f, tehta1: %f, theta2: %f, psi1: %f, psi2: %f, phi1: %f, phi2: %f", d_1, d_2, theta_1, theta_2, psi_1, psi_2, phi_1, phi_2);

          for(int i = 0; i < contact_num_; i ++)
            {
              double theta = (i % 2 == 1)? theta_1: theta_2;
              double psi = (i % 2 == 1)? psi_1: psi_2;
              double phi = (i % 2 == 1)? phi_1: phi_2;

              v_contact_rot.push_back(Quaterniond(AngleAxisd(abs_psi, Vector3d::UnitZ())));
              v_contact_p.push_back(v_contact_rot.at(i) * Vector3d(0, -object_radius_, 0));
              v_joint_p.push_back(v_contact_rot.at(i) * (Vector3d(0, -object_radius_ - duct_radius_, 0) + AngleAxisd(v_phi.at(i), Vector3d::UnitZ()) * Vector3d(-link_length_ / 2, 0, 0)));

              if(i < contact_num_ -1)
                {
                  v_theta.push_back(theta);
                  v_phi.push_back(phi); // the first phi is add in the start part of this function
                }
              abs_psi += psi;

              if(kinematics_verbose_)
                ROS_INFO("[envelopingCalc]: cylinder, joint%d p: [%f, %f], contact%d p: [%f, %f], ", i , v_joint_p.at(i)(0), v_joint_p.at(i)(1), i + 1, v_contact_p.at(i)(0), v_contact_p.at(i)(1));

              /* check the validation of joint angle */
              if(theta <= 0 || fabs(theta) >= joint_angle_limit_)
                {
                  if(kinematics_verbose_)
                    ROS_WARN("[envelopingCalc]: cylinder: something wrong with this algorithm, theta:%f, joint_angle_limit: %f", theta, joint_angle_limit_);
                  return false;
                }
            }
          break;
        }
      default:
        break;
      }
    return true;
  }
};
