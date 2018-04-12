/*******************************************************************************
* File Name: main.c
*
* Version: 1.0
*
* Description:
*  This project demonstrates Wireless Power Transfer profile in Power 
*  Receiver Unit (PRU) Server role.
*
* Related Document:
*  A4WP Wireless Power Transfer System Baseline System Specification (BSS)
*   V1.2.1
*  BLUETOOTH SPECIFICATION Version 4.1
*
* Hardware Dependency:
*  CY8CKIT-042 BLE
* 
********************************************************************************
* Copyright 2016, Cypress Semiconductor Corporation.  All rights reserved.
* You may use this file only in accordance with the license, terms, conditions,
* disclaimers, and limitations in the end user license agreement accompanying
* the software package with which this file was provided.
*******************************************************************************/

#include "common.h"
#include "wpru.h"
#include "bas.h"

volatile uint32 mainTimer = 0u;


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
* It first starts advertisement once the stack is initialized. 
* Upon advertisement timeout this function enters Hibernate mode.
*
*******************************************************************************/
void AppCallback(uint32 event, void* eventParam)
{
    CYBLE_API_RESULT_T apiResult;
    CYBLE_GAP_BD_ADDR_T localAddr;
    CYBLE_GAP_AUTH_INFO_T *authInfo;
    uint8 i;

    switch (event)
	{
        /**********************************************************
        *                       General Events
        ***********************************************************/
		case CYBLE_EVT_STACK_ON: /* This event received when component is Started */
            /* Enter in to discoverable mode so that remote can search it. */
            apiResult = CyBle_GappStartAdvertisement(CYBLE_ADVERTISING_FAST);
            if(apiResult != CYBLE_ERROR_OK)
            {
                DBG_PRINTF("StartAdvertisement API Error: %d \r\n", apiResult);
            }
            DBG_PRINTF("Bluetooth On, StartAdvertisement with addr: ");
            localAddr.type = 0u;
            CyBle_GetDeviceAddress(&localAddr);
            for(i = CYBLE_GAP_BD_ADDR_SIZE; i > 0u; i--)
            {
                DBG_PRINTF("%2.2x", localAddr.bdAddr[i-1]);
            }
            DBG_PRINTF("\r\n");
            
            break;
		case CYBLE_EVT_TIMEOUT: 
            DBG_PRINTF("CYBLE_EVT_TIMEOUT: %x \r\n", *(CYBLE_TO_REASON_CODE_T *)eventParam);
			break;
		case CYBLE_EVT_HARDWARE_ERROR:    /* This event indicates that some internal HW error has occurred. */
            DBG_PRINTF("Hardware Error \r\n");
			break;
        case CYBLE_EVT_HCI_STATUS:
            DBG_PRINTF("CYBLE_EVT_HCI_STATUS: %x \r\n", *(uint8 *)eventParam);
			break;
        
        /**********************************************************
        *                       GAP Events
        ***********************************************************/
        case CYBLE_EVT_GAP_AUTH_REQ:
            DBG_PRINTF("CYBLE_EVT_AUTH_REQ: security=%x, bonding=%x, ekeySize=%x, err=%x \r\n", 
                (*(CYBLE_GAP_AUTH_INFO_T *)eventParam).security, 
                (*(CYBLE_GAP_AUTH_INFO_T *)eventParam).bonding, 
                (*(CYBLE_GAP_AUTH_INFO_T *)eventParam).ekeySize, 
                (*(CYBLE_GAP_AUTH_INFO_T *)eventParam).authErr);
            break;
        case CYBLE_EVT_GAP_PASSKEY_ENTRY_REQUEST:
            DBG_PRINTF("CYBLE_EVT_PASSKEY_ENTRY_REQUEST \r\n");
            break;
        case CYBLE_EVT_GAP_PASSKEY_DISPLAY_REQUEST:
            DBG_PRINTF("CYBLE_EVT_PASSKEY_DISPLAY_REQUEST %6.6ld \r\n", *(uint32 *)eventParam);
            break;
        case CYBLE_EVT_GAP_AUTH_COMPLETE:
            authInfo = (CYBLE_GAP_AUTH_INFO_T *)eventParam;
            (void)authInfo;
            DBG_PRINTF("AUTH_COMPLETE: security:%x, bonding:%x, ekeySize:%x, authErr %x \r\n", 
                                    authInfo->security, authInfo->bonding, authInfo->ekeySize, authInfo->authErr);
            break;
        case CYBLE_EVT_GAP_AUTH_FAILED:
            DBG_PRINTF("CYBLE_EVT_AUTH_FAILED: %x \r\n", *(uint8 *)eventParam);
            break;
        case CYBLE_EVT_GAPP_ADVERTISEMENT_START_STOP:
            DBG_PRINTF("CYBLE_EVT_GAPP_ADVERTISEMENT_START_STOP, state: %x\r\n", CyBle_GetState());
            if(CYBLE_STATE_DISCONNECTED == CyBle_GetState())
            {   
                if((pruControl.enables & PRU_CONTROL_ENABLES_ENABLE_CHARGE_INDICATOR) == 0u)
                {
                    /* Fast advertising period complete and charging is not enabled,
                     *  go to low power mode (Hibernate mode) and wait for an external
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
                else /* If charging is enabled Start Advertising */
                {
                    apiResult = CyBle_GappStartAdvertisement(CYBLE_ADVERTISING_FAST);
                    if(apiResult != CYBLE_ERROR_OK)
                    {
                        DBG_PRINTF("StartAdvertisement API Error: %d \r\n", apiResult);
                    }
                }
            }
            break;
        case CYBLE_EVT_GAP_DEVICE_CONNECTED:
            printf("CYBLE_EVT_GAP_DEVICE_CONNECTED: ");
            printf(" %x, %x(%d ms), %x, %x \r\n", 
                ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->status,
                ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->connIntv,
                ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->connIntv *5 /4,
                ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->connLatency,
                ((CYBLE_GAP_CONN_PARAM_UPDATED_IN_CONTROLLER_T *)eventParam)->supervisionTO
                );
            UpdateLedState();
            break;
        case CYBLE_EVT_GAP_DEVICE_DISCONNECTED:
            DBG_PRINTF("CYBLE_EVT_GAP_DEVICE_DISCONNECTED\r\n");
            /* Put the device to discoverable mode so that remote can search it. */
            apiResult = CyBle_GappStartAdvertisement(CYBLE_ADVERTISING_FAST);
            if(apiResult != CYBLE_ERROR_OK)
            {
                DBG_PRINTF("StartAdvertisement API Error: %x \r\n", apiResult);
            }
            break;
        case CYBLE_EVT_GAP_ENCRYPT_CHANGE:
            DBG_PRINTF("ENCRYPT_CHANGE: %x \r\n", *(uint8 *)eventParam);
            break;
        case CYBLE_EVT_GAPC_CONNECTION_UPDATE_COMPLETE:
            DBG_PRINTF("CYBLE_EVT_CONNECTION_UPDATE_COMPLETE: %x \r\n", *(uint8 *)eventParam);
            break;
        case CYBLE_EVT_GAP_KEYINFO_EXCHNGE_CMPLT:
            DBG_PRINTF("CYBLE_EVT_GAP_KEYINFO_EXCHNGE_CMPLT \r\n");
            break;
            
        /**********************************************************
        *                       GATT Events
        ***********************************************************/
        case CYBLE_EVT_GATT_CONNECT_IND:
            DBG_PRINTF("CYBLE_EVT_GATT_CONNECT_IND: %x, %x \r\n", cyBle_connHandle.attId, cyBle_connHandle.bdHandle);
            break;
        case CYBLE_EVT_GATT_DISCONNECT_IND:
            DBG_PRINTF("CYBLE_EVT_GATT_DISCONNECT_IND \r\n");
            break;
        case CYBLE_EVT_GATTS_WRITE_REQ:
            DBG_PRINTF("CYBLE_EVT_GATT_WRITE_REQ: %x = ",((CYBLE_GATTS_WRITE_REQ_PARAM_T *)eventParam)->handleValPair.attrHandle);
            ShowValue(&((CYBLE_GATTS_WRITE_REQ_PARAM_T *)eventParam)->handleValPair.value);
            (void)CyBle_GattsWriteRsp(((CYBLE_GATTS_WRITE_REQ_PARAM_T *)eventParam)->connHandle);
            break;
        case CYBLE_EVT_GATTS_INDICATION_ENABLED:
            DBG_PRINTF("CYBLE_EVT_GATTS_INDICATION_ENABLED \r\n");
            break;
        case CYBLE_EVT_GATTS_INDICATION_DISABLED:
            DBG_PRINTF("CYBLE_EVT_GATTS_INDICATION_DISABLED \r\n");
            break;

        case CYBLE_EVT_GATTS_READ_CHAR_VAL_ACCESS_REQ:
            /* Triggered on server side when client sends read request and when
            * characteristic has CYBLE_GATT_DB_ATTR_CHAR_VAL_RD_EVENT property set.
            * This event could be ignored by application unless it need to response
            * by error response which needs to be set in gattErrorCode field of
            * event parameter. */
            DBG_PRINTF("CYBLE_EVT_GATTS_READ_CHAR_VAL_ACCESS_REQ: handle: %x \r\n", 
                ((CYBLE_GATTS_CHAR_VAL_READ_REQ_T *)eventParam)->attrHandle);
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
*
*******************************************************************************/
CY_ISR(Timer_Interrupt)
{
    if(CySysWdtGetInterruptSource() & WDT_INTERRUPT_SOURCE)
    {
        static uint8 led = LED_OFF;
        
        /* Blink LED to indicate that device advertises */
        if(CyBle_GetState() == CYBLE_STATE_ADVERTISING)
        {
            led ^= LED_OFF;
            if(PRU_CHARGING)
            {
                if((pruDynamicParameter.alert & PRU_ALERT_OVER_MASK) != 0u)
                {
                    Advertising_LED_Write(led);
                    Disconnect_LED_Write(led);
                }
                Charging_LED_Write(led);
            }
            else
            {
                Advertising_LED_Write(led);
            }
        }
        
        /* Indicate that timer is raised to the main loop */
        mainTimer++;
        
        /* Clears interrupt request  */
        CySysWdtClearInterrupt(WDT_INTERRUPT_SOURCE);
    }
}

/*******************************************************************************
* Function Name: UpdateLedState
********************************************************************************
*
* Summary:
*  This function sets a led color depending on state and alert status.
*
*******************************************************************************/
void UpdateLedState(void)
{
   
    if(CyBle_GetState() == CYBLE_STATE_DISCONNECTED)
    {   
        Advertising_LED_Write(LED_OFF);
        Disconnect_LED_Write(LED_ON);
        Charging_LED_Write(LED_OFF);
    }
    else if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
    {
        /* Set all LEDs on to indicate over limited condition */
        if(PRU_CHARGING && ((pruDynamicParameter.alert & PRU_ALERT_OVER_MASK) != 0u))
        {
            Advertising_LED_Write(LED_ON);
            Disconnect_LED_Write(LED_ON);
            Charging_LED_Write(LED_ON);
        }
        else
        {
            Advertising_LED_Write(LED_OFF);
            Disconnect_LED_Write(LED_OFF);
            if(PRU_CHARGING)
            {
                Charging_LED_Write(LED_ON);
            }
            else
            {
                Charging_LED_Write(LED_OFF);
            }
        }
    }
    else
    {
        /* Advertising led is handled in Timer_Interrupt */
    }
}

/*******************************************************************************
* Function Name: WDT_Start
********************************************************************************
*
* Summary:
*  Configures WDT to trigger an interrupt every second.
*
*******************************************************************************/

void WDT_Start(void)
{
    /* Unlock the WDT registers for modification */
    CySysWdtUnlock(); 
    /* Setup ISR */
    WDT_Interrupt_StartEx(&Timer_Interrupt);
    /* Write the mode to generate interrupt on match */
    CySysWdtWriteMode(WDT_COUNTER, CY_SYS_WDT_MODE_INT);
    /* Configure the WDT counter clear on a match setting */
    CySysWdtWriteClearOnMatch(WDT_COUNTER, WDT_COUNTER_ENABLE);
    /* Configure the WDT counter match comparison value */
    CySysWdtWriteMatch(WDT_COUNTER, WDT_1SEC);
    /* Reset WDT counter */
    CySysWdtResetCounters(WDT_COUNTER);
    /* Enable the specified WDT counter */
    CySysWdtEnable(WDT_COUNTER_MASK);
    /* Lock out configuration changes to the Watchdog timer registers */
    CySysWdtLock();    
}


/*******************************************************************************
* Function Name: WDT_Stop
********************************************************************************
*
* Summary:
*  This API stops the WDT timer.
*
*******************************************************************************/
void WDT_Stop(void)
{
    /* Unlock the WDT registers for modification */
    CySysWdtUnlock(); 
    /* Disable the specified WDT counter */
    CySysWdtDisable(WDT_COUNTER_MASK);
    /* Locks out configuration changes to the Watchdog timer registers */
    CySysWdtLock();    
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
* BLE is idle and the UART transmission/reception is not happening. At all other
* times, the function tries to enter CPU sleep.
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
    if((CyBle_GetState() == CYBLE_STATE_ADVERTISING) || 
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
            #if (DEBUG_UART_ENABLED == ENABLED)
                /* Put the CPU into the Deep-Sleep mode when all debug information has been sent */
                if((UART_DEB_SpiUartGetTxBufferSize() + UART_DEB_GET_TX_FIFO_SR_VALID) == 0u)
                {
                    CySysPmDeepSleep();
                }
                else /* Put the CPU into Sleep mode and let SCB to continue sending debug data */
                {
                    CySysPmSleep();
                }
            #else
                CySysPmDeepSleep();
            #endif /* (DEBUG_UART_ENABLED == ENABLED) */
            }
        }
        else /* When BLE subsystem has been put into Sleep mode or is active */
        {
            /* And hardware doesn't finish Tx/Rx opeation - put the CPU into Sleep mode */
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
*  This function process all BLE events and also implements the low power 
*  functionality.
*
*******************************************************************************/
int main()
{
  
    /* Enable global interrupts */
    CyGlobalIntEnable; 
    
#if (DEBUG_UART_ENABLED == ENABLED)
	UART_DEB_Start();
#endif /* (DEBUG_UART_ENABLED == ENABLED) */ 
    DBG_PRINTF("BLE Wireless Power Receiver Example Project \r\n");
    Disconnect_LED_Write(LED_OFF);
    Advertising_LED_Write(LED_OFF);
    Charging_LED_Write(LED_OFF);

    /* Start CYBLE component and register generic event handler */
    CyBle_Start(AppCallback);
    
    /* Register service specific callback functions */
    WptsInit();
    BasInit();
    
    /* Start ADC and OpAmps */
    ADC_Start();
    Opamp_1_Start();
    Opamp_2_Start();

    /* Start general timer */
    WDT_Start();

    /***************************************************************************
    * Main polling loop
    ***************************************************************************/
	while(1) 
    {   
        /* Process all the generated events. */
        CyBle_ProcessEvents();

        /* To achieve low power in the device */
        LowPowerImplementation();

        
        /***********************************************************************
        * Wait for connection established with Central device
        ***********************************************************************/
        if(CyBle_GetState() == CYBLE_STATE_CONNECTED)
        {
            /*******************************************************************
            *  Periodically simulate call of the Wireless Charging simulation API
            *******************************************************************/        
            if(mainTimer != 0u)
            {
                mainTimer = 0u;
                
            #if (BAS_SIMULATE_ENABLE != 0)
                SimulateBattery();
                CyBle_ProcessEvents();
            #endif /* BAS_SIMULATE_ENABLE != 0 */    
            #if (BAS_MEASURE_ENABLE != 0)
                MeasureBattery();
                CyBle_ProcessEvents();
            #endif /* BAS_MEASURE_ENABLE != 0 */
                
                /* Measure parameters for wireless system */
                MeasureWirelessParam();
                
                /* Simulate charging complete */
                if(PRU_CHARGING)
                {
                    SimulateWirelessTransfer();
                }
            }
        }
	}   
}  


/* [] END OF FILE */

