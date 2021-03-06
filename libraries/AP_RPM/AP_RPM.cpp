/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "AP_RPM.h"
#include "RPM_Pin.h"
#include "RPM_SITL.h"
#include "RPM_EFI.h"
#include "RPM_HarmonicNotch.h"
#include "RPM_ESC_Telem.h"

extern const AP_HAL::HAL& hal;

// table of user settable parameters
const AP_Param::GroupInfo AP_RPM::var_info[] = {
    // @Param: _TYPE
    // @DisplayName: RPM type
    // @Description: What type of RPM sensor is connected
    // @Values: 0:None,1:PWM,2:AUXPIN,3:EFI,4:Harmonic Notch,5:ESC Telemetry Motors Bitmask
    // @User: Standard
    AP_GROUPINFO("_TYPE",    0, AP_RPM, _type[0], 0),

    // @Param: _SCALING
    // @DisplayName: RPM scaling
    // @Description: Scaling factor between sensor reading and RPM.
    // @Increment: 0.001
    // @User: Standard
    AP_GROUPINFO("_SCALING", 1, AP_RPM, _scaling[0], 1.0f),

    // @Param: _MAX
    // @DisplayName: Maximum RPM
    // @Description: Maximum RPM to report
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("_MAX", 2, AP_RPM, _maximum[0], 100000),

    // @Param: _MIN
    // @DisplayName: Minimum RPM
    // @Description: Minimum RPM to report
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("_MIN", 3, AP_RPM, _minimum[0], 10),

    // @Param: _MIN_QUAL
    // @DisplayName: Minimum Quality
    // @Description: Minimum data quality to be used
    // @Increment: 0.1
    // @User: Advanced
    AP_GROUPINFO("_MIN_QUAL", 4, AP_RPM, _quality_min[0], 0.5),

    // @Param: _PIN
    // @DisplayName: Input pin number
    // @Description: Which pin to use
    // @Values: -1:Disabled,50:AUX1,51:AUX2,52:AUX3,53:AUX4,54:AUX5,55:AUX6
    // @User: Standard
    AP_GROUPINFO("_PIN",    5, AP_RPM, _pin[0], -1),

    // @Param: _ESC_MASK
    // @DisplayName: Bitmask of ESC telemetry channels to average
    // @Description: Mask of channels which support ESC rpm telemetry. RPM telemetry of the selected channels will be averaged
    // @Bitmask: 0:Channel1,1:Channel2,2:Channel3,3:Channel4,4:Channel5,5:Channel6,6:Channel7,7:Channel8,8:Channel9,9:Channel10,10:Channel11,11:Channel12,12:Channel13,13:Channel14,14:Channel15,15:Channel16
    // @User: Advanced
    AP_GROUPINFO("_ESC_MASK",  6, AP_RPM, _esc_mask[0], 0),

#if RPM_MAX_INSTANCES > 1
    // @Param: 2_TYPE
    // @DisplayName: Second RPM type
    // @Description: What type of RPM sensor is connected
    // @Values: 0:None,1:PWM,2:AUXPIN,3:EFI,4:Harmonic Notch,5:ESC Telemetry Motors Bitmask
    // @User: Advanced
    AP_GROUPINFO("2_TYPE",    10, AP_RPM, _type[1], 0),

    // @Param: 2_SCALING
    // @DisplayName: RPM scaling
    // @Description: Scaling factor between sensor reading and RPM.
    // @Increment: 0.001
    // @User: Advanced
    AP_GROUPINFO("2_SCALING", 11, AP_RPM, _scaling[1], 1.0f),

    // @Param: 2_PIN
    // @DisplayName: RPM2 input pin number
    // @Description: Which pin to use
    // @Values: -1:Disabled,50:AUX1,51:AUX2,52:AUX3,53:AUX4,54:AUX5,55:AUX6
    // @User: Standard
    AP_GROUPINFO("2_PIN",    12, AP_RPM, _pin[1], -1),

    // @Param: 2_ESC_MASK
    // @DisplayName: Bitmask of ESC telemetry channels to average
    // @Description: Mask of channels which support ESC rpm telemetry. RPM telemetry of the selected channels will be averaged
    // @Bitmask: 0:Channel1,1:Channel2,2:Channel3,3:Channel4,4:Channel5,5:Channel6,6:Channel7,7:Channel8,8:Channel9,9:Channel10,10:Channel11,11:Channel12,12:Channel13,13:Channel14,14:Channel15,15:Channel16
    // @User: Advanced
    AP_GROUPINFO("2_ESC_MASK",  13, AP_RPM, _esc_mask[1], 0),
#endif

    AP_GROUPEND
};

AP_RPM::AP_RPM(void)
{
    AP_Param::setup_object_defaults(this, var_info);

    if (_singleton != nullptr) {
        AP_HAL::panic("AP_RPM must be singleton");
    }
    _singleton = this;
}

/*
  initialise the AP_RPM class.
 */
void AP_RPM::init(void)
{
    if (num_instances != 0) {
        // init called a 2nd time?
        return;
    }
    for (uint8_t i=0; i<RPM_MAX_INSTANCES; i++) {
        switch (_type[i]) {
#if CONFIG_HAL_BOARD != HAL_BOARD_SITL
        case RPM_TYPE_PWM:
        case RPM_TYPE_PIN:
            // PWM option same as PIN option, for upgrade
            drivers[i] = new AP_RPM_Pin(*this, i, state[i]);
            break;
#endif
        case RPM_TYPE_ESC_TELEM:
            drivers[i] = new AP_RPM_ESC_Telem(*this, i, state[i]);
            break;
#if HAL_EFI_ENABLED
        case RPM_TYPE_EFI:
            drivers[i] = new AP_RPM_EFI(*this, i, state[i]);
            break;
#endif
        // include harmonic notch last
        // this makes whatever process is driving the dynamic notch appear as an RPM value
        case RPM_TYPE_HNTCH:
            drivers[i] = new AP_RPM_HarmonicNotch(*this, i, state[i]);
            break;
#if CONFIG_HAL_BOARD == HAL_BOARD_SITL
        case RPM_TYPE_SITL:
            drivers[i] = new AP_RPM_SITL(*this, i, state[i]);
            break;
#endif
        }
        if (drivers[i] != nullptr) {
            // we loaded a driver for this instance, so it must be
            // present (although it may not be healthy)
            num_instances = i+1; // num_instances is a high-water-mark
        }
    }
}

/*
  update RPM state for all instances. This should be called by main loop
 */
void AP_RPM::update(void)
{
    for (uint8_t i=0; i<num_instances; i++) {
        if (drivers[i] != nullptr) {
            if (_type[i] == RPM_TYPE_NONE) {
                // allow user to disable an RPM sensor at runtime and force it to re-learn the quality if re-enabled.
                state[i].signal_quality = 0;
                continue;
            }

            drivers[i]->update();
        }
    }
}

/*
  check if an instance is healthy
 */
bool AP_RPM::healthy(uint8_t instance) const
{
    if (instance >= num_instances || _type[instance] == RPM_TYPE_NONE) {
        return false;
    }

    // check that data quality is above minimum required
    if (state[instance].signal_quality < _quality_min[0]) {
        return false;
    }

    return true;
}

/*
  check if an instance is activated
 */
bool AP_RPM::enabled(uint8_t instance) const
{
    if (instance >= num_instances) {
        return false;
    }
    // if no sensor type is selected, the sensor is not activated.
    if (_type[instance] == RPM_TYPE_NONE) {
        return false;
    }
    return true;
}

/*
  get RPM value, return true on success
 */
bool AP_RPM::get_rpm(uint8_t instance, float &rpm_value) const
{
    if (!healthy(instance)) {
        return false;
    }
    rpm_value = state[instance].rate_rpm;
    return true;
}

// check settings are valid
bool AP_RPM::arming_checks(size_t buflen, char *buffer) const
{
    for (uint8_t i=0; i<RPM_MAX_INSTANCES; i++) {
        switch (_type[i]) {
        case RPM_TYPE_PWM:
        case RPM_TYPE_PIN:
            if (_pin[i] == -1) {
                hal.util->snprintf(buffer, buflen, "RPM[%u] no pin set", i + 1);
                return false;
            }
            if (!hal.gpio->valid_pin(_pin[i])) {
                hal.util->snprintf(buffer, buflen, "RPM[%u] pin %d invalid", unsigned(i + 1), int(_pin[i].get()));
                return false;
            }
            break;
        }
    }
    return true;
}

// singleton instance
AP_RPM *AP_RPM::_singleton;

namespace AP {

AP_RPM *rpm()
{
    return AP_RPM::get_singleton();
}

}
