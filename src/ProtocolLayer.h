/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2017 Semtech

Description: 	Firmware update over the air with LoRa proof of concept
				General functions for the (de)fragmentation algorithm

*/

#ifndef __PROTOCOLLAYER_H
#define __PROTOCOLLAYER_H

#include "mbed.h"

#define LORAWAN_APP_PROTOCOL_DATA_MAX_SIZE 100
#define LORAWAN_PROTOCOL_DEFAULT_DATARATE DR_5
#define LORAWAN_PROTOCOL_PORT 200

#define MC_GROUP_STATUS_REQ 0x01
#define MC_GROUP_STATUS_ANS 0x01
#define MC_GROUP_SETUP_REQ  0x02
#define MC_GROUP_SETUP_ANS  0x02
#define MC_GROUP_DELETE_REQ 0x03
#define MC_GROUP_DELETE_ANS 0x03
#define MC_CLASSC_SESSION_REQ  0x04
#define MC_CLASSC_SESSION_ANS  0x04
#define MC_CLASSC_SESSION_REQ_LENGTH 0xa
#define MC_CLASSC_SESSION_ANS_LENGTH 0x5
#define FRAGMENTATION_ON_GOING 0xFFFFFFFF
#define FRAGMENTATION_NOT_STARTED 0xFFFFFFFE
#define FRAGMENTATION_FINISH 0x0
#define MAX_UPLINK_T0_UIFCNTREF 0x3

#define FRAG_STATUS_REQ 0x01
#define FRAG_STATUS_ANS 0x01
#define FRAG_SESSION_SETUP_REQ  0x02
#define FRAG_SESSION_SETUP_ANS  0x02
#define FRAG_SESSION_DELETE_REQ 0x03
#define FRAG_SESSION_DELETE_ANS 0x03
#define DATA_BLOCK_AUTH_REQ  0x05
#define DATA_BLOCK_AUTH_ANS  0x05
#define DATA_FRAGMENT  0x08
#define FRAG_SESSION_SETUP_REQ_LENGTH 0x7

#define  FRAG_SESSION_SETUP_ANS_LENGTH 0x2
#define  DATA_BLOCK_AUTH_REQ_LENGTH 0xa
#define  LORAWAN_APP_FTM_PACKAGE_DATA_MAX_SIZE 20

#define REDUNDANCYMAX 240

#define DELAY_BW2FCNT  10 // 5s
#define STATUS_ERROR 1
#define STATUS_OK 0

typedef struct sMcGroupSetParams {
    uint8_t McGroupIDHeader;

    uint32_t McAddr;

    uint8_t McKey[16];

    uint16_t McCountMSB;

    uint32_t Validity;
} McGroupSetParams_t;

/*!
 * Global  McClassCSession parameters
 */
typedef struct sMcClassCSessionParams
{
  	/*!
     * is the identifier of the multicast  group being used.
     */
    uint8_t McGroupIDHeader ;
    /*!
     * encodes the maximum length in seconds of the multicast fragmentation session
     */
    uint32_t TimeOut;

    /*!
     * encodes the maximum length in seconds of the multicast fragmentation session
     */
    uint32_t TimeToStart;

	  /*!
     * encodes the maximum length in seconds of the multicast fragmentation session ans
     */
     int32_t TimeToStartRec;

  	/*!
     * equal to the 8LSBs of the device�s uplink frame counter used by the network as the reference to provide the session timing information
     */
    uint8_t UlFCountRef;

	  /*!
     * reception frequency
     */
    uint32_t DLFrequencyClassCSession;

	   /*!
     * datarate of the current class c session
     */
    uint8_t DataRateClassCSession ;

		 /*!
     * bit signals the server that the timing information of the uplink
		 * specified by UlFCounter is no longer available
     */
    uint8_t UlFCounterError ;

}McClassCSessionParams_t;


/*!
 * Global  DataBlockTransport parameters
 */
typedef struct sDataBlockTransportParams
{
    /*!
     * Channels TX power
     */
    int8_t ChannelsTxPower;
    /*!
     * Channels data rate
     */
    int8_t ChannelsDatarate;

}DataBlockTransportParams_t;

/*!
 * Global  FTMPackage parameters
 */
typedef struct sFTMPackageParams
{
        /*!
     * identifies the fragmentation session and contains the following fields
     */
    uint8_t FragSession ;
    /*!
     * specifies the total number of fragments of the data block to be transported
           * during the coming multicast fragmentation session
     */
    uint16_t NbFrag;

    /*!
     * is the size in byte of each fragment.
     */
    uint8_t FragSize;

        /*!
     * FragmentationMatrix   encodes the type of fragmentation algorithm used
           * Block_ack_delay encodes the amplitude of the random delay that devices have
     * to wait between the moment they have reconstructed the full data block and the moment
           * they transmit the DataBlockAuthReq message
     */
    uint8_t Encoding;
                /*!
     * specifies the total number of Redundancy fragments of the data block to be transported
           * during the coming multicast fragmentation session
     */
    uint16_t Redundancy;

    uint8_t Padding;

}FTMPackageParams_t;

#endif
