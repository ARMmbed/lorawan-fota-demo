/**
 * @file    ISL29011.h
 * @brief   Device driver - ISL29011 Ambient Light/IR Proximity Sensor
 * @author  Tim Barr
 * @version 1.0
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
 */
 
#ifndef ISL29011_H
#define ISL29011_H

#include "mbed.h"

/** Using the Multitech MTDOT-EVB
 *
 * Example:
 * @code
 *  #include "mbed.h"
 *  #include "ISL29011.h"
 *

 * 
 *  int main() 
 *  {

 *  }
 * @endcode
 */

/**
 *  @class ISL29011
 *  @brief API abstraction for the ISL29011 Ambient Light/Proximity IC
 *  initial version will be polling only. Interrupt service and rtos support will
 *  be added at a later point
 */ 
class ISL29011
{  
public:

    /**
     * @static INT_FLG
     * @brief Interrupt flag bit
     */
    uint8_t static const INT_FLG = 0x04;

    /**
     * @static CT_16BIT
     * @brief Conversion time in usec for 16 bit setting
     */
    uint32_t static const CT_16BIT = 90000;

    /**
     * @static CT_12BIT
     * @brief conversion time in usec for 12 bit setting
     */
	uint32_t static const CT_12BIT = 5630;

	/**
     * @static CT_8BIT
     * @brief conversion time in usec for 8 bit setting
     */
	uint32_t static const CT_8BIT  = 351;

	/**
     * @static CT_4BIT
     * @brief conversion time in usec for 4 bit setting
     */
	uint32_t static const CT_4BIT  = 22;


    /**
     * @enum OP_MODE
     * @brief operating mode of ILS29011
     */
    enum OPERATION_MODE
	{
    	PWR_DOWN  = 0x00,
		ALS_ONCE  = 0x20,
		IR_ONCE   = 0x04,
		PROX_ONCE = 0xA0,
		ALS_CONT  = 0xC0,
		IR_CONT   = 0xE0,
		PROX_CONT
	};

  /**
     * @enum INT_PERSIST
     * @brief Number of cycles measurement needs to be out of threshold to generate an interrupt
     */
    enum INT_PERSIST
	{
    	NUMCYCLE_1 = 0x00,
		NUMCYCLE_2, NUMCYCLE_4, NUMCYCYLE_8, NUMCYCLE_16
	};
    
    /**
     * @enum PROX_SCHEME
     * @brief Dynamic Range scheme for IR proximity
     */
    enum PROX_SCHEME
	{
    	PROX_FULL = 0x00, /* full n (4,8,12,16) bit data */
		PROX_NR = 0x80    /* n-1 (3,7,11,15) bit data */
	};

    /**
     *  @enum MOD_FREQ
     *  @brief Modulation frequency of Proximity LED
     */
    enum MOD_FREQ
    {
        FREQ_DC   = 0x00, /* No modulation of LED */
        FREQ_360k = 0x40  /* Proximity LED modulated at 360 kHz  */
    };

    /**
     *  @enum LED_DRIVE
     *  @brief Sets the drive current of the IR Proximity LED
     */
    enum LED_DRIVE
    {
        LED_12_5 = 0x00,  /* 12.5 mA current drive */
        LED_25   = 0x10,  /* 25 mA current drive */
		LED_50   = 0x20,  /* 50 mA current drive */
		LED_100  = 0x30   /* 100 mA current driver */
    };

     /**
     * @enum ADC_RESOLUTION
     * @brief Measurement resolution for ADC
     */
    enum ADC_RESOLUTION
	{
    	ADC_16BIT  = 0x00,
		ADC_12BIT  = 0x04,
		ADC_8BIT   = 0x08,
		ADC_4BIT   = 0x0B
	};

    /**
     * @enum LUX_RANGE
     * @brief Setting for LUX Range of ADC
     */
    enum LUX_RANGE
	{
    	RNG_1000 = 0x00,	/* Full scale 1,000 LUX */
		RNG_4000,			/* Full scale 4,000 LUX */
		RNG_16000,			/* Full scale 16,000 LUX */
		RNG_64000			/* Full scale 64,000 LUX */
	};

	/**
     *  @enum REGISTER
     *  @brief The device register map
     */
    enum REGISTER
    {
        COMMAND1 = 0x00,
        COMMAND2, DATA_LSB, DATA_MSB, INT_LT_LSB, INT_LT_MSB, INT_HT_LSB, INT_HT_MSB
    };
        
    /** Create the ISL29011 object
     *  @param i2c - A defined I2C object
     *  @param InterruptIn* - pointer to a defined InterruptIn object. Default to NULL if polled
     */ 
    ISL29011(I2C &i2c, InterruptIn* isl_int = NULL);
    
    /** Get the data
     *  @return The last valid LUX reading from the ambient light sensor
     */
    uint16_t getData(void);
    
    /** Setup the ISL29011 measurement mode
     *  @op_mode - Operating moe of sensor using the OPERATION_MODE enum
     *  @return status of command
     */
    uint8_t setMode(OPERATION_MODE op_mode) const;

    /** Set Interrupt Threshold persistence
     *  @int_persist - Sets the Interrupt Persistence Threshold using the INT_PERSIST enum
     *  @return status of command
     *  TODO - Still need to add interrupt support code
     */
    uint8_t setPersistence(INT_PERSIST int_persist) const;

    /** Set Proximity measurement parameters
     *  @prox_scheme - Sets the Proximity measurement scheme using the PROX_SCHEME enum
     *  @mod_freq - Sets the Moduletion Frequency using the MOD_FREQ enum
     *  @led_drive - Sets the LED Drive current for Proximity mode using the LED_DRIVE enum
     *  @return status of command
     *  NOTE: function added for completeness. MTDOT-EVB does not have IR LED installed at this time
     */
    uint8_t setProximity(PROX_SCHEME prox_scheme, MOD_FREQ mod_freq, LED_DRIVE led_drive) const;
    
    /** Set ADC Resolution
     *  @adc_resolution - Sets the ADC resolution using the ADC_RESOLUTION enum
     *  @return status of command
     */
    uint8_t setResolution(ADC_RESOLUTION adc_resolution) const;
 
    /** Set the LUX Full Scale measurement range
     *  @lux_range - Sets the maximum measured Lux value usngthe LUX_RANGE enum
     *  @return status of command
     */
    uint8_t setRange(LUX_RANGE lux_range ) const;
    
private:
    
    I2C						*_i2c;
    InterruptIn				*_isl_int;
    bool					_polling_mode;
    uint8_t static const	_i2c_addr = (0x44 << 1);
    uint16_t				_lux_data;
    
    /* Initialize the ISL29011 device
    */
    uint8_t init(void);

    /*
     * Write to a register (exposed for debugging reasons)
     *  Note: most writes are only valid in stop mode
     *  @param reg - The register to be written
     *  @param data - The data to be written
     */
    uint8_t writeRegister(uint8_t const reg, uint8_t const data) const;
    
    /*
     * Read from a register (exposed for debugging reasons)
     *  @param reg - The register to read from
     *  @return The register contents
     */
    uint8_t readRegister(uint8_t const reg, char* data, uint8_t count = 1) const;

};

#endif
