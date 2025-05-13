#include "DriveSystem.h"

#include <ArduinoJson.h>
#include <Streaming.h>

#include "Utils.h"

DriveSystem::DriveSystem() : front_bus_(), rear_bus_() {
  control_mode_ = DriveControlMode::kIdle;
  fault_current_ = 10.0;
  fault_position_ = PI;
  fault_velocity_ = 7.0;
  max_current_ = 0.0;
  position_reference_.fill(0.0);
  velocity_reference_.fill(0.0);
  current_reference_.fill(0.0);
  active_mask_.fill(false);
  zero_position_.fill(0.0);
  start_position_.fill(0.0);

  cartesian_position_gains_.kp.Fill(0.0);
  cartesian_position_gains_.kd.Fill(0.0);

  std::array<float, 12> direction_multipliers = {-1, -1, 1, -1, 1, -1,
                                                 -1, -1, 1, -1, 1, -1};
  direction_multipliers_ = direction_multipliers;
  current_limit_ = 2.0;

  float backlash = 2.0 / 80.0;
  float abduction_zero_position = 0 + backlash;
  float hip_zero_position = (90 - 30) * PI / 180 + backlash;
  float knee_zero_position = (180 - 30) * PI / 180 + backlash;

  float abduction_init_position = 45 * PI / 180 + backlash;
  float hip_init_position = 90 * PI / 180 + backlash;
  float knee_init_position = (180 - 15) * PI / 180 + backlash;

  std::array<float, 12> homing_directions = {-1, 1, -1, 1, 1, -1,
                                             -1, 1, -1, 1, 1, -1};
  homing_directions_ = homing_directions;
  ActuatorPositionVector zero_positions_cmd = {abduction_zero_position, hip_zero_position, knee_zero_position, abduction_zero_position, hip_zero_position, knee_zero_position,
                                                abduction_zero_position, hip_zero_position, knee_zero_position, abduction_zero_position, hip_zero_position, knee_zero_position};
  zero_positions_cmd_ = zero_positions_cmd;
  ActuatorPositionVector initial_positions = {abduction_init_position, hip_init_position, knee_init_position, abduction_init_position, hip_init_position, knee_init_position,
                                              abduction_init_position, hip_init_position, knee_init_position, abduction_init_position, hip_init_position, knee_init_position};
  initial_positions_ = initial_positions;
  std::array<bool, 12> homed_axes = {false, false, false, false, false, false,
                                     false, false, false, false, false, false};
  homed_axes_ = homed_axes;
  just_homed_ = false;

  homing_velocity = 0.0005;
  std::array<int, 4> knee_axes = {2, 5, 8, 11};
  knee_axes_ = knee_axes;
  std::array<int, 4> hip_axes = {1, 4, 7, 10};
  hip_axes_ = hip_axes;
  std::array<int, 2> right_abduction_axes = {0, 6};
  right_abduction_axes_ = right_abduction_axes;
  std::array<int, 2> left_abduction_axes = {3, 9};
  left_abduction_axes_ = left_abduction_axes;
  /*  Homing parameters end */

  knee_soft_limit = -PI / 6;

  SetDefaultCartesianPositions();
}

void DriveSystem::CheckForCANMessages() {
  front_bus_.PollCAN();
  rear_bus_.PollCAN();
}

DriveControlMode DriveSystem::CheckErrors() {
  for (size_t i = 0; i < kNumActuators; i++) {
    // check positions
    if (abs(GetActuatorPosition(i)) > fault_position_) {
      Serial << "actuator[" << i << "] hit fault position: " << fault_position_
             << endl;
      return DriveControlMode::kError;
    }
    // check velocities
    if (abs(GetActuatorVelocity(i)) > fault_velocity_) {
      Serial << "actuator[" << i << "] hit fault velocity: " << fault_velocity_
             << endl;
      return DriveControlMode::kError;
    }
  }
  return DriveControlMode::kIdle;
}

void DriveSystem::SetIdle() { control_mode_ = DriveControlMode::kIdle; }

void DriveSystem::SetupIMU(int filter_frequency) { imu.Setup(filter_frequency); }

void DriveSystem::UpdateIMU() { imu.Update(); }

void DriveSystem::ExecuteHomingSequence() {
  control_mode_ = DriveControlMode::kHoming;
  for (size_t i = 0; i < kNumActuators; i++) {
    homed_axes_[i] = false;
  }
  SetActivations({1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1});
  SetMaxCurrent(current_limit_);
}

template <size_t N>
bool DriveSystem::CheckHomingStatus(std::array<int, N> axes) {
  // Check if any axes are not homed
  for (int i : axes) {
    if (!homed_axes_[i]) {
      return false;
    }
  }

  return true;
}

void DriveSystem::ZeroCurrentPosition() {
  SetZeroPositions(GetRawActuatorPositions());
}

void DriveSystem::SetZeroPositions(ActuatorPositionVector zero) {
  zero_position_ = zero;
}

ActuatorPositionVector DriveSystem::DefaultCartesianPositions() {
  ActuatorPositionVector pos;
  for (int i = 0; i < 4; i++) {
    BLA::Matrix<3> p = ForwardKinematics({0, 0, 0}, leg_parameters_, i) +
                       HipPosition(hip_layout_parameters_, i);
    pos[3 * i] = p(0);
    pos[3 * i + 1] = p(1);
    pos[3 * i + 2] = p(2);
  }
  return pos;
}

void DriveSystem::SetDefaultCartesianPositions() {
  cartesian_position_reference_ = DefaultCartesianPositions();
}

void DriveSystem::SetJointPositions(ActuatorPositionVector pos) {
  control_mode_ = DriveControlMode::kPositionControl;
  position_reference_ = pos;
}

void DriveSystem::SetPositionKp(float kp) { position_gains_.kp = kp; }

void DriveSystem::SetPositionKd(float kd) { position_gains_.kd = kd; }

void DriveSystem::SetCartesianKp3x3(BLA::Matrix<3, 3> kp) {
  cartesian_position_gains_.kp = kp;
}

void DriveSystem::SetCartesianKd3x3(BLA::Matrix<3, 3> kd) {
  cartesian_position_gains_.kd = kd;
}

void DriveSystem::SetCartesianPositions(ActuatorPositionVector pos) {
  control_mode_ = DriveControlMode::kCartesianPositionControl;
  cartesian_position_reference_ = pos;
}

void DriveSystem::SetCartesianVelocities(ActuatorVelocityVector vel) {
  control_mode_ = DriveControlMode::kCartesianPositionControl;
  cartesian_velocity_reference_ = vel;
}

void DriveSystem::SetFeedForwardForce(BLA::Matrix<12> force) {
  ff_force_ = force;
}

void DriveSystem::SetCurrent(uint8_t i, float current_reference) {
  control_mode_ = DriveControlMode::kCurrentControl;
  current_reference_[i] = current_reference;
}

void DriveSystem::SetFaultCurrent(float fault_current) {
  fault_current_ = fault_current;
}

void DriveSystem::SetFaultVelocity(float fault_velocity) {
  fault_velocity_ = fault_velocity;
}

void DriveSystem::SetMaxCurrent(float max_current) {
  max_current_ = max_current;
}

BLA::Matrix<12> DriveSystem::CartesianPositionControl() {
  BLA::Matrix<12> actuator_torques;
  for (int leg_index = 0; leg_index < 4; leg_index++) {
    auto joint_angles = LegJointAngles(leg_index);
    auto joint_velocities = LegJointVelocities(leg_index);
    BLA::Matrix<3, 3> jac =
        LegJacobian(joint_angles, leg_parameters_, leg_index);

    auto measured_hip_relative_positions =
        ForwardKinematics(joint_angles, leg_parameters_, leg_index);
    auto measured_velocities = jac * joint_velocities;
    auto reference_hip_relative_positions =
        LegCartesianPositionReference(leg_index) -
        HipPosition(hip_layout_parameters_, leg_index);
    auto reference_velocities = LegCartesianVelocityReference(leg_index);

    auto cartesian_forces =
        PDControl3(measured_hip_relative_positions, measured_velocities,
                   reference_hip_relative_positions, reference_velocities,
                   cartesian_position_gains_) +
        LegFeedForwardForce(leg_index);
    auto knee_angle = joint_angles(2);
    auto knee_constraint_torque = (knee_angle > knee_soft_limit) ? position_gains_.kp * (knee_soft_limit - knee_angle) : 0.0;
    auto joint_torques = ~jac * cartesian_forces;

    // Ensures that the direction of the force is preserved when motors
    // saturate
    float norm = Utils::InfinityNorm3(joint_torques);
    if (norm > max_current_) {
      joint_torques = joint_torques * max_current_ / norm;
    }

    actuator_torques(3 * leg_index) = joint_torques(0);
    actuator_torques(3 * leg_index + 1) = joint_torques(1);
    actuator_torques(3 * leg_index + 2) = joint_torques(2) + knee_constraint_torque;
  }
  return actuator_torques;
}

void DriveSystem::Update() {
  // If there are errors, put the system in the error state.
  if (CheckErrors() == DriveControlMode::kError) {
    control_mode_ = DriveControlMode::kError;
  }

  switch (control_mode_) {
    case DriveControlMode::kError: {
      Serial << "ERROR" << endl;
      CommandIdle();
      break;
    }
    case DriveControlMode::kIdle: {
      CommandIdle();
      break;
    }
    case DriveControlMode::kHoming: {
      start_position_ = GetRawActuatorPositions();
      bool position_warning = false;

      for (size_t i = 0; i < kNumActuators; i++) {
        if (abs(start_position_[i]) > 0.15) {
          position_warning = true;
        }
      }

      if (position_warning) {
        Serial << "WARNING: Initial position is not zero" << endl;
        control_mode_ = DriveControlMode::kError;
        break;
      }

      for (size_t i = 0; i < kNumActuators; i++) {
        zero_position_[i] = start_position_[i] -
                            (zero_positions_cmd_[i] *
                             direction_multipliers_[i] * homing_directions_[i]);
        homed_axes_[i] = true;
      }
      ActuatorPositionVector non_constrained_positions;

      for (size_t i = 0; i < kNumActuators; i++) {
        non_constrained_positions[i] =
            initial_positions_[i] * homing_directions_[i];
      }

      position_reference_ =
          Utils::Constrain(non_constrained_positions, float(-PI), float(PI));
      Serial << "Homing complete" << endl;
      // Switch to position control mode
      just_homed_ = true;
      control_mode_ = DriveControlMode::kPositionControl;
    }
    case DriveControlMode::kPositionControl: {
      if (just_homed_) {
        static unsigned long transition_start_time = millis();
        static ActuatorPositionVector start_positions = GetActuatorPositions();
        static ActuatorPositionVector target_positions = position_reference_;

        const unsigned long transition_duration = 5000;
        float progress =
            (float)(millis() - transition_start_time) / transition_duration;
        progress = (progress < 0.0f)   ? 0.0f
                   : (progress > 1.0f) ? 1.0f
                                       : progress;
        float smooth_progress = 0.5f - 0.5f * cos(progress * PI);

        ActuatorPositionVector interpolated_positions;
        for (size_t i = 0; i < kNumActuators; i++) {
          interpolated_positions[i] =
              start_positions[i] +
              (target_positions[i] - start_positions[i]) * smooth_progress;
        }

        ActuatorCurrentVector pd_current;
        for (size_t i = 0; i < kNumActuators; i++) {
          PD(pd_current[i], GetActuatorPosition(i), GetActuatorVelocity(i),
             interpolated_positions[i], velocity_reference_[i],
             position_gains_);
        }
        CommandCurrents(pd_current);

        if (progress >= 1.0f) {
          just_homed_ = false;
        }
      } else {
        ActuatorCurrentVector pd_current;
        for (size_t i = 0; i < kNumActuators; i++) {
          PD(pd_current[i], GetActuatorPosition(i), GetActuatorVelocity(i),
             position_reference_[i], velocity_reference_[i], position_gains_);
        }
        CommandCurrents(pd_current);
      }
      break;
    }
    case DriveControlMode::kCartesianPositionControl: {
      CommandCurrents(Utils::VectorToArray<12, 12>(CartesianPositionControl()));
      break;
    }
    case DriveControlMode::kCurrentControl: {
      CommandCurrents(current_reference_);
      break;
    }
  }
}

void DriveSystem::SetActivations(ActuatorActivations acts) {
  active_mask_ = acts;  // Is this a copy?
}

void DriveSystem::CommandIdle() {
  ActuatorCurrentVector currents;
  currents.fill(0.0);
  CommandCurrents(currents);
}

void DriveSystem::CommandCurrents(ActuatorCurrentVector currents) {
  ActuatorCurrentVector current_command =
      Utils::Constrain(currents, -max_current_, max_current_);
  if (Utils::Maximum(current_command) > fault_current_ ||
      Utils::Minimum(current_command) < -fault_current_) {
    Serial << "Requested current too large. Erroring out. Current request: " << current_command << endl;
    control_mode_ = DriveControlMode::kError;
    return;
  }
  // Set disabled motors to zero current
  current_command = Utils::MaskArray(current_command, active_mask_);

  // Update record of last commanded current
  last_commanded_current_ = current_command;

  // Convert the currents into the motors' local frames
  current_command = Utils::ElemMultiply(current_command, direction_multipliers_);

  // Convert from float array to int32 array in units milli amps.
  std::array<int32_t, kNumActuators> currents_mA = Utils::ConvertToFixedPoint(current_command, 1000);

  // Send current commands down the CAN buses
  front_bus_.CommandTorques(currents_mA[0], currents_mA[1], currents_mA[2], currents_mA[3], C610Subbus::kIDZeroToThree);
  front_bus_.CommandTorques(currents_mA[4], currents_mA[5], 0, 0, C610Subbus::kIDFourToSeven);
  rear_bus_.CommandTorques(currents_mA[6], currents_mA[7], currents_mA[8], currents_mA[9], C610Subbus::kIDZeroToThree);
  rear_bus_.CommandTorques(currents_mA[10], currents_mA[11], 0, 0, C610Subbus::kIDFourToSeven);
}

C610 DriveSystem::GetController(uint8_t i) {
  if (i >= 0 && i <= (kNumActuatorsPerBus - 1)) {
    return front_bus_.Get(i);
  } else if (i >= kNumActuatorsPerBus && i <= (kNumActuators - 1)) {
    return rear_bus_.Get(i - 6);
  } else {
    Serial << "Invalid actuator index. Must be 0<=i<=11." << endl;
    control_mode_ = DriveControlMode::kError;
    return C610();
  }
}

float DriveSystem::GetRawActuatorPosition(uint8_t i) {
  return GetController(i).Position();
}

ActuatorPositionVector DriveSystem::GetRawActuatorPositions() {
  ActuatorPositionVector pos;
  for (size_t i = 0; i < pos.size(); i++) {
    pos[i] = GetRawActuatorPosition(i);
  }
  return pos;
}

float DriveSystem::GetActuatorPosition(uint8_t i) {
  return (GetRawActuatorPosition(i) - zero_position_[i]) *
         direction_multipliers_[i];
}

ActuatorPositionVector DriveSystem::GetActuatorPositions() {
  ActuatorPositionVector pos;
  for (size_t i = 0; i < pos.size(); i++) {
    pos[i] = GetActuatorPosition(i);
  }
  return pos;
}

float DriveSystem::GetActuatorVelocity(uint8_t i) {
  return GetController(i).Velocity() * direction_multipliers_[i];
}

float DriveSystem::GetActuatorCurrent(uint8_t i) {
  return GetController(i).Current() * direction_multipliers_[i];
}

float DriveSystem::GetTotalElectricalPower() {
  float power = 0.0;
  for (uint8_t i = 0; i < kNumActuators; i++) {
    power += GetController(i).ElectricalPower();
  }
  return power;
}

float DriveSystem::GetTotalMechanicalPower() {
  float power = 0.0;
  for (uint8_t i = 0; i < kNumActuators; i++) {
    power += GetController(i).MechanicalPower();
  }
  return power;
}

BLA::Matrix<3> DriveSystem::LegJointAngles(uint8_t i) {
  return {GetActuatorPosition(3 * i), GetActuatorPosition(3 * i + 1),
          GetActuatorPosition(3 * i + 2)};
}

BLA::Matrix<3> DriveSystem::LegJointVelocities(uint8_t i) {
  return {GetActuatorVelocity(3 * i), GetActuatorVelocity(3 * i + 1),
          GetActuatorVelocity(3 * i + 2)};
}

// Get the cartesian reference position for leg i.
BLA::Matrix<3> DriveSystem::LegCartesianPositionReference(uint8_t i) {
  return {cartesian_position_reference_[3 * i],
          cartesian_position_reference_[3 * i + 1],
          cartesian_position_reference_[3 * i + 2]};
}

// Return the cartesian reference velocity for leg i.
BLA::Matrix<3> DriveSystem::LegCartesianVelocityReference(uint8_t i) {
  return {cartesian_velocity_reference_[3 * i],
          cartesian_velocity_reference_[3 * i + 1],
          cartesian_velocity_reference_[3 * i + 2]};
}

BLA::Matrix<3> DriveSystem::LegFeedForwardForce(uint8_t i) {
  return {ff_force_(3 * i), ff_force_(3 * i + 1), ff_force_(3 * i + 2)};
}

void DriveSystem::PrintHeader(DrivePrintOptions options) {
  if (options.time) {
    Serial << "T" << options.delimiter;
  }
  for (size_t i = 0; i < kNumActuators; i++) {
    if (!active_mask_[i]) continue;
    if (options.positions) {
      Serial << "p[" << i << "]" << options.delimiter;
    }
    if (options.velocities) {
      Serial << "v[" << i << "]" << options.delimiter;
    }
    if (options.currents) {
      Serial << "I[" << i << "]" << options.delimiter;
    }
    if (options.position_references) {
      Serial << "pr[" << i << "]" << options.delimiter;
    }
    if (options.velocity_references) {
      Serial << "vr[" << i << "]" << options.delimiter;
    }
    if (options.current_references) {
      Serial << "Ir[" << i << "]" << options.delimiter;
    }
    if (options.last_current) {
      Serial << "Il[" << i << "]" << options.delimiter;
    }
  }
  Serial << endl;
}

void DriveSystem::PrintMsgPackStatus(DrivePrintOptions options) {
  StaticJsonDocument<2048> doc;
  // 21 micros to put this doc together
  doc["ts"] = millis();
  doc["yaw"] = imu.yaw;
  doc["pitch"] = imu.pitch;
  doc["roll"] = imu.roll;
  doc["yaw_rate"] = imu.yaw_rate;
  doc["pitch_rate"] = imu.pitch_rate;
  doc["roll_rate"] = imu.roll_rate;
  for (uint8_t i = 0; i < kNumActuators; i++) {
    if (options.positions) {
      doc["pos"][i] = GetActuatorPosition(i);
    }
    if (options.velocities) {
      doc["vel"][i] = GetActuatorVelocity(i);
    }
    if (options.currents) {
      doc["cur"][i] = GetActuatorCurrent(i);
    }
    if (options.position_references) {
      doc["pref"][i] = position_reference_[i];
    }
    if (options.velocity_references) {
      doc["vref"][i] = velocity_reference_[i];
    }
    if (options.current_references) {
      doc["cref"][i] = current_reference_[i];
    }
    if (options.last_current) {
      doc["lcur"][i] = last_commanded_current_[i];
    }
  }
  uint16_t num_bytes = measureMsgPack(doc);
  // Serial.println(num_bytes);
  Serial.write(69);
  Serial.write(69);
  Serial.write(num_bytes >> 8 & 0xff);
  Serial.write(num_bytes & 0xff);
  serializeMsgPack(doc, Serial);
  Serial.println();
}

void DriveSystem::PrintStatus(DrivePrintOptions options) {
  char delimiter = options.delimiter;
  if (options.time) {
    Serial << millis() << delimiter;
  }
  Serial << imu.yaw << delimiter;
  Serial << imu.pitch << delimiter;
  Serial << imu.roll << delimiter;
  Serial << imu.yaw_rate << delimiter;
  Serial << imu.pitch_rate << delimiter;
  Serial << imu.roll_rate << delimiter;

  for (uint8_t i = 0; i < kNumActuators; i++) {
    if (!active_mask_[i]) continue;
    if (options.positions) {
      Serial.print(GetActuatorPosition(i), 2);
      Serial << delimiter;
    }
    if (options.velocities) {
      Serial.print(GetActuatorVelocity(i), 2);
      Serial << delimiter;
    }
    if (options.currents) {
      Serial.print(GetActuatorCurrent(i), 2);
      Serial << delimiter;
    }
    if (options.position_references) {
      Serial.print(position_reference_[i], 2);
      Serial << delimiter;
    }
    if (options.velocity_references) {
      Serial.print(velocity_reference_[i], 2);
      Serial << delimiter;
    }
    if (options.current_references) {
      Serial.print(current_reference_[i], 2);
      Serial << delimiter;
    }
    if (options.last_current) {
      Serial.print(last_commanded_current_[i], 2);
      Serial << delimiter;
    }
  }
  Serial << endl;
  Serial.flush();
}

BLA::Matrix<kNumDriveSystemDebugValues> DriveSystem::DebugData() {
  uint32_t write_index = 0;
  BLA::Matrix<kNumDriveSystemDebugValues> output;
  output(write_index++) = millis();
  output(write_index++) = imu.yaw;
  output(write_index++) = imu.pitch;
  output(write_index++) = imu.roll;
  output(write_index++) = imu.yaw_rate;
  output(write_index++) = imu.pitch_rate;
  output(write_index++) = imu.roll_rate;
  for (uint8_t i = 0; i < kNumActuators; i++) {
    output(write_index++) = GetActuatorPosition(i);
    output(write_index++) = GetActuatorVelocity(i);
    output(write_index++) = GetActuatorCurrent(i);
    output(write_index++) = position_reference_[i];
    output(write_index++) = velocity_reference_[i];
    output(write_index++) = current_reference_[i];
    output(write_index++) = last_commanded_current_[i];
  }
  return output;
}