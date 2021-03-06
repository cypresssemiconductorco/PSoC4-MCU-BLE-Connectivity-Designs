/*******************************************************************************
* File Name: main.c
*
* Version: 1.0
*
*  This example demonstrates how to setup an IPv6 communication infrastructure 
*  between two devices over a BLE transport using L2CAP channel. Creation 
*  and transmission of IPv6 packets over BLE is not part of this example.
*
*  Router sends generated packets with different content to Node in the loop 
*  and validate them with the afterwards received data packet. Node simply wraps
*  received data coming from the Node, back to the Router.
*
* Note:
*
* Hardware Dependency:
*  BLE DONGLE
*
********************************************************************************
* Copyright 2016, Cypress Semiconductor Corporation.  All rights reserved.
* You may use this file only in accordance with the license, terms, conditions,
* disclaimers, and limitations in the end user license agreement accompanying
* the software package with which this file was provided.
*******************************************************************************/

#include "main.h"
#include "ble.h"
#include <stdbool.h>

uint16 connIntv;                    /* in milliseconds / 1.25ms */
bool l2capConnected = false;
CYBLE_L2CAP_CBFC_CONN_CNF_PARAM_T l2capParameters;

CYBLE_GAP_BD_ADDR_T peerAddr[CYBLE_MAX_ADV_DEVICES];
uint8 advDevices = 0u;
uint8 deviceN = 0u;

uint8 state = STATE_INIT;

static uint16 ipv6LoopbackBuffer[L2CAP_MAX_LEN/2];

uint8 custom_command = 0u;


/****************************************************************************** 
##Function Name: CheckAdvPacketForServiceUuid
*******************************************************************************

Summary:
 This function parses advertising packet and returns not zero value 
 when packet contains service UUID equal to input parameter.

Parameters:
 *eventParam: the pointer to a data structure specified by the event.
 uuid: 16-bit UUID of the service.

Return:
 Not zero value when the advertising packet contains service UUID.

******************************************************************************/
uint32 CheckAdvPacketForServiceUuid(CYBLE_GAPC_ADV_REPORT_T *eventParam, uint16 uuid)
{
    uint32 servicePresent = 0u; 
    uint32 advIndex = 0u;
    uint32 i;
    
    do
    {
        /* Find complete or incomplete Service UUID list field type. */
        if((eventParam->data[advIndex + 1u] == (uint8)CYBLE_GAP_ADV_INCOMPL_16UUID) || 
           (eventParam->data[advIndex + 1u] == (uint8)CYBLE_GAP_ADV_COMPL_16UUID))
        {
            /* Compare uuid values with input parameter */
            for(i = 0u; (i < (eventParam->data[advIndex] - 1u)) && (servicePresent == 0u); i += sizeof(uint16))
            {
                if(CyBle_Get16ByPtr(&eventParam->data[advIndex + 2u + i]) == uuid)
                {
                    servicePresent = 1u;
                }
            }
        }
        advIndex += eventParam->data[advIndex] + 1u;
    }while((advIndex < eventParam->dataLen) && (servicePresent == 0u));    
    
    return(servicePresent);
}


/*******************************************************************************
* Function Name: AppCallBack()
********************************************************************************
*
* Summary:
*   This is an event callback function to receive events from the BLE Component.
*
* Parameters:
*  event - the event code
*  *eventParam - the event parameters
*
* Theory:
* The function is responsible for handling the events generated by the stack.
* It first starts scanning once the stack is initialized. 
* Upon scanning timeout this function enters Hibernate mode.
*
* IPSP protocol multiplexer for L2CAP is registered and the initial Receive 
* Credit Low Mark for Based Flow Control mode is set after CYBLE_EVT_STACK_ON
* event.
* When GAP connection is established, after CYBLE_EVT_GATT_CONNECT_IND event, 
* Router automatically initiates an L2CAP LE credit based connection with a PSM
* set to LE_PSM_IPSP.
* Use '1' command to generate and send first Data packet to Node though IPSP 
* channel. Sent data will be compared with the received data in response packed 
* after CYBLE_EVT_L2CAP_CBFC_DATA_READ event. When no failure observed, new 
* packed will be generated and send to Node. Otherwise transfer will be stopped
* and "Wraparound failed" message will indicate failure.
*
*******************************************************************************/
void AppCallback(uint32 event, void* eventParam)
{
    CYBLE_API_RESULT_T apiResult;
    CYBLE_GAP_BD_ADDR_T localAddr;
    CYBLE_GAPC_ADV_REPORT_T *advReport;
    uint8 newDevice = 0u;
    uint16 i;
    
    switch (event)
	{
        /**********************************************************
        *                       General Events
        ***********************************************************/
		case CYBLE_EVT_STACK_ON: /* This event received when component is Started */
            /* Register IPSP protocol multiplexer to L2CAP and 
            *  set the initial Receive Credit Low Mark for Based Flow Control mode 
            */
            apiResult = CyBle_L2capCbfcRegisterPsm(CYBLE_L2CAP_PSM_LE_PSM_IPSP, LE_WATER_MARK_IPSP);
            if(apiResult != CYBLE_ERROR_OK)
            {
                DBG_PRINTF("CyBle_L2capCbfcRegisterPsm API Error: %d \r\n", apiResult);
            }
            
            /* Start Limited Discovery */
            apiResult = CyBle_GapcStartScan(CYBLE_SCANNING_FAST);                   
            if(apiResult != CYBLE_ERROR_OK)
            {
                DBG_PRINTF("StartScan API Error: %xd \r\n", apiResult);
            }
            else
            {
                DBG_PRINTF("Bluetooth On, StartScan with addr: ");
                localAddr.type = 0u;
                CyBle_GetDeviceAddress(&localAddr);
                for(i = CYBLE_GAP_BD_ADDR_SIZE; i > 0u; i--)
                {
                    DBG_PRINTF("%2.2x", localAddr.bdAddr[i-1]);
                }
                DBG_PRINTF("\r\n");
            }
            break;
		case CYBLE_EVT_TIMEOUT: 
            DBG_PRINTF("CYBLE_EVT_TIMEOUT: %x \r\n", *(CYBLE_TO_REASON_CODE_T *)eventParam);
			break;
		case CYBLE_EVT_HARDWARE_ERROR:    /* This event indicates that some internal HW error has occurred. */
            DBG_PRINTF("Hardware Error: %x \r\n", *(uint8 *)eventParam);
			break;
        case CYBLE_EVT_HCI_STATUS:
            DBG_PRINTF("CYBLE_EVT_HCI_STATUS: %x \r\n", *(uint8 *)eventParam);
			break;
    	case CYBLE_EVT_STACK_BUSY_STATUS:
        #if(DEBUG_UART_FULL)  
            DBG_PRINTF("CYBLE_EVT_STACK_BUSY_STATUS: %x\r\n", CyBle_GattGetBusyStatus());
        #endif /* DEBUG_UART_FULL */
            break;
            
        /**********************************************************
        *                       GAP Events
        ***********************************************************/
        /* This event provides the remote device lists during discovery process. */
        case CYBLE_EVT_GAPC_SCAN_PROGRESS_RESULT:
            advReport = (CYBLE_GAPC_ADV_REPORT_T *)eventParam;
            /* Filter and connect only to nodes that advertise IPSS in ADV payload */
            if(CheckAdvPacketForServiceUuid(advReport, CYBLE_UUID_INTERNET_PROTOCOL_SUPPORT_SERVICE) != 0u)
            {
                DBG_PRINTF("Advertisement report: eventType = %x, peerAddrType - %x, ", 
                    advReport->eventType, advReport->peerAddrType);
                DBG_PRINTF("peerBdAddr - ");
                for(newDevice = 1u, i = 0u; i < advDevices; i++)
                {
                    /* Compare device address with already logged one */
                    if((memcmp(peerAddr[i].bdAddr, advReport->peerBdAddr, CYBLE_GAP_BD_ADDR_SIZE) == 0))
                    {
                        DBG_PRINTF("%x: ",i);
                        newDevice = 0u;
                        break;
                    }
                }
                if(newDevice != 0u)
                {
                    if(advDevices < CYBLE_MAX_ADV_DEVICES)
                    {
                        memcpy(peerAddr[advDevices].bdAddr, advReport->peerBdAddr, CYBLE_GAP_BD_ADDR_SIZE);
                        peerAddr[advDevices].type = advReport->peerAddrType;
                        DBG_PRINTF("%x: ",advDevices);
                        advDevices++;
                    }
                }
                for(i = CYBLE_GAP_BD_ADDR_SIZE; i > 0u; i--)
                {
                    DBG_PRINTF("%2.2x", advReport->peerBdAddr[i-1]);
                }
                DBG_PRINTF(", rssi - %d dBm", advReport->rssi);
            #if(DEBUG_UART_FULL)  
                DBG_PRINTF(", data - ");
                for( i = 0; i < advReport->dataLen; i++)
                {
                    DBG_PRINTF("%2.2x ", advReport->data[i]);
                }
            #endif /* DEBUG_UART_FULL */
                DBG_PRINTF("\r\n");
            }
            break;
	    case CYBLE_EVT_GAPC_SCAN_START_STOP:
            DBG_PRINTF("CYBLE_EVT_GAPC_SCAN_START_STOP, state: %x\r\n", CyBle_GetState());
            if(CYBLE_STATE_DISCONNECTED == CyBle_GetState())
            {
                if(state == STATE_CONNECTING)
                {
                    DBG_PRINTF("GAPC_END_SCANNING\r\n");
                    /* Connect to selected device */
                    apiResult = CyBle_GapcConnectDevice(&peerAddr[deviceN]);
                    if(apiResult != CYBLE_ERROR_OK)
                    {
                        DBG_PRINTF("ConnectDevice API Error: %x \r\n", apiResult);
                    }
                }
                else
                {
                    /* Fast scanning period complete,
                     * go to low power mode (Hibernate mode) and wait for an external
                     * user event to wake up the device again */
                    DBG_PRINTF("Hibernate \r\n");
                    UpdateLedState();
                #if (DEBUG_UART_ENABLED == ENABLED)
                    while((UART_DEB_SpiUartGetTxBufferSize() + UART_DEB_GET_TX_FIFO_SR_VALID) != 0);
                #endif /* (DEBUG_UART_ENABLED == ENABLED) */
                    SW2_ClearInterrupt();
                    Wakeup_Interrupt_ClearPending();
                    Wakeup_Interrupt_Start();
                    CySysPmHibernate();
                }
            }
            break;
        case CYBLE_EVT_GAP_AUTH_REQ:
            DBG_PRINTF("CYBLE_EVT_AUTH_REQ: security=%x, bonding=%x, ekeySize=%x, err=%x \r\n", 
                (*(CYBLE_GAP_AUTH_INFO_T *)eventParam).security, 
                (*(CYBLE_GAP_AUTH_INFO_T *)eventParam).bonding, 
                (*(CYBLE_GAP_AUTH_INFO_T *)eventParam).ekeySize, 
                (*(CYBLE_GAP_AUTH_INFO_T *)eventParam).authErr);
            break;
        case CYBLE_EVT_GAP_PASSKEY_ENTRY_REQUEST:
            DBG_PRINTF("CYBLE_EVT_PASSKEY_ENTRY_REQUEST press 'p' to enter passkey \r\n");
            break;
        case CYBLE_EVT_GAP_PASSKEY_DISPLAY_REQUEST:
            DBG_PRINTF("CYBLE_EVT_PASSKEY_DISPLAY_REQUEST %6.6ld \r\n", *(uint32 *)eventParam);
            break;
        case CYBLE_EVT_GAP_KEYINFO_EXCHNGE_CMPLT:
            DBG_PRINTF("CYBLE_EVT_GAP_KEYINFO_EXCHNGE_CMPLT \r\n");
            break;
        case CYBLE_EVT_GAP_AUTH_COMPLETE:
            DBG_PRINTF("AUTH_COMPLETE: security:%x, bonding:%x, ekeySize:%x, authErr %x \r\n", 
                ((CYBLE_GAP_AUTH_INFO_T *)eventParam)->security, 
                ((CYBLE_GAP_AUTH_INFO_T *)eventParam)->bonding, 
                ((CYBLE_GAP_AUTH_INFO_T *)eventParam)->ekeySize, 
                ((CYBLE_GAP_AUTH_INFO_T *)eventParam)->authErr);
            break;
        case CYBLE_EVT_GAP_AUTH_FAILED:
            DBG_PRINTF("CYBLE_EVT_AUTH_FAILED: %x \r\n", *(uint8 *)eventParam);
            break;
        case CYBLE_EVT_GAP_DEVICE_CONNECTED:
            connIntv = ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->connIntv * 5u /4u;
            DBG_PRINTF("CYBLE_EVT_GAP_DEVICE_CONNECTED: connIntv = %d ms \r\n", connIntv);
            break;
        case CYBLE_EVT_GAPC_CONNECTION_UPDATE_COMPLETE:
            connIntv = ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->connIntv * 5u /4u;
            DBG_PRINTF("CYBLE_EVT_GAPC_CONNECTION_UPDATE_COMPLETE: %x, %x, %x, %x \r\n", 
                ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->status,
                ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->connIntv,
                ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->connLatency,
                ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->supervisionTO
            );
            break;
        case CYBLE_EVT_GAP_DEVICE_DISCONNECTED:
            DBG_PRINTF("CYBLE_EVT_GAP_DEVICE_DISCONNECTED: %x\r\n", *(uint8 *)eventParam);
            apiResult = CyBle_GapcStartScan(CYBLE_SCANNING_FAST);                   /* Start Limited Discovery */
            if(apiResult != CYBLE_ERROR_OK)
            {
                DBG_PRINTF("StartScan API Error: %xd \r\n", apiResult);
            }
            break;
        case CYBLE_EVT_GAP_ENCRYPT_CHANGE:
            DBG_PRINTF("CYBLE_EVT_GAP_ENCRYPT_CHANGE: %x \r\n", *(uint8 *)eventParam);
            break;
            
        /**********************************************************
        *                       GATT Events
        ***********************************************************/
        case CYBLE_EVT_GATT_CONNECT_IND:
            DBG_PRINTF("CYBLE_EVT_GATT_CONNECT_IND: %x, %x \r\n", cyBle_connHandle.attId, cyBle_connHandle.bdHandle);
            /* Send an L2CAP LE credit based connection request with a PSM set to LE_PSM_IPSP.
             * Once the peer responds, CYBLE_EVT_L2CAP_CBFC_CONN_CNF 
             * event will come up on this device.
             */
            
            {
                /* L2CAP Channel parameters for the local device */
                CYBLE_L2CAP_CBFC_CONNECT_PARAM_T cbfcConnParameters = 
                {
                    CYBLE_L2CAP_MTU,         /* MTU size of this device */
                    CYBLE_L2CAP_MPS,         /* MPS size of this device */
                    LE_DATA_CREDITS_IPSP     /* Initial Credits given to peer device for Tx */
                };
                apiResult = CyBle_L2capCbfcConnectReq(cyBle_connHandle.bdHandle, CYBLE_L2CAP_PSM_LE_PSM_IPSP, 
                                      CYBLE_L2CAP_PSM_LE_PSM_IPSP, &cbfcConnParameters);
                if(apiResult != CYBLE_ERROR_OK)
                {
                    DBG_PRINTF("CyBle_L2capCbfcConnectReq API Error: %d \r\n", apiResult);
                }
                else
                {
                    DBG_PRINTF("L2CAP channel connection request sent. \r\n");
                }
            }
            break;
        case CYBLE_EVT_GATT_DISCONNECT_IND:
            DBG_PRINTF("CYBLE_EVT_GATT_DISCONNECT_IND \r\n");
            break;
        case CYBLE_EVT_GATTC_ERROR_RSP:
            DBG_PRINTF("GATT_ERROR_RSP: opcode: %x,  handle: %x,  errorcode: %x \r\n",
                ((CYBLE_GATTC_ERR_RSP_PARAM_T *)eventParam)->opCode,
                ((CYBLE_GATTC_ERR_RSP_PARAM_T *)eventParam)->attrHandle,
                ((CYBLE_GATTC_ERR_RSP_PARAM_T *)eventParam)->errorCode);
            break;
            
        /**********************************************************
        *                       L2CAP Events
        ***********************************************************/
        case CYBLE_EVT_L2CAP_CBFC_CONN_CNF:
            l2capParameters = *((CYBLE_L2CAP_CBFC_CONN_CNF_PARAM_T *)eventParam);
            DBG_PRINTF("CYBLE_EVT_L2CAP_CBFC_CONN_CNF: bdHandle=%d, lCid=%d, responce=%d", 
                l2capParameters.bdHandle,
                l2capParameters.lCid,
                l2capParameters.response);
            DBG_PRINTF(", connParam: mtu=%d, mps=%d, credit=%d\r\n", 
                l2capParameters.connParam.mtu,
                l2capParameters.connParam.mps,
                l2capParameters.connParam.credit);
            l2capConnected = true;
            break;

        case CYBLE_EVT_L2CAP_CBFC_DISCONN_IND:
            DBG_PRINTF("CYBLE_EVT_L2CAP_CBFC_DISCONN_IND: %d \r\n", *(uint16 *)eventParam);
            l2capConnected = false;
            break;

        /* Following two events are required, to receive data */        
        case CYBLE_EVT_L2CAP_CBFC_DATA_READ:
            {
                CYBLE_L2CAP_CBFC_RX_PARAM_T *rxDataParam = (CYBLE_L2CAP_CBFC_RX_PARAM_T *)eventParam;
                DBG_PRINTF("<- EVT_L2CAP_CBFC_DATA_READ: lCid=%d, result=%d, len=%d", 
                    rxDataParam->lCid,
                    rxDataParam->result,
                    rxDataParam->rxDataLength);
            #if(DEBUG_UART_FULL)  
                DBG_PRINTF(", data:");
                for(i = 0; i < rxDataParam->rxDataLength; i++)
                {
                    DBG_PRINTF("%2.2x", rxDataParam->rxData[i]);
                }
            #endif /* DEBUG_UART_FULL */
                DBG_PRINTF("\r\n");
                /* Data is received from Node, validate the content */
                if(memcmp(((uint8 *)ipv6LoopbackBuffer), rxDataParam->rxData, L2CAP_MAX_LEN) != 0u)
                {
                    DBG_PRINTF("Wraparound failed \r\n");
                }
                else
                {
                    /* Send new Data packet to Node though IPSP channel  */
                    custom_command = '1';
                }
            }
            break;

        case CYBLE_EVT_L2CAP_CBFC_RX_CREDIT_IND:
            {
                CYBLE_L2CAP_CBFC_LOW_RX_CREDIT_PARAM_T *rxCreditParam = (CYBLE_L2CAP_CBFC_LOW_RX_CREDIT_PARAM_T *)eventParam;
                DBG_PRINTF("CYBLE_EVT_L2CAP_CBFC_RX_CREDIT_IND: lCid=%d, credit=%d \r\n", 
                    rxCreditParam->lCid,
                    rxCreditParam->credit);

                /* This event informs that receive credits reached low mark. 
                 * If the device expects more data to receive, send more credits back to the peer device.
                 */
                apiResult = CyBle_L2capCbfcSendFlowControlCredit(rxCreditParam->lCid, LE_DATA_CREDITS_IPSP);
                if(apiResult != CYBLE_ERROR_OK)
                {
                    DBG_PRINTF("CyBle_L2capCbfcSendFlowControlCredit API Error: %d \r\n", apiResult);
                }
            }
            break;

        /* Following events are required, to send data */
        case CYBLE_EVT_L2CAP_CBFC_TX_CREDIT_IND:
            DBG_PRINTF("CYBLE_EVT_L2CAP_CBFC_TX_CREDIT_IND: lCid=%d, result=%d, credit=%d \r\n", 
                ((CYBLE_L2CAP_CBFC_LOW_TX_CREDIT_PARAM_T *)eventParam)->lCid,
                ((CYBLE_L2CAP_CBFC_LOW_TX_CREDIT_PARAM_T *)eventParam)->result,
                ((CYBLE_L2CAP_CBFC_LOW_TX_CREDIT_PARAM_T *)eventParam)->credit);
            break;
        
        case CYBLE_EVT_L2CAP_CBFC_DATA_WRITE_IND:
            #if(DEBUG_UART_FULL)
            {
                CYBLE_L2CAP_CBFC_DATA_WRITE_PARAM_T *writeDataParam = (CYBLE_L2CAP_CBFC_DATA_WRITE_PARAM_T*)eventParam;
                DBG_PRINTF("CYBLE_EVT_L2CAP_CBFC_DATA_WRITE_IND: lCid=%d \r\n", writeDataParam->lCid);
            }
            #endif /* DEBUG_UART_FULL */
            break;
            
        /**********************************************************
        *                       Discovery Events 
        ***********************************************************/
        case CYBLE_EVT_GATTC_CHAR_DUPLICATION:
        case CYBLE_EVT_GATTC_DESCR_DUPLICATION:
        case CYBLE_EVT_GATTC_SRVC_DUPLICATION: 
            
            DBG_PRINTF("DUPLICATION, UUID: %x \r\n", *(uint16 *)eventParam);
            break;
        case CYBLE_EVT_GATTC_SRVC_DISCOVERY_FAILED:
            DBG_PRINTF("DISCOVERY_FAILED \r\n");
            break;
        case CYBLE_EVT_GATTC_SRVC_DISCOVERY_COMPLETE:
            DBG_PRINTF("CYBLE_EVT_SERVER_SRVC_DISCOVERY_COMPLETE \r\n");
            break;
        case CYBLE_EVT_GATTC_INCL_DISCOVERY_COMPLETE:
            DBG_PRINTF("CYBLE_EVT_SERVER_INCL_DISCOVERY_COMPLETE \r\n");
            break;
        case CYBLE_EVT_GATTC_CHAR_DISCOVERY_COMPLETE:
            DBG_PRINTF("CYBLE_EVT_SERVER_CHAR_DISCOVERY_COMPLETE ");
            break;
        case CYBLE_EVT_GATTC_DISCOVERY_COMPLETE:
            DBG_PRINTF("CYBLE_EVT_SERVER_DISCOVERY_COMPLETE \r\n");
            DBG_PRINTF("GATT %x-%x Char: %x, cccd: %x, \r\n", 
                cyBle_serverInfo[CYBLE_SRVI_GATT].range.startHandle,
                cyBle_serverInfo[CYBLE_SRVI_GATT].range.endHandle,
                cyBle_gattc.serviceChanged.valueHandle,
                cyBle_gattc.cccdHandle);

            DBG_PRINTF("\r\nIPSP %x-%x: ", cyBle_serverInfo[CYBLE_SRVI_IPSS].range.startHandle,
                                       cyBle_serverInfo[CYBLE_SRVI_IPSS].range.endHandle);
            
            DBG_PRINTF("\r\n");
            break;

        /**********************************************************
        *                       Other Events
        ***********************************************************/
        case CYBLE_EVT_PENDING_FLASH_WRITE:
            /* Inform application that flash write is pending. Stack internal data 
            * structures are modified and require to be stored in Flash using 
            * CyBle_StoreBondingData() */
            DBG_PRINTF("CYBLE_EVT_PENDING_FLASH_WRITE\r\n");
            break;
        default:
            DBG_PRINTF("OTHER event: %lx \r\n", event);
			break;
	}
}


/*******************************************************************************
* Function Name: Timer_Interrupt
********************************************************************************
*
* Summary:
*  Handles the Interrupt Service Routine for the WDT timer.
*  Blinking Blue LED during scanning process.
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
CY_ISR(Timer_Interrupt)
{
    static uint8 led = LED_OFF;
    
    /* Blink LED to indicate that device scans */
    if(CyBle_GetState() == CYBLE_STATE_SCANNING)
    {
        led ^= LED_ON;
        Scanning_LED_Write(led);
    }
}


/*******************************************************************************
* Function Name: UpdateLedState
********************************************************************************
*
* Summary:
*  This function updates led status based on current BLE state.
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void UpdateLedState(void)
{
   
    if(CyBle_GetState() == CYBLE_STATE_DISCONNECTED)
    {   
        Scanning_LED_Write(LED_OFF);
    }
    else if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        Scanning_LED_Write(LED_ON);
    }
    else
    {
        /* Scanning LED is handled in Timer_Interrupt */
    }
}


/*******************************************************************************
* Function Name: LowPowerImplementation()
********************************************************************************
* Summary:
* Implements low power in the project.
*
* Parameters:
* None
*
* Return:
* None
*
* Theory:
* The function tries to enter deep sleep as much as possible - whenever the 
* BLE is idle and the UART transmission/reception is not happening. In all other
* cases, the function tries to enter CPU sleep.
*
*******************************************************************************/
static void LowPowerImplementation(void)
{
    CYBLE_LP_MODE_T bleMode;
    uint8 interruptStatus;
    
    /* For advertising and connected states, implement deep sleep 
     * functionality to achieve low power in the system. For more details
     * on the low power implementation, refer to the Low Power Application 
     * Note.
     */
    if((CyBle_GetState() == CYBLE_STATE_SCANNING) || 
       (CyBle_GetState() == CYBLE_STATE_CONNECTED))
    {
        /* Request BLE subsystem to enter into Deep-Sleep mode between connection and advertising intervals */
        bleMode = CyBle_EnterLPM(CYBLE_BLESS_DEEPSLEEP);
        /* Disable global interrupts */
        interruptStatus = CyEnterCriticalSection();
        /* When BLE subsystem has been put into Deep-Sleep mode */
        if(bleMode == CYBLE_BLESS_DEEPSLEEP)
        {
            /* And it is still there or ECO is on */
            if((CyBle_GetBleSsState() == CYBLE_BLESS_STATE_ECO_ON) || 
               (CyBle_GetBleSsState() == CYBLE_BLESS_STATE_DEEPSLEEP))
            {
                /* Put the CPU into Sleep mode and let SCB to continue sending debug data and receive commands */
                CySysPmSleep();
            }
        }
        else /* When BLE subsystem has been put into Sleep mode or is active */
        {
            /* And hardware hasn't finished Tx/Rx operation - put the CPU into Sleep mode */
            if(CyBle_GetBleSsState() != CYBLE_BLESS_STATE_EVENT_CLOSE)
            {
                CySysPmSleep();
            }
        }
        /* Enable global interrupt */
        CyExitCriticalSection(interruptStatus);
    }
}


/*******************************************************************************
* Function Name: main()
********************************************************************************
* 
* Summary:
*  Main function for the project.
*
* Parameters:
*  None
*
* Return:
*  None
*
* Theory:
*  The function starts BLE and UART components.
*  This function processes all BLE events and also implements the low power 
*  functionality.
*
*******************************************************************************/
int main()
{
    CYBLE_API_RESULT_T apiResult;
    CYBLE_STACK_LIB_VERSION_T stackVersion;
    char8 command;
    
    CyGlobalIntEnable;              /* Enable interrupts */
    UART_DEB_Start();               /* Start communication component */
    DBG_PRINTF("BLE IPSP Router Example Project \r\n");
    
    /* Start BLE component */
    CyBle_Start(AppCallback);    
    
    apiResult = CyBle_GetStackLibraryVersion(&stackVersion);
    if(apiResult != CYBLE_ERROR_OK)
    {
        DBG_PRINTF("CyBle_GetStackLibraryVersion API Error: 0x%2.2x \r\n", apiResult);
    }
    else
    {
        DBG_PRINTF("Stack Version: %d.%d.%d.%d \r\n", stackVersion.majorVersion, 
            stackVersion.minorVersion, stackVersion.patch, stackVersion.buildNumber);
    }

    /* Register Timer_Interrupt() by the WDT COUNTER2 to generate interrupt every second */
    CySysWdtSetInterruptCallback(CY_SYS_WDT_COUNTER2, Timer_Interrupt);
    /* Enable the COUNTER2 ISR handler. */
    CySysWdtEnableCounterIsr(CY_SYS_WDT_COUNTER2);

    for(;;)
    {
        /* Process all the generated events. */
        CyBle_ProcessEvents();

        /* To achieve low power in the device */
        LowPowerImplementation();
        
        if(((command = UART_DEB_UartGetChar()) != 0) || ((custom_command != 0) && (cyBle_busyStatus == 0u)))
        {
            if(custom_command != 0u)
            {
                command = custom_command;
                custom_command = 0u;
            }
            switch(command)
            {
                case 'c':                   /* Send connect request to selected peer device.  */
                    CyBle_GapcStopScan(); 
                    state = STATE_CONNECTING;
                    break;
                case 'v':                   /* Cancel connection request. */
                    apiResult = CyBle_GapcCancelDeviceConnection();
                    DBG_PRINTF("CyBle_GapcCancelDeviceConnection: %x\r\n" , apiResult);
                    break;
                case 'd':                   /* Send disconnect request to peer device. */
                    apiResult = CyBle_GapDisconnect(cyBle_connHandle.bdHandle); 
                    if(apiResult != CYBLE_ERROR_OK)
                    {
                        DBG_PRINTF("DisconnectDevice API Error: %x \r\n", apiResult);
                    }
                    break;
                case 's':                   /* Start discovery procedure. */
                    apiResult = CyBle_GattcStartDiscovery(cyBle_connHandle);
                    DBG_PRINTF("StartDiscovery \r\n");
                    if(apiResult != CYBLE_ERROR_OK)
                    {
                        DBG_PRINTF("StartDiscovery API Error: %x \r\n", apiResult);
                    }
                    break;
                case 'z':                   /* Select specific peer device.  */
                    DBG_PRINTF("Select Device:\n"); 
                    while((command = UART_DEB_UartGetChar()) == 0);
                    if((command >= '0') && (command <= '9'))
                    {
                        deviceN = (uint8)(command - '0');
                        DBG_PRINTF("%c\n",command); /* print number */
                    }
                    else
                    {
                        DBG_PRINTF(" Wrong digit \r\n");
                        break;
                    }
                    break;
                    /**********************************************************
                    *               L2Cap Commands (WrapAround)
                    ***********************************************************/
                case '1':                   /* Send Data packet to node though IPSP channel */
                    {
                        static uint16 counter = 0;
                        static uint16 repeats = 0;
                        uint16 i;
                        
                        DBG_PRINTF("-> CyBle_L2capChannelDataWrite #%d \r\n", repeats++);
                        (void)repeats;
                    #if(DEBUG_UART_FULL)  
                        DBG_PRINTF(", Data:");
                    #endif /* DEBUG_UART_FULL */
                        /* Fill output buffer by counter */
                        for(i = 0u; i < L2CAP_MAX_LEN / 2u; i++)
                        {
                            ipv6LoopbackBuffer[i] = counter++;
                        #if(DEBUG_UART_FULL)  
                            DBG_PRINTF("%4.4x", ipv6LoopbackBuffer[i]);
                        #endif /* DEBUG_UART_FULL */
                        }
                        apiResult = CyBle_L2capChannelDataWrite(cyBle_connHandle.bdHandle,
                                        l2capParameters.lCid, (uint8 *)ipv6LoopbackBuffer, L2CAP_MAX_LEN);
                        if(apiResult != CYBLE_ERROR_OK)
                        {
                            DBG_PRINTF("CyBle_L2capChannelDataWrite API Error: %x \r\n", apiResult);
                        }
                    }
                    break;
                case 'h':                   /* Help menu */
                    DBG_PRINTF("\r\n");
                    DBG_PRINTF("Available commands:\r\n");
                    DBG_PRINTF(" \'h\' - Help menu.\r\n");
                    DBG_PRINTF(" \'z\' + 'Number' - Select peer device.\r\n");
                    DBG_PRINTF(" \'c\' - Send connect request to peer device.\r\n");
                    DBG_PRINTF(" \'d\' - Send disconnect request to peer device.\r\n");
                    DBG_PRINTF(" \'v\' - Cancel connection request.\r\n");
                    DBG_PRINTF(" \'s\' - Start discovery procedure.\r\n");
                    DBG_PRINTF(" \'1\' - Send Data packet to Node though IPSP channel.\r\n");
                    break;
            }
        }
        /* Store bonding data to flash only when all debug information has been sent */
    #if (DEBUG_UART_ENABLED == ENABLED)
        if((cyBle_pendingFlashWrite != 0u) &&
           ((UART_DEB_SpiUartGetTxBufferSize() + UART_DEB_GET_TX_FIFO_SR_VALID) == 0u))
    #else
        if(cyBle_pendingFlashWrite != 0u)
    #endif /* (DEBUG_UART_ENABLED == ENABLED) */
        {
            apiResult = CyBle_StoreBondingData(0u);
            DBG_PRINTF("Store bonding data, status: %x \r\n", apiResult);
        }
        
    }
}  


/* [] END OF FILE */
