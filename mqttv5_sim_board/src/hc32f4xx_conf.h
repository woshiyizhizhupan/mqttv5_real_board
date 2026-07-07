/**
 *******************************************************************************
 * @file  usb/usb_dev_hid_custom/source/hc32f4xx_conf.h
 * @brief This file contains HC32 Series Device Driver Library usage management.
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
 @endverbatim
 *******************************************************************************
 * Copyright (C) 2022-2023, Xiaohua Semiconductor Co., Ltd. All rights reserved.
 *
 * This software component is licensed by XHSC under BSD 3-Clause license
 * (the "License"); You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                    opensource.org/licenses/BSD-3-Clause
 *
 *******************************************************************************
 */
#ifndef __HC32F4XX_CONF_H__
#define __HC32F4XX_CONF_H__

/*******************************************************************************
 * Include files
 ******************************************************************************/

/* C binding of definitions if building with C++ compiler */
#ifdef __cplusplus
extern "C" {
#endif

#define LL_ICG_ENABLE
#define LL_UTILITY_ENABLE
// #define LL_PRINT_ENABLE

// #define LL_ADC_ENABLE
// #define LL_AES_ENABLE
#define LL_AOS_ENABLE
// #define LL_CAN_ENABLE
#define LL_CLK_ENABLE
// #define LL_CMP_ENABLE
#define LL_CRC_ENABLE
// #define LL_DBGC_ENABLE
// #define LL_DCU_ENABLE
#define LL_DMA_ENABLE
#define LL_EFM_ENABLE
// #define LL_EMB_ENABLE
// #define LL_EVENT_PORT_ENABLE
#define LL_FCG_ENABLE
// #define LL_FCM_ENABLE
#define LL_GPIO_ENABLE
// #define LL_HASH_ENABLE
#define LL_I2C_ENABLE
// #define LL_I2S_ENABLE
#define LL_INTERRUPTS_ENABLE
// #define LL_INTERRUPTS_SHARE_ENABLE
// #define LL_KEYSCAN_ENABLE
// #define LL_MPU_ENABLE
// #define LL_OTS_ENABLE
#define LL_PWC_ENABLE
// #define LL_QSPI_ENABLE
// #define LL_RMU_ENABLE
// #define LL_RTC_ENABLE
// #define LL_SDIOC_ENABLE
#define LL_SPI_ENABLE
#define LL_SRAM_ENABLE
// #define LL_SWDT_ENABLE
#define LL_TMR0_ENABLE
// #define LL_TMR4_ENABLE
// #define LL_TMR6_ENABLE
// #define LL_TMRA_ENABLE
#define LL_TRNG_ENABLE
#define LL_USART_ENABLE
// #define LL_USB_ENABLE
// #define LL_WDT_ENABLE

#include "hc32_ll.h"
#ifdef LL_ADC_ENABLE
#include "hc32_ll_adc.h"
#endif /* LL_ADC_ENABLE */

#ifdef LL_AES_ENABLE
#include "hc32_ll_aes.h"
#endif /* LL_AES_ENABLE */

#ifdef LL_AOS_ENABLE
#include "hc32_ll_aos.h"
#endif /* LL_AOS_ENABLE */

#ifdef LL_CAN_ENABLE
#include "hc32_ll_can.h"
#endif /* LL_CAN_ENABLE */

#ifdef LL_CLK_ENABLE
#include "hc32_ll_clk.h"
#endif /* LL_CLK_ENABLE */

#ifdef LL_CMP_ENABLE
#include "hc32_ll_cmp.h"
#endif /* LL_CMP_ENABLE */

#ifdef LL_CRC_ENABLE
#include "hc32_ll_crc.h"
#endif /* LL_CRC_ENABLE */

#ifdef LL_DBGC_ENABLE
#include "hc32_ll_dbgc.h"
#endif /* LL_DBGC_ENABLE */

#ifdef LL_DCU_ENABLE
#include "hc32_ll_dcu.h"
#endif /* LL_DCU_ENABLE */

#ifdef LL_DMA_ENABLE
#include "hc32_ll_dma.h"
#endif /* LL_DMA_ENABLE */

#ifdef LL_EFM_ENABLE
#include "hc32_ll_efm.h"
#endif /* LL_EFM_ENABLE */

#ifdef LL_EMB_ENABLE
#include "hc32_ll_emb.h"
#endif /* LL_EMB_ENABLE */

#ifdef LL_EVENT_PORT_ENABLE
#include "hc32_ll_event_port.h"
#endif /* LL_EVENT_PORT_ENABLE */

#ifdef LL_FCG_ENABLE
#include "hc32_ll_fcg.h"
#endif /* LL_FCG_ENABLE */

#ifdef LL_FCM_ENABLE
#include "hc32_ll_fcm.h"
#endif /* LL_FCM_ENABLE */

#ifdef LL_GPIO_ENABLE
#include "hc32_ll_gpio.h"
#endif /* LL_GPIO_ENABLE */

#ifdef LL_HASH_ENABLE
#include "hc32_ll_hash.h"
#endif /* LL_HASH_ENABLE */

#ifdef LL_I2C_ENABLE
#include "hc32_ll_i2c.h"
#endif /* LL_I2C_ENABLE */

#ifdef LL_I2S_ENABLE
#include "hc32_ll_i2s.h"
#endif /* LL_I2S_ENABLE */

#ifdef LL_ICG_ENABLE
#include "hc32_ll_icg.h"
#endif /* LL_ICG_ENABLE */

#ifdef LL_INTERRUPTS_ENABLE
#include "hc32_ll_interrupts.h"
#endif /* LL_INTERRUPTS_ENABLE */

#ifdef LL_INTERRUPTS_SHARE_ENABLE
#include "hc32f460_ll_interrupts_share.h"
#endif /* LL_INTERRUPTS_ENABLE */

#ifdef LL_KEYSCAN_ENABLE
#include "hc32_ll_keyscan.h"
#endif /* LL_KEYSCAN_ENABLE */

#ifdef LL_MPU_ENABLE
#include "hc32_ll_mpu.h"
#endif /* LL_MPU_ENABLE */

#ifdef LL_OTS_ENABLE
#include "hc32_ll_ots.h"
#endif /* LL_OTS_ENABLE */

#ifdef LL_PWC_ENABLE
#include "hc32_ll_pwc.h"
#endif /* LL_PWC_ENABLE */

#ifdef LL_QSPI_ENABLE
#include "hc32_ll_qspi.h"
#endif /* LL_QSPI_ENABLE */

#ifdef LL_RMU_ENABLE
#include "hc32_ll_rmu.h"
#endif /* LL_RMU_ENABLE */

#ifdef LL_RTC_ENABLE
#include "hc32_ll_rtc.h"
#endif /* LL_RTC_ENABLE */

#ifdef LL_SDIOC_ENABLE
#include "hc32_ll_sdioc.h"
#endif /* LL_SDIOC_ENABLE */

#ifdef LL_SPI_ENABLE
#include "hc32_ll_spi.h"
#endif /* LL_SPI_ENABLE */

#ifdef LL_SRAM_ENABLE
#include "hc32_ll_sram.h"
#endif /* LL_SRAM_ENABLE */

#ifdef LL_SWDT_ENABLE
#include "hc32_ll_swdt.h"
#endif /* LL_SWDT_ENABLE */

#ifdef LL_TMR0_ENABLE
#include "hc32_ll_tmr0.h"
#endif /* LL_TMR0_ENABLE */

#ifdef LL_TMR4_ENABLE
#include "hc32_ll_tmr4.h"
#endif /* LL_TMR4_ENABLE */

#ifdef LL_TMR6_ENABLE
#include "hc32_ll_tmr6.h"
#endif /* LL_TMR6_ENABLE */

#ifdef LL_TMRA_ENABLE
#include "hc32_ll_tmra.h"
#endif /* LL_TMRA_ENABLE */

#ifdef LL_TRNG_ENABLE
#include "hc32_ll_trng.h"
#endif /* LL_TRNG_ENABLE */

#ifdef LL_USART_ENABLE
#include "hc32_ll_usart.h"
#endif /* LL_USART_ENABLE */

#ifdef LL_UTILITY_ENABLE
#include "hc32_ll_utility.h"
#endif /* LL_UTILITY_ENABLE */

#ifdef LL_USB_ENABLE
#include "hc32_ll_usb.h"
#endif /* LL_USB_ENABLE */

#ifdef LL_WDT_ENABLE
#include "hc32_ll_wdt.h"
#endif /* LL_WDT_ENABLE */



#ifdef __cplusplus
}
#endif

#endif /* __HC32F4XX_CONF_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
