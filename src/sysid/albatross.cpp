/****************************************************************************
 *
 * Copyright 2026 Jeremy W. Hopwood. All rights reserved.
 *
 ****************************************************************************/

/**
 * @brief Actuator control using offboard mode for system ID
 * @file albatross.cpp
 * @author Jeremy Hopwood <jeremy@hopwoodclan.org>
 */

#include <px4_msgs/msg/offboard_control_mode.hpp>
#include <px4_msgs/msg/actuator_servos.hpp>
#include <px4_msgs/msg/actuator_motors.hpp>
#include <px4_msgs/msg/manual_control_setpoint.hpp>
#include <px4_msgs/msg/vehicle_status.hpp>
#include <rclcpp/rclcpp.hpp>
#include <stdint.h>

#include <chrono>
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <map>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace px4_msgs::msg;

class OffboardControl : public rclcpp::Node
{
public:
	OffboardControl() : Node("fox")
	{
		// Create publishers
		offboard_control_mode_publisher_ = this->create_publisher<OffboardControlMode>("/fmu/in/offboard_control_mode", 1);
		actuator_servos_publisher_ = this->create_publisher<ActuatorServos>("/fmu/in/actuator_servos", 1);
		actuator_motors_publisher_ = this->create_publisher<ActuatorMotors>("/fmu/in/actuator_motors", 1);

		// Create subscribers
		rmw_qos_profile_t qos_profile_mcs = rmw_qos_profile_sensor_data;
		auto qos_mcs = rclcpp::QoS(rclcpp::QoSInitialization(qos_profile_mcs.history, 1), qos_profile_mcs);
		manual_control_setpoint_subscriber_ = this->create_subscription<px4_msgs::msg::ManualControlSetpoint>("/fmu/out/manual_control_setpoint", qos_mcs,
			[this](const px4_msgs::msg::ManualControlSetpoint::UniquePtr msg) {
				da = msg->roll;
				de = msg->pitch;
				dr = msg->yaw;
				dt = msg->throttle;
			});
		rmw_qos_profile_t qos_profile_vs = rmw_qos_profile_sensor_data;
		auto qos_vs = rclcpp::QoS(rclcpp::QoSInitialization(qos_profile_vs.history, 1), qos_profile_vs);
		vehcile_status_subscriber_ = this->create_subscription<px4_msgs::msg::VehicleStatus>("/fmu/out/vehicle_status", qos_vs,
			[this](const px4_msgs::msg::VehicleStatus::UniquePtr msg) {
				offboard_mode = (msg->nav_state == msg->NAVIGATION_STATE_OFFBOARD);
			});

		// Load CSV
		load_data();
		if (InputSignal.empty()) {
    		RCLCPP_ERROR(get_logger(), "Error loading CSV: %s", ms_file.c_str());
		}

		auto timer_callback = [this]() -> void {
			// Get current time
			const uint64_t now_us = this->get_clock()->now().nanoseconds() / 1000;

			// Let PX4 know what we will be sending
			publish_offboard_control_mode();

			// If we have just entered offboard mode, set initial time
			if (offboard_mode && !prev_offboard_mode) {
				t0 = now_us;
			}
			
			// If we are in offboard mode, publish actuator commands
			if (offboard_mode) {
				publish_actuators();
			}

			// Update previous offboard mode state
			prev_offboard_mode = offboard_mode;
		};
		timer_ = this->create_wall_timer(10ms, timer_callback); // 10ms interval must correspond to 1/fs
	}

private:
	rclcpp::TimerBase::SharedPtr timer_;

	// Declare publishers and subscribers
	rclcpp::Publisher<OffboardControlMode>::SharedPtr offboard_control_mode_publisher_;
	rclcpp::Publisher<ActuatorServos>::SharedPtr actuator_servos_publisher_;
	rclcpp::Publisher<ActuatorMotors>::SharedPtr actuator_motors_publisher_;
	rclcpp::Subscription<ManualControlSetpoint>::SharedPtr manual_control_setpoint_subscriber_;
	rclcpp::Subscription<VehicleStatus>::SharedPtr vehcile_status_subscriber_;

	// Excitation signal
	const std::string ms_file = "/home/pace/src/sysid-ROS2-PX4/src/signals/ms_albatross_3s1p_T30_f005-075-2_100hz.csv";
	const int T_ms = 30; // seconds
	const int fs = 100; // Hz
	std::map<int,std::vector<float>> InputSignal;
	uint64_t t0 = 0; // microseconds

	// Stick positions
	double da = 0.0, de = 0.0, dr = 0.0, df = 0.0, dt = 0.0;

	// Offboard mode booleans
	bool offboard_mode = false, prev_offboard_mode = false;

	void publish_offboard_control_mode();
	void publish_actuators();

	void load_data();
};

/**
 * @brief Publish the offboard control mode.
 */
void OffboardControl::publish_offboard_control_mode()
{
	OffboardControlMode msg{};
	msg.position = false;
	msg.velocity = false;
	msg.acceleration = false;
	msg.attitude = false;
	msg.body_rate = false;
	msg.thrust_and_torque = false;
	msg.direct_actuator = true;
	msg.timestamp = this->get_clock()->now().nanoseconds() / 1000;
	offboard_control_mode_publisher_->publish(msg);
}

/**
 * @brief Publish actuator controls
 */
void OffboardControl::publish_actuators()
{
	// Create messages
	ActuatorServos msg_servos{};
	ActuatorMotors msg_motors{};

	// Populate with manual control inputs
	// TODO: check signs and allocation
	msg_servos.control[0] = -da;
	msg_servos.control[1] = df;
	msg_servos.control[2] = df;
	msg_servos.control[3] = da;
	msg_servos.control[4] = dr;
	msg_servos.control[5] = de;
	msg_motors.control[0] = 0.5*(dt + 1.0);

	// Compute the time index
	uint64_t t1 = this->get_clock()->now().nanoseconds() / 1000; // microseconds
	int time_idx = (int)( (t1-t0)/(1000000/fs) ) % (T_ms*fs);
	//
	// TODO: Check to see if the following works instead (should be safer)
	/*
	auto it = InputSignal.find(time_idx);
	if (it == InputSignal.end() || it->second.size() < 4) {
    	RCLCPP_ERROR(this->get_logger(), "Missing or malformed input at index %d", time_idx);
    	return;
	}
	const std::vector<float>& input = it->second;
	*/
	
	// Get the input excitation vector from the map
	std::vector<float> input = InputSignal[time_idx];
	
	// Add excitation (da,de,dr,dt) to the manual control inputs (daL,~,~,daR,dr,de,dt) [no flaps]
	// TODO: check signs and allocation
	msg_servos.control[0] += -input[0]; // left aileron
	msg_servos.control[3] += input[0]; // right aileron
	msg_servos.control[4] += input[2]; // rudder
	msg_servos.control[5] += input[1]; // elevator
	msg_motors.control[0] += input[3]; // motor

	// Set the timestamp and publish
	uint64_t t = this->get_clock()->now().nanoseconds() / 1000;
	msg_servos.timestamp = t;
	msg_motors.timestamp = t;
	actuator_servos_publisher_->publish(msg_servos);
	actuator_motors_publisher_->publish(msg_motors);
}

void OffboardControl::load_data()
{
	// Initialize and open the CSV file
	std::ifstream indata;
	indata.open(ms_file);
	std::string line;
	if (!indata.is_open()) {
		RCLCPP_ERROR(this->get_logger(), "Could not open CSV: %s", ms_file.c_str());
	return;

	// Loop through each line (i.e. time index)
	int tidx = 0;
	while (getline(indata, line)) {
		// initialization for this line
		std::vector<float> values;
		std::stringstream lineStream(line);
		std::string cell;

		// Get the first "cell" as the time index
		std::getline(lineStream, cell, ',');
		tidx = stoi(cell);
		
		// The rest of the line is the vector of values
		while (std::getline(lineStream, cell, ',')) {
			values.push_back(stof(cell));
		}
		
		// Assign the vector of values to the integer time key
		InputSignal[tidx] = values;
	}
}

int main(int argc, char *argv[])
{
	std::cout << "Starting albatross system ID node..." << std::endl;
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);
	rclcpp::init(argc, argv);
	rclcpp::spin(std::make_shared<OffboardControl>());

	rclcpp::shutdown();
	return 0;
}

