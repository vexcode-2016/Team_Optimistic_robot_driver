/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2011, Eric Perko, Chad Rockey
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
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Case Western Reserve University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *0
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

#include <math.h>
#include <geometry_msgs/Quaternion.h>
#include <std_msgs/Empty.h>

#include "robot_driver/robotPOS.h"

robotPOS::robotPOS(const std::string &port, uint32_t baud_rate, boost::asio::io_service &io, int csChannel, long speed):
  port_(port)
  ,baud_rate_(baud_rate)
  ,serial_(io, port_)
{
    serial_.set_option(boost::asio::serial_port_base::baud_rate(baud_rate_));
    spcPub = n.advertise<std_msgs::Empty>("robotPOS/spcRequest", 1000);
    mpcPub = n.advertise<geometry_msgs::Point32>("robotPOS/pickedUpObject", 1000);

    // Init imu
    imu_(csChannel, speed);
    std::cout << "IMU INIT" << std::endl;
    std::cout << imu_.init(1, BITS_DLPF_CFG_5HZ) << std::endl;

    usleep(100000);
  	usleep(100000);

  	std::cout << "gyro scale = " << std::dec<< imu_.set_gyro_scale(BITS_FS_2000DPS) << std::endl;

    //half second wait. Function breaks with 1 million
  	usleep(500000);
  	usleep(500000);

  	std::cout << "accel scale = " << std::dec<< imu_.set_acc_scale(BITS_FS_16G) << std::endl;

    usleep(100000);
  	usleep(500000);
  	usleep(500000);
  	usleep(500000);
  	usleep(500000);
  	usleep(500000);

    std::cout << "IMU INIT DONE" << std::endl;
}

/**
 * Polls UART and sets its inputs to the latest data
 * @param odom Odometry data
 * @param imu  IMU data
 */
void robotPOS::poll(nav_msgs::Odometry *odom, sensor_msgs::Imu *imu)
{
  boost::array<uint8_t, 3> flagHolders; //0 = start byte, 1 = msg type, 2 = msg count

  // Load start byte
  do
  {
    boost::asio::read(serial_, boost::asio::buffer(&flagHolders[0], 1));
  } while (flagHolders[0] != 0xFA);

  // Load rest of header
  boost::asio::read(serial_, boost::asio::buffer(&flagHolders[1], 2));

  // Verify msg count
  if (!verifyMsgHeader(flagHolders[1], flagHolders[2]))
  {
    std::cout << "Message count invalid (" << flagHolders[2] << ") for type " << flagHolders[1] << "." << std::endl;
  }

  // Load msg
  std::vector<uint8_t> msgData;
  boost::asio::read(serial_, boost::asio::buffer(msgData, getMsgLengthForType(flagHolders[2])));

  // Use msg
  switch (flagHolders[2])
  {
    case std_msg_type:
      // Twist
      const float conversion = 1;
      odom->twist.twist.linear.x = 0;
      odom->twist.twist.linear.y = ((msgData[1] + msgData[2]) / 2.0) * conversion;
      odom->twist.twist.linear.z = 0;

      odom->twist.twist.angular.x = 0;
      odom->twist.twist.angular.y = 0;
      odom->twist.twist.angular.z = (msgData[1] - msgData[2]) * conversion;

      odom->twist.covariance = ODOM_TWIST_COV_MAT;

      // Pose
      geometry_msgs::Quaternion quat = odom->pose.pose.orientation;
      float theta = quatToEuler(quat) + (odom->twist.twist.angular.z / 2.0);

      odom->pose.pose.position.x += cos(theta);
      odom->pose.pose.position.y += sin(theta);
      odom->pose.pose.position.z = 0;

      theta += (odom->twist.twist.angular.z / 2.0);

      odom->pose.pose.orientation.x = 0;
      odom->pose.pose.orientation.y = 0;
      odom->pose.pose.orientation.z = sin(theta / 2.0);
      odom->pose.pose.orientation.w = sqrt(1.0) / 2;

      odom->pose.covariance = ODOM_POSE_COV_MAT;
      break;

    case spc_msg_type:
      //Tell motion_path_creator to tell the cortex which object to get
      spcPub.publish(std_msgs::Empty());
      break;

    case mpc_msg_type:
      //Publish the object that got picked up
      geometry_msgs::Point32 out;
      out.x = msgData[0];
      out.y = msgData[1];
      out.z = msgData[2];
      mpcPub.publish(out);
      break;
  }

  // Fill imu message
  const float dpsToRps = 0.01745;
	imu->angular_velocity.x = 0; // Hopefully this is 0
  imu->angular_velocity.y = 0; // Hopefully this is 0
  imu->angular_velocity.x = imu.read_rot(2) * dpsToRps;

  const float gravity = 9.80665;
  imu->linear_acceleration.x = imu.read_acc(0) * gravity;
  imu->linear_acceleration.y = imu.read_acc(1) * gravity;
  imu->linear_acceleration.z = gravity; // Hopefully this is gravity

  std::cout << "whoami: " << imu.whoami() << std::endl;
  std::cout << "Temp: " << imu.read_temp() << ", R0: " << imu.read_rot(0) << ", R1: " << imu.read_rot(1) << ", R2: " << imu.read_rot(2)
            << ", A0: " << imu.read_acc(0) << ", A1: "  << imu.read_acc(1) << ", A2: " << imu.read_acc(2) << std::endl;
  usleep(500000);
  usleep(500000);
}

/**
 * Converts a quaternion to an euler angle (yaw only)
 * @param  quat Quaternion
 * @return  n   Euler angle (yaw)
 */
inline const float quatToEuler(const geometry_msgs::Quaternion& quat) const
{
  return atan2((2 * ((quat.x * quat.w) + (quat.y * quat.z))),
                    ((quat.x * quat.x) + (quat.y * quat.y) - (quat.z * quat.z) - (quat.w * quat.w)));
}

/**
 * Callback function for sending ekf position estimate to cortex
 */
void robotPOS::ekf_callback(const nav_msgs::Odometry::ConstPtr& in)
{
  const int msgLength = 4;
  boost::array<uint8_t, msgLength> out;

  out[0] = outMsgCount++;
  out[1] = in->pose.pose.position.x;
  out[2] = in->pose.pose.position.y;
  const geometry_msgs::Quaternion quat = in->pose.pose.orientation;
  out[3] = quatToEuler(quat);

  //Send header
  sendMsgHeader(std_msg_type);

  //Send data
  boost::asio::write(serial_,  boost::asio::buffer(&out[0], msgLength));
}

/**
 * Callback function for sending new object position to cortex
 */
void robotPOS::mpc_callback(const geometry_msgs::Point32::ConstPtr& in)
{
  const int msgLength = 3;
  boost::array<uint8_t, msgLength> out = {{(uint8_t)in->x, (uint8_t)in->y, (uint8_t)in->z}};

  //Send header
  sendMsgHeader(mpc_msg_type);

  //Send data
  boost::asio::write(serial_, boost::asio::buffer(&out[0], msgLength));
}

/**
 * Returns the length of a given type of message
 * @param  type Type of message
 * @return      Length of message
 */
inline const uint8_t robotPOS::getMsgLengthForType(const uint8_t type) const
{
  switch (type)
  {
    case std_msg_type:
      return std_msg_length;

    case spc_msg_type:
      return spc_msg_length;

    case mpc_msg_type:
      return mpc_msg_length;

    default:
      return 0;
  }
}

/**
 * Sends message header over UART
 * @param type Type of message
 */
void robotPOS::sendMsgHeader(const uint8_t type)
{
  //Send start byte
  boost::asio::write(serial_, boost::asio::buffer(&startFlag[0], 1));

  //Send type byte
  boost::asio::write(serial_, boost::asio::buffer(&msgTypes[type - 1], 1));

  //Send count
  msgCounts[type - 1] = msgCounts[type - 1] + 1;
  boost::asio::write(serial_, boost::asio::buffer(&msgCounts[type - 1], 1));
}

/**
 * Verifies a message header
 * @param  type  Message type
 * @param  count Message count
 * @return       If header is valid
 */
inline const bool robotPOS::verifyMsgHeader(const uint8_t type, const uint8_t count)
{
  return (msgCounts[type] = msgCounts[type] + 1) == count;
}
