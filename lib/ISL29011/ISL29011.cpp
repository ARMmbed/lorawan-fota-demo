/**
 * @file    ISL29011.cpp
 * @brief   Device driver - ISL29011 Ambient Light/IR Proximity Sensor IC
 * @author  Tim Barr
 * @version 1.01
 * @see     http://www.intersil.com/content/dam/Intersil/documents/isl2/isl29011.pdf
 *
 * Copyright (c) 2015
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * 1.01 TAB 7/6/15  Removed "= NULL" reference from object call. Only needs to be in .h file
 */

#include "ISL29011.h"
#include "mbed_debug.h"

ISL29011::ISL29011(I2C &i2c, InterruptIn* isl_int)
{
    _i2c =  &i2c;
    _isl_int = isl_int;

    ISL29011::init();

    return;
}

uint8_t ISL29011::init(void)
{
    uint8_t result = 0;

    _i2c->frequency(400000);

    // Reset all registers to POR values
    result = ISL29011::writeRegister(COMMAND1, 0x00);

    if (result == 0) {
        if (_isl_int == NULL)
            _polling_mode = true;
        else _polling_mode = false;

        result = ISL29011::writeRegister(COMMAND2, 0x00);
        result = ISL29011::writeRegister(INT_LT_LSB, 0x00);
        result = ISL29011::writeRegister(INT_LT_MSB, 0x00);
        result = ISL29011::writeRegister(INT_HT_LSB, 0xFF);
        result = ISL29011::writeRegister(INT_HT_MSB, 0xFF);
    }

    if(result != 0) {
        debug("ILS29011:init failed\n\r");
    }

    return result;
}

/** Get the data
 *  @return The last valid LUX reading from the ambient light sensor
 */
uint16_t ISL29011::getData(void)
{
    if (_polling_mode) {
        char datain[2];

        ISL29011::readRegister(DATA_LSB, datain, 2);
        _lux_data = (datain[1] << 8) | datain[0];
    }
    return _lux_data;
}

/** Setup the ISL29011 measurement mode
 *  @return status of command
 */
uint8_t ISL29011::setMode(OPERATION_MODE op_mode) const
{
    uint8_t result = 0;
    char datain[1];
    char dataout;


    result |= ISL29011::readRegister(COMMAND1,datain);
    dataout = (datain[0] & 0x1F) | op_mode;
    result |= ISL29011::writeRegister(COMMAND1, dataout);
    return result;

}

/** Set Interrupt Persistence Threshold
 *  @return status of command
 */
uint8_t ISL29011::setPersistence(INT_PERSIST int_persist) const
{
    uint8_t result = 0;
    char datain[1];
    char dataout;


    result |= ISL29011::readRegister(COMMAND1,datain);
    dataout = (datain[0] & 0xFC) | int_persist;
    result |= ISL29011::writeRegister(COMMAND1, dataout);

    return result;
}

/** Set Proximity measurement parameters
 *  @return status of command
 */
uint8_t ISL29011::setProximity(PROX_SCHEME prox_scheme, MOD_FREQ mod_freq, LED_DRIVE led_drive) const
{
    uint8_t result = 0;
    char datain[1];
    char dataout;


    result |= ISL29011::readRegister(COMMAND2,datain);
    dataout = (datain[0] & 0x0F) | prox_scheme | mod_freq | led_drive;
    result |= ISL29011::writeRegister(COMMAND2, dataout);

    return result;
}

/** Set ADC Resolution
 *  @return status of command
 */
uint8_t ISL29011::setResolution(ADC_RESOLUTION adc_resolution) const
{
    uint8_t result = 0;
    char datain[1];
    char dataout;


    result |= ISL29011::readRegister(COMMAND2,datain);
    dataout = (datain[0] & 0xF3) | adc_resolution;
    result |= ISL29011::writeRegister(COMMAND2, dataout);

    return result;
}

/** Set the LUX Full Scale measurement range
 *  @return status of command
 */
uint8_t ISL29011::setRange(LUX_RANGE lux_range ) const
{
    uint8_t result = 0;
    char datain[1];
    char dataout;


    result |= ISL29011::readRegister(COMMAND2,datain);
    dataout = (datain[0] & 0xFC) | lux_range;
    result |= ISL29011::writeRegister(COMMAND2, dataout);

    return result;
}


uint8_t ISL29011::writeRegister(uint8_t const reg, uint8_t const data) const
{
    char buf[2] = {reg, data};
    uint8_t result = 0;

    buf[0] = reg;
    buf[1] = data;

    result |= _i2c->write(_i2c_addr, buf, 2);

    if(result != 0) {
        debug("ISL29011:writeRegister failed\n\r");
    }

    return result;
}

uint8_t ISL29011::readRegister(uint8_t const reg, char* data, uint8_t count) const
{
    uint8_t result = 0;
    char reg_out[1];

    reg_out[0] = reg;
    result |= _i2c->write(_i2c_addr,reg_out,1,true);

    if(result != 0) {
        debug("ISL29011::readRegister failed write\n\r");
        return result;
    }

    result |= _i2c->read(_i2c_addr,data,count,false);

    if(result != 0) {
        debug("ISL29011::readRegister failed read\n\r");
    }

    return result;
}


