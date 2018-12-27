/******************************************************************************
 *
 *  Copyright (C) 2018 ST Microelectronics S.A.
 *  Copyright 2018 NXP
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/
#define LOG_TAG "StEse_HalApi"

#include "StEseApi.h"
#include <SpiLayerComm.h>
#include <cutils/properties.h>
#include <ese_config.h>
#include "T1protocol.h"
#include "android_logmsg.h"

/*********************** Global Variables *************************************/

/* ESE Context structure */
ese_Context_t ese_ctxt;

/******************************************************************************
 * Function         StEseLog_InitializeLogLevel
 *
 * Description      This function is called during StEse_init to initialize
 *                  debug log level.
 *
 * Returns          None
 *
 ******************************************************************************/

void StEseLog_InitializeLogLevel() { InitializeSTLogLevel(); }

/******************************************************************************
 * Function         StEse_init
 *
 * Description      This function is called by Jni during the
 *                  initialization of the ESE. It opens the physical connection
 *                  with ESE and creates required client thread for
 *                  operation.
 * Returns          This function return ESESTATUS_SUCCES (0) in case of success
 *                  In case of failure returns other failure value.
 *
 ******************************************************************************/
ESESTATUS StEse_init() {
  SpiDriver_config_t tSpiDriver;
  ESESTATUS wConfigStatus = ESESTATUS_SUCCESS;

  char ese_dev_node[64];
  std::string ese_node;

  STLOG_HAL_D("%s : SteSE_open Enter EseLibStatus = %d ", __func__,
              ese_ctxt.EseLibStatus);
  /*When spi channel is already opened return status as FAILED*/
  if (ese_ctxt.EseLibStatus != ESE_STATUS_CLOSE) {
    STLOG_HAL_D("already opened\n");
    return ESESTATUS_BUSY;
  }

  memset(&ese_ctxt, 0x00, sizeof(ese_ctxt));
  memset(&tSpiDriver, 0x00, sizeof(tSpiDriver));

  ALOGE("MW SEAccessKit Version");

  /* initialize trace level */
  StEseLog_InitializeLogLevel();

  /*Read device node path*/
  ese_node = EseConfig::getString(NAME_ST_ESE_DEV_NODE, "/dev/st54j");
  strcpy(ese_dev_node, ese_node.c_str());
  tSpiDriver.pDevName = ese_dev_node;

  /* Initialize SPI Driver layer */
  T1protocol_init(&tSpiDriver);
  if (wConfigStatus != ESESTATUS_SUCCESS) {
    STLOG_HAL_E("T1protocol_init Failed");
    goto clean_and_return;
  }
  /* Copying device handle to ESE Lib context*/
  ese_ctxt.pDevHandle = tSpiDriver.pDevHandle;

  STLOG_HAL_D("wConfigStatus %x", wConfigStatus);
  ese_ctxt.EseLibStatus = ESE_STATUS_OPEN;
  return wConfigStatus;

clean_and_return:
  if (NULL != ese_ctxt.pDevHandle) {
    SpiLayerInterface_close(ese_ctxt.pDevHandle);
    memset(&ese_ctxt, 0x00, sizeof(ese_ctxt));
  }
  ese_ctxt.EseLibStatus = ESE_STATUS_CLOSE;
  return ESESTATUS_FAILED;
}

/******************************************************************************
 * Function         StEseApi_isOpen
 *
 * \brief           Check if the hal is opened
 *
 * \retval return false if it is close, otherwise true.
 *
 ******************************************************************************/
bool StEseApi_isOpen() {
  STLOG_HAL_D(" %s  status 0x%x \n", __FUNCTION__, ese_ctxt.EseLibStatus);
  return ese_ctxt.EseLibStatus != ESE_STATUS_CLOSE;
}

/******************************************************************************
 * Function         StEse_Transceive
 *
 * Description      This function update the len and provided buffer
 *
 * Returns          On Success ESESTATUS_SUCCESS else proper error code
 *
 ******************************************************************************/
ESESTATUS StEse_Transceive(StEse_data* pCmd, StEse_data* pRsp) {
  ESESTATUS status = ESESTATUS_SUCCESS;
  static int pTxBlock_len = 0;
  uint8_t pCmdlen = pCmd->len;

  STLOG_HAL_D("%s : Enter EseLibStatus = %d ", __func__, ese_ctxt.EseLibStatus);

  if ((NULL == pCmd) || (NULL == pRsp)) return ESESTATUS_INVALID_PARAMETER;

  if ((pCmd->len == 0) || pCmd->p_data == NULL) {
    STLOG_HAL_E(" StEse_Transceive - Invalid Parameter no data\n");
    return ESESTATUS_INVALID_PARAMETER;
  } else if ((ESE_STATUS_CLOSE == ese_ctxt.EseLibStatus)) {
    STLOG_HAL_E(" %s ESE Not Initialized \n", __FUNCTION__);
    return ESESTATUS_NOT_INITIALISED;
  } else if ((ESE_STATUS_BUSY == ese_ctxt.EseLibStatus)) {
    STLOG_HAL_E(" %s ESE - BUSY \n", __FUNCTION__);
    return ESESTATUS_BUSY;
  } else {
    ese_ctxt.EseLibStatus = ESE_STATUS_BUSY;

    pRsp->p_data = (uint8_t*)malloc(254 * sizeof(uint8_t));
    /* Create local copy of cmd_data */
    memcpy(ese_ctxt.p_cmd_data, pCmd->p_data, pCmd->len);
    ese_ctxt.cmd_len = pCmd->len;
    char* CmdPart = (char*)pCmd->p_data;
    char* RspPart = (char*)pRsp->p_data;

    while (pCmd->len > ATP.ifsc) {
      pTxBlock_len = ATP.ifsc;
      int rc = T1protocol_transcieveApduPart(CmdPart, pTxBlock_len, false,
                                             RspPart, &pRsp->len);

      if (rc < 0) {
        status = ESESTATUS_FAILED;
        ese_ctxt.EseLibStatus = ESE_STATUS_IDLE;
        return status;
      }
      pCmdlen -= pTxBlock_len;
      CmdPart = CmdPart + pTxBlock_len;
      if (ESESTATUS_SUCCESS != status) {
        STLOG_HAL_E(" %s T1protocol_transcieveApduPart- Failed \n",
                    __FUNCTION__);
      }
    }

    int rc = T1protocol_transcieveApduPart(CmdPart, pCmdlen, true, RspPart,
                                           &pRsp->len);
    if (rc < 0) status = ESESTATUS_FAILED;

    if (ESESTATUS_SUCCESS != status) {
      STLOG_HAL_E(" %s T1protocol_transcieveApduPart- Failed \n", __FUNCTION__);
    }
    ese_ctxt.EseLibStatus = ESE_STATUS_IDLE;

    STLOG_HAL_E(" %s Exit status 0x%x \n", __FUNCTION__, status);

    return status;
  }
}

/******************************************************************************
 * Function         StEse_close
 *
 * Description      This function close the ESE interface and free all
 *                  resources.
 *
 * Returns          Always return ESESTATUS_SUCCESS (0).
 *
 ******************************************************************************/
ESESTATUS StEse_close(void) {
  ESESTATUS status = ESESTATUS_SUCCESS;

  if ((ESE_STATUS_CLOSE == ese_ctxt.EseLibStatus)) {
    STLOG_HAL_E(" %s ESE Not Initialized \n", __FUNCTION__);
    return ESESTATUS_NOT_INITIALISED;
  }

  if (NULL != ese_ctxt.pDevHandle) {
    SpiLayerInterface_close(ese_ctxt.pDevHandle);
    memset(&ese_ctxt, 0x00, sizeof(ese_ctxt));
    STLOG_HAL_D("StEse_close - ESE Context deinit completed");
    ese_ctxt.EseLibStatus = ESE_STATUS_CLOSE;
  }
  /* Return success always */
  return status;
}