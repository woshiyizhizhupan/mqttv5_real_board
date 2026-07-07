/**
 *******************************************************************************
 * @file  hc32_ll.h
 * @brief This file contains HC32 Series Device Driver Library file call
 *        management.
 @verbatim
   Change Logs:
   Date             Author          Notes
   2022-03-31       CDT             First version
   2023-01-15       CDT             Modify version as 3.1.0
   2023-09-30       CDT             Modify version as 3.2.0
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
#ifndef __HC32_LL_H__
#define __HC32_LL_H__

/* C binding of definitions if building with C++ compiler */
#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "hc32_ll_def.h"


#include "hc32f4xx_conf.h"

/**
 * @addtogroup LL_Driver
 * @{
 */

/**
 * @addtogroup LL_Global
 * @{
 */

/*******************************************************************************
 * Global type definitions ('typedef')
 ******************************************************************************/

/*******************************************************************************
 * Global pre-processor symbols/macros ('#define')
 ******************************************************************************/
/**
 * @defgroup LL_Global_Macros LL Global Macros
 * @{
 */

/**
 * @defgroup Peripheral_Register_WP_Global_Macros Peripheral Register Write Protection Global Macros
 * @{
 */
#define LL_PERIPH_EFM           (1UL << 0U)
#define LL_PERIPH_FCG           (1UL << 1U)
#define LL_PERIPH_GPIO          (1UL << 2U)
#define LL_PERIPH_INTC          (1UL << 3U)
#define LL_PERIPH_LVD           (1UL << 4U)
#define LL_PERIPH_MPU           (1UL << 5U)
#define LL_PERIPH_PWC_CLK_RMU   (1UL << 6U)
#define LL_PERIPH_SRAM          (1UL << 7U)
#define LL_PERIPH_ALL           (LL_PERIPH_EFM | LL_PERIPH_FCG | LL_PERIPH_GPIO | LL_PERIPH_INTC  | \
                                 LL_PERIPH_LVD | LL_PERIPH_MPU | LL_PERIPH_SRAM | LL_PERIPH_PWC_CLK_RMU)
/**
 * @}
 */

/* Defined use Device Driver Library */
#if !defined (USE_DDL_DRIVER)
/**
 * @brief Comment the line below if you will not use the Device Driver Library.
 * In this case, the application code will be based on direct access to
 * peripherals registers.
 */
/* #define USE_DDL_DRIVER */
#endif /* USE_DDL_DRIVER */

/**
* @defgroup HC32_Series_DDL_Release_Version HC32 Series DDL Release Version
* @{
*/
#define HC32_DDL_REV_MAIN               0x03U  /*!< [31:24] main version  */
#define HC32_DDL_REV_SUB1               0x02U  /*!< [23:16] sub1 version  */
#define HC32_DDL_REV_SUB2               0x00U  /*!< [15:8]  sub2 version  */
#define HC32_DDL_REV_PATCH              0x00U  /*!< [7:0]   patch version */
#define HC32_DDL_REV                    ((HC32_DDL_REV_MAIN << 24) | (HC32_DDL_REV_SUB1 << 16) | \
                                         (HC32_DDL_REV_SUB2 << 8 ) | (HC32_DDL_REV_PATCH))
/**
 * @}
 */

/**
 * @}
 */


/*******************************************************************************
 * Global variable definitions ('extern')
 ******************************************************************************/

/*******************************************************************************
 * Global function prototypes (definition in C source)
 ******************************************************************************/
/**
 * @addtogroup LL_Global_Functions
 * @{
 */
void LL_PERIPH_WE(uint32_t u32Peripheral);
void LL_PERIPH_WP(uint32_t u32Peripheral);
/**
 * @}
 */

/**
 * @}
 */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* __HC32_DDL_H__ */

/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/
