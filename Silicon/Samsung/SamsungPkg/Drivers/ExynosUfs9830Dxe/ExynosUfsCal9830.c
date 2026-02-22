/** @file
  Exynos 9830 UFS CAL - PHY calibration sequences.
  Adapted directly from Samsung lk3rd platform/exynos9830/ufs-cal-9830.c.
  Original Copyright: Samsung Electronics Co. LTD

  SPDX-License-Identifier: Proprietary (Samsung) / ported for EDK2
**/

/* Define to select the EDK2 path in CAL macros */
#define __UFS_CAL_LK__

#include "ExynosUfs.h"

/* ─── CAL-internal defines ──────────────────────────────────────────────────*/
#define _UFS_CAL_

#define IS_PWR_MODE_HS(m)   (((m) == FAST_MODE) || ((m) == FASTAUTO_MODE))
#define IS_PWR_MODE_PWM(m)  (((m) == SLOW_MODE) || ((m) == SLOWAUTO_MODE))

enum { PA_HS_MODE_A = 1, PA_HS_MODE_B = 2 };
enum { FAST_MODE = 1, SLOW_MODE = 2, FASTAUTO_MODE = 4, SLOWAUTO_MODE = 5, UNCHANGED = 7 };

/* Integer-only timing macros (no float, safe for EDK2 MSVC/GCC -msoft-float) */
#define UNIPRO_MCLK_PERIOD(p)            (1000000000UL / (p)->mclk_rate)
/* Round-off: (1e9 / rate + 0.5) => (1e9*2 / rate + 1) / 2  */
#define UNIPRO_MCLK_PERIOD_ROUND_OFF(p)  (u32)(((2000000000ULL / (p)->mclk_rate) + 1) / 2)
#define UNIPRO18_MCLK_PERIOD(p)          (16ULL * 1000ULL * 1000000ULL / (p)->mclk_rate)

#define TX_LINE_RESET_TIME              3200UL
#define RX_LINE_RESET_DETECT_TIME       1000UL
#define PCS_TX_LINE_RESET_PERIOD(p) \
  (u32)((TX_LINE_RESET_TIME * ((u64)(p)->mclk_rate / 1000000ULL)))
#define PCS_RX_LINE_RESET_DETECT_PERIOD(p) \
  (u32)((RX_LINE_RESET_DETECT_TIME * ((u64)(p)->mclk_rate / 1000000ULL)))

#define PHY_PMA_COMN_ADDR(reg)          (reg)
#define PHY_PMA_TRSV_ADDR(reg,lane)     ((reg) + (0x800 * (lane)))

#define NUM_OF_UFS_HOST  2

enum {
  PHY_CFG_NONE = 0,
  PHY_PCS_COMN, PHY_PCS_RXTX, PHY_PMA_COMN, PHY_PMA_TRSV,
  PHY_PLL_WAIT, PHY_CDR_WAIT, PHY_CDR_AFC_WAIT,
  UNIPRO_STD_MIB, UNIPRO_DBG_MIB, UNIPRO_DBG_APB,
  PHY_PCS_RX, PHY_PCS_TX, PHY_PCS_RX_PRD, PHY_PCS_TX_PRD,
  UNIPRO_DBG_PRD,
  PHY_PMA_TRSV_LANE1_SQ_OFF,
  PHY_PMA_TRSV_SQ,
  COMMON_WAIT,
  PHY_PCS_RX_LR_PRD, PHY_PCS_TX_LR_PRD,
  PHY_PCS_RX_PRD_ROUND_OFF, PHY_PCS_TX_PRD_ROUND_OFF,
  UNIPRO_ADAPT_LENGTH,
  PHY_EMB_CDR_WAIT, PHY_EMB_CAL_WAIT,
};

enum {
  __PMD_PWM_G1_L1, __PMD_PWM_G1_L2, __PMD_PWM_G2_L1, __PMD_PWM_G2_L2,
  __PMD_PWM_G3_L1, __PMD_PWM_G3_L2, __PMD_PWM_G4_L1, __PMD_PWM_G4_L2,
  __PMD_PWM_G5_L1, __PMD_PWM_G5_L2, __PMD_PWM_MAX,
  __PMD_HS_G1_L1, __PMD_HS_G1_L2, __PMD_HS_G2_L1, __PMD_HS_G2_L2,
  __PMD_HS_G3_L1, __PMD_HS_G3_L2, __PMD_HS_G4_L1, __PMD_HS_G4_L2,
  __PMD_HS_MAX,
};

#define PMD_PWM_G1_L1  (1U << __PMD_PWM_G1_L1)
#define PMD_PWM_G1_L2  (1U << __PMD_PWM_G1_L2)
#define PMD_PWM_G2_L1  (1U << __PMD_PWM_G2_L1)
#define PMD_PWM_G2_L2  (1U << __PMD_PWM_G2_L2)
#define PMD_PWM_G3_L1  (1U << __PMD_PWM_G3_L1)
#define PMD_PWM_G3_L2  (1U << __PMD_PWM_G3_L2)
#define PMD_PWM_G4_L1  (1U << __PMD_PWM_G4_L1)
#define PMD_PWM_G4_L2  (1U << __PMD_PWM_G4_L2)
#define PMD_PWM_G5_L1  (1U << __PMD_PWM_G5_L1)
#define PMD_PWM_G5_L2  (1U << __PMD_PWM_G5_L2)
#define PMD_PWM_MAX    (1U << __PMD_PWM_MAX)
#define PMD_HS_G1_L1   (1U << __PMD_HS_G1_L1)
#define PMD_HS_G1_L2   (1U << __PMD_HS_G1_L2)
#define PMD_HS_G2_L1   (1U << __PMD_HS_G2_L1)
#define PMD_HS_G2_L2   (1U << __PMD_HS_G2_L2)
#define PMD_HS_G3_L1   (1U << __PMD_HS_G3_L1)
#define PMD_HS_G3_L2   (1U << __PMD_HS_G3_L2)
#define PMD_HS_G4_L1   (1U << __PMD_HS_G4_L1)
#define PMD_HS_G4_L2   (1U << __PMD_HS_G4_L2)
#define PMD_HS_MAX     (1U << __PMD_HS_MAX)
#define PMD_ALL        (PMD_HS_MAX - 1)
#define PMD_PWM        (PMD_PWM_MAX - 1)
#define PMD_HS         (PMD_ALL ^ PMD_PWM)

struct ufs_cal_phy_cfg {
  u32 addr;
  u32 val;
  u32 flg;
  u32 lyr;
  u8  board;
};

#define for_each_phy_cfg(cfg)  for (; (cfg)->flg != PHY_CFG_NONE; (cfg)++)

/* ─── CAL state ─────────────────────────────────────────────────────────────*/
static struct ufs_cal_param  *ufs_cal[NUM_OF_UFS_HOST];
static unsigned long          ufs_cal_lock_timeout = 0xFFFFFFFF;

/* ─── PHY init tables (evt0 / evt1) ─────────────────────────────────────────*/
static const struct ufs_cal_phy_cfg init_cfg_evt0[] = {
  {0x44,   0x00,        PMD_ALL, UNIPRO_DBG_PRD,        BRD_ALL},
  {0x200,  0x40,        PMD_ALL, PHY_PCS_COMN,           BRD_ALL},
  {0x12,   0x00,        PMD_ALL, PHY_PCS_RX_PRD_ROUND_OFF, BRD_ALL},
  {0xAA,   0x00,        PMD_ALL, PHY_PCS_TX_PRD_ROUND_OFF, BRD_ALL},
  {0xA9,   0x02,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0xAB,   0x00,        PMD_ALL, PHY_PCS_TX_LR_PRD,      BRD_ALL},
  {0x11,   0x00,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x1B,   0x00,        PMD_ALL, PHY_PCS_RX_LR_PRD,      BRD_ALL},
  {0x2F,   0x79,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x76,   0x03,        PMD_ALL, PHY_PCS_RX,             BRD_ZEBU},
  {0x84,   0x01,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x04,   0x01,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0x25,   0xF6,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x7F,   0x00,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0x200,  0x00,        PMD_ALL, PHY_PCS_COMN,           BRD_ALL},
  {0x155E, 0x00,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x3000, 0x00,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x3001, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x4021, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x4020, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0xA006, 0x80000000,  PMD_ALL, UNIPRO_DBG_MIB,         BRD_ALL},
  {0x00,   0x3E8,       PMD_ALL, COMMON_WAIT,             BRD_ALL},
  {0x10C,  0x10,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x118,  0x48,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x81C,  0x0C,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xB84,  0x40,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x974,  0x00,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x978,  0x3F,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x97C,  0xFF,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x990,  0x4E,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x9B8,  0x5E,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x9BC,  0x70,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xBB4,  0x25,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAB0,  0x13,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xACC,  0x05,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAD8,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xADC,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE0,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE4,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE8,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAEC,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF0,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF4,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF8,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xBCC,  0x80,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x28,   0x33,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x34,   0xB9,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x38,   0x0F,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x44,   0x01,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xB0,   0x30,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x104,  0x20,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xB90,  0x18,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x10C,  0x18,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x10C,  0x00,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xCE0,  0x08,        PMD_ALL, PHY_EMB_CAL_WAIT,       BRD_ALL},
  {0xA006, 0x00,        PMD_ALL, UNIPRO_DBG_MIB,         BRD_ALL},
  {0, 0, 0, 0, 0},
};

static const struct ufs_cal_phy_cfg init_cfg_evt1[] = {
  {0x44,   0x00,        PMD_ALL, UNIPRO_DBG_PRD,        BRD_ALL},
  {0x200,  0x40,        PMD_ALL, PHY_PCS_COMN,           BRD_ALL},
  {0x12,   0x00,        PMD_ALL, PHY_PCS_RX_PRD_ROUND_OFF, BRD_ALL},
  {0xAA,   0x00,        PMD_ALL, PHY_PCS_TX_PRD_ROUND_OFF, BRD_ALL},
  {0xA9,   0x02,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0xAB,   0x00,        PMD_ALL, PHY_PCS_TX_LR_PRD,      BRD_ALL},
  {0x11,   0x00,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x1B,   0x00,        PMD_ALL, PHY_PCS_RX_LR_PRD,      BRD_ALL},
  {0x2F,   0x79,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x76,   0x03,        PMD_ALL, PHY_PCS_RX,             BRD_ZEBU},
  {0x84,   0x01,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x04,   0x01,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0x25,   0xF6,        PMD_ALL, PHY_PCS_RX,             BRD_ALL},
  {0x7F,   0x00,        PMD_ALL, PHY_PCS_TX,             BRD_ALL},
  {0x200,  0x00,        PMD_ALL, PHY_PCS_COMN,           BRD_ALL},
  {0x155E, 0x00,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x3000, 0x00,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x3001, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x4021, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x4020, 0x01,        PMD_ALL, UNIPRO_STD_MIB,         BRD_ALL},
  {0x10C,  0x10,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x118,  0x48,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x81C,  0x0C,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xB84,  0x40,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x974,  0x00,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x978,  0x3F,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x97C,  0xFF,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x990,  0x4E,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x9B8,  0x5E,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x9BC,  0x70,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xBB4,  0x25,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAB0,  0x13,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xACC,  0x05,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAD8,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xADC,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE0,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE4,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAE8,  0x10,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAEC,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF0,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF4,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xAF8,  0x08,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0xBCC,  0x80,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x28,   0x33,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x34,   0xB9,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x38,   0x0F,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x44,   0x01,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xB0,   0x30,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x104,  0x20,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xB90,  0x18,        PMD_ALL, PHY_PMA_TRSV,           BRD_ALL},
  {0x10C,  0x18,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0x10C,  0x00,        PMD_ALL, PHY_PMA_COMN,           BRD_ALL},
  {0xCE0,  0x08,        PMD_ALL, PHY_EMB_CAL_WAIT,       BRD_ALL},
  {0xA006, 0x00,        PMD_ALL, UNIPRO_DBG_MIB,         BRD_ALL},
  {0, 0, 0, 0, 0},
};

/* Empty card table - not used on exynos9830 embedded */
static const struct ufs_cal_phy_cfg init_cfg_card[] = {
  {0, 0, 0, 0, 0},
};

/* ─── Post-link G3 tables ────────────────────────────────────────────────────*/
static const struct ufs_cal_phy_cfg post_init_cfg_evt0_g3[] = {
  {0x00, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0x0B, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg post_init_cfg_evt1_g3[] = {
  {0x00, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0x0B, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0, 0, 0, 0, 0},
};

/* ─── Post-link G4 tables ────────────────────────────────────────────────────*/
static const struct ufs_cal_phy_cfg post_init_cfg_evt0_g4[] = {
  {0x00, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0x0B, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg post_init_cfg_evt1_g4[] = {
  {0x00, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0x0B, 0x00, PMD_ALL, UNIPRO_ADAPT_LENGTH, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg post_init_cfg_card[] = {
  {0, 0, 0, 0, 0},
};

/* ─── PMC calibration tables ─────────────────────────────────────────────────*/
static const struct ufs_cal_phy_cfg calib_of_pwm[] = {
  {0x00, 0x9E, PMD_PWM, UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg calib_of_pwm_card[]  = { {0,0,0,0,0} };
static const struct ufs_cal_phy_cfg post_calib_of_pwm[] = {
  {0x00, 0x9E, PMD_PWM, UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg post_calib_of_pwm_card[] = { {0,0,0,0,0} };

static const struct ufs_cal_phy_cfg calib_of_hs_rate_a[] = {
  {0x9D, 0x00, PMD_HS,  PHY_PCS_RX,     BRD_ALL},
  {0x9E, 0x00, PMD_HS,  PHY_PCS_RX,     BRD_ALL},
  {0x0B, 0x00, PMD_HS,  UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg calib_of_hs_rate_a_card[] = { {0,0,0,0,0} };
static const struct ufs_cal_phy_cfg post_calib_of_hs_rate_a[] = {
  {0x00, 0x76, PMD_HS,  UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg post_calib_of_hs_rate_a_card[] = { {0,0,0,0,0} };

static const struct ufs_cal_phy_cfg calib_of_hs_rate_b[] = {
  {0x9D, 0x00, PMD_HS,  PHY_PCS_RX,     BRD_ALL},
  {0x9E, 0x00, PMD_HS,  PHY_PCS_RX,     BRD_ALL},
  {0x0B, 0x00, PMD_HS,  UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg calib_of_hs_rate_b_card[] = { {0,0,0,0,0} };
static const struct ufs_cal_phy_cfg post_calib_of_hs_rate_b[] = {
  {0x00, 0x76, PMD_HS,  UNIPRO_STD_MIB, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg post_calib_of_hs_rate_b_card[] = { {0,0,0,0,0} };

/* ─── H8 tables ──────────────────────────────────────────────────────────────*/
static const struct ufs_cal_phy_cfg post_h8_enter[] = {
  {0xCE0, 0x00, PMD_HS, PHY_PMA_TRSV, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg post_h8_enter_card[] = { {0,0,0,0,0} };
static const struct ufs_cal_phy_cfg pre_h8_exit[] = {
  {0xCE0, 0x08, PMD_HS, PHY_EMB_CDR_WAIT, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg pre_h8_exit_card[] = { {0,0,0,0,0} };

/* ─── Lane-1 SQ off tables ───────────────────────────────────────────────────*/
static const struct ufs_cal_phy_cfg lane1_sq_off[] = {
  {0x750, 0x02, PMD_ALL, PHY_PMA_TRSV_LANE1_SQ_OFF, BRD_ALL},
  {0, 0, 0, 0, 0},
};
static const struct ufs_cal_phy_cfg lane1_sq_off_card[] = { {0,0,0,0,0} };

/* ─── CAL wait helpers ───────────────────────────────────────────────────────*/
static ufs_cal_errno ufs_cal_wait_pll_lock (void *hba, u32 addr, u32 mask)
{
  u32 i;
  for (i = 0; i < 100; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_COMN_ADDR (addr)) & mask) == mask)
      return UFS_CAL_NO_ERROR;
    ufs_lld_udelay (1);
  }
  DEBUG ((DEBUG_ERROR, "UFS CAL: PLL lock timeout\n"));
  return UFS_CAL_ERROR;
}

static ufs_cal_errno ufs_cal_wait_cdr_lock (void *hba, u32 addr, u32 mask, int lane)
{
  u32 i;
  for (i = 0; i < 1000; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (addr, lane)) & mask) == mask)
      return UFS_CAL_NO_ERROR;
    ufs_lld_udelay (1);
  }
  DEBUG ((DEBUG_ERROR, "UFS CAL: CDR lock timeout lane %d\n", lane));
  return UFS_CAL_ERROR;
}

static ufs_cal_errno ufs_cal_wait_cdr_afc_check (void *hba, u32 addr, u32 mask, int lane)
{
  u32 i;
  for (i = 0; i < 1000; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (addr, lane)) & mask) == mask)
      return UFS_CAL_NO_ERROR;
    ufs_lld_udelay (1);
  }
  return UFS_CAL_ERROR;
}

static ufs_cal_errno ufs30_cal_wait_cdr_lock (void *hba, u32 addr, u32 mask, int lane)
{
  return ufs_cal_wait_cdr_lock (hba, addr, mask, lane);
}

static ufs_cal_errno ufs30_cal_done_wait (void *hba, u32 addr, u32 mask, int lane)
{
  u32 i;
  for (i = 0; i < 500; i++) {
    if ((ufs_lld_pma_read (hba, PHY_PMA_TRSV_ADDR (addr, lane)) & mask) == 0)
      return UFS_CAL_NO_ERROR;
    ufs_lld_udelay (1);
  }
  DEBUG ((DEBUG_ERROR, "UFS CAL: EMB CAL timeout lane %d\n", lane));
  return UFS_CAL_ERROR;
}

/* ─── Core config apply loop (mirrors LK ufs_cal_config_uic) ────────────────*/
static ufs_cal_errno ufs_cal_config_uic (
  struct ufs_cal_param            *p,
  const struct ufs_cal_phy_cfg    *cfg,
  struct uic_pwr_mode             *pmd)
{
  void *hba = p->host;
  u8    lane;
  u8    num_lanes = p->available_lane;
  u32   pmd_mask  = PMD_ALL;

  if (pmd) {
    if (IS_PWR_MODE_HS (pmd->mode)) {
      if (pmd->hs_series == PA_HS_MODE_A) {
        pmd_mask = (1U << (__PMD_HS_G1_L1 + ((pmd->gear - 1) * 2) + (pmd->lane - 1)));
      } else {
        pmd_mask = (1U << (__PMD_HS_G1_L1 + ((pmd->gear - 1) * 2) + (pmd->lane - 1)));
      }
      pmd_mask = PMD_HS;  /* simplified: apply all HS entries */
    } else {
      pmd_mask = PMD_PWM;
    }
  }

  for_each_phy_cfg (cfg) {
    if (!(cfg->board & p->board))
      continue;

    for (lane = 0; lane < num_lanes; lane++) {
      if (!(cfg->flg & pmd_mask) && pmd)
        continue;

      switch (cfg->lyr) {
      case UNIPRO_STD_MIB:
      case UNIPRO_DBG_MIB:
        if (lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->addr), cfg->val);
        break;
      case UNIPRO_DBG_PRD:
        if (lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->addr), UNIPRO_MCLK_PERIOD (p));
        break;
      case UNIPRO_ADAPT_LENGTH:
        if (lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->addr), cfg->val);
        break;
      case PHY_PCS_COMN:
        if (lane == 0)
          ufs_lld_dme_set (hba, UIC_ARG_MIB (cfg->addr), cfg->val);
        break;
      case PHY_PCS_RXTX:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr, TX_LANE_0 + lane), cfg->val);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr, RX_LANE_0 + lane), cfg->val);
        break;
      case PHY_PCS_RX:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr, RX_LANE_0 + lane), cfg->val);
        break;
      case PHY_PCS_TX:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr, TX_LANE_0 + lane), cfg->val);
        break;
      case PHY_PCS_RX_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr, RX_LANE_0 + lane),
                         UNIPRO_MCLK_PERIOD (p));
        break;
      case PHY_PCS_TX_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr, TX_LANE_0 + lane),
                         UNIPRO_MCLK_PERIOD (p));
        break;
      case PHY_PCS_RX_PRD_ROUND_OFF:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr, RX_LANE_0 + lane),
                         UNIPRO_MCLK_PERIOD_ROUND_OFF (p));
        break;
      case PHY_PCS_TX_PRD_ROUND_OFF:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr, TX_LANE_0 + lane),
                         UNIPRO_MCLK_PERIOD_ROUND_OFF (p));
        break;
      case PHY_PCS_RX_LR_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr,   RX_LANE_0+lane), (PCS_RX_LINE_RESET_DETECT_PERIOD(p)>>16)&0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr+1, RX_LANE_0+lane), (PCS_RX_LINE_RESET_DETECT_PERIOD(p)>>8)&0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr+2, RX_LANE_0+lane), (PCS_RX_LINE_RESET_DETECT_PERIOD(p)>>0)&0xFF);
        break;
      case PHY_PCS_TX_LR_PRD:
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr,   TX_LANE_0+lane), (PCS_TX_LINE_RESET_PERIOD(p)>>16)&0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr+1, TX_LANE_0+lane), (PCS_TX_LINE_RESET_PERIOD(p)>>8)&0xFF);
        ufs_lld_dme_set (hba, UIC_ARG_MIB_SEL (cfg->addr+2, TX_LANE_0+lane), (PCS_TX_LINE_RESET_PERIOD(p)>>0)&0xFF);
        break;
      case PHY_PMA_COMN:
        if (lane == 0)
          ufs_lld_pma_write (hba, cfg->val, PHY_PMA_COMN_ADDR (cfg->addr));
        break;
      case PHY_PMA_TRSV:
        ufs_lld_pma_write (hba, cfg->val, PHY_PMA_TRSV_ADDR (cfg->addr, lane));
        break;
      case PHY_PMA_TRSV_LANE1_SQ_OFF:
        if (lane == 1) {
          if (p->connected_rx_lane < p->available_lane)
            ufs_lld_pma_write (hba, cfg->val, PHY_PMA_TRSV_ADDR (cfg->addr, lane));
        }
        break;
      case PHY_PMA_TRSV_SQ:
        if (lane < p->connected_rx_lane)
          ufs_lld_pma_write (hba, cfg->val, PHY_PMA_TRSV_ADDR (cfg->addr, lane));
        break;
      case UNIPRO_DBG_APB:
        if (lane == 0)
          ufs_lld_unipro_write (hba, cfg->val, cfg->addr);
        break;
      case PHY_PLL_WAIT:
        if (lane == 0) {
          if (ufs_cal_wait_pll_lock (hba, cfg->addr, cfg->val) == UFS_CAL_ERROR)
            return UFS_CAL_TIMEOUT;
        }
        break;
      case PHY_CDR_WAIT:
        if (lane < p->active_rx_lane) {
          if (ufs_cal_wait_cdr_lock (hba, cfg->addr, cfg->val, lane) == UFS_CAL_ERROR)
            return UFS_CAL_TIMEOUT;
        }
        break;
      case PHY_EMB_CDR_WAIT:
        if (lane < p->active_rx_lane) {
          if (ufs30_cal_wait_cdr_lock (hba, cfg->addr, cfg->val, lane) == UFS_CAL_ERROR)
            return UFS_CAL_TIMEOUT;
        }
        break;
      case PHY_CDR_AFC_WAIT:
        if (lane < p->active_rx_lane) {
          if (p->tbl == HOST_CARD) {
            if (ufs_cal_wait_cdr_afc_check (hba, cfg->addr, cfg->val, lane) == UFS_CAL_ERROR)
              return UFS_CAL_TIMEOUT;
          }
        }
        break;
      case COMMON_WAIT:
        if (lane == 0)
          ufs_lld_udelay (cfg->val);
        break;
      case PHY_EMB_CAL_WAIT:
        if (ufs30_cal_done_wait (hba, cfg->addr, cfg->val, lane) == UFS_CAL_ERROR)
          return UFS_CAL_TIMEOUT;
        break;
      default:
        break;
      }
    }
  }
  return UFS_CAL_NO_ERROR;
}

/* ─── Hibern8 calibration (from lk3rd) ──────────────────────────────────────*/
static void ufs_cal_calib_hibern8_values (void *hba)
{
  u32 hw_cap_min_tactivate;
  u32 peer_rx_min_actv_time_cap;
  u32 max_rx_hibern8_time_cap;

  ufs_lld_dme_get (hba, UIC_ARG_MIB_SEL (0x8F, RX_LANE_0), &hw_cap_min_tactivate);
  ufs_lld_dme_get (hba, UIC_ARG_MIB (0x15A8), &peer_rx_min_actv_time_cap);
  ufs_lld_dme_get (hba, UIC_ARG_MIB (0x15A7), &max_rx_hibern8_time_cap);

  if (peer_rx_min_actv_time_cap >= hw_cap_min_tactivate)
    ufs_lld_dme_peer_set (hba, UIC_ARG_MIB (0x15A8), peer_rx_min_actv_time_cap + 1);
  ufs_lld_dme_set (hba, UIC_ARG_MIB (0x15A7), max_rx_hibern8_time_cap + 1);
}

/* ─── Public CAL API ─────────────────────────────────────────────────────────*/

ufs_cal_errno ufs_cal_post_h8_enter (struct ufs_cal_param *p)
{
  const struct ufs_cal_phy_cfg *cfg;
  cfg = (p->tbl == HOST_CARD) ? post_h8_enter_card : post_h8_enter;
  return ufs_cal_config_uic (p, cfg, p->pmd);
}

ufs_cal_errno ufs_cal_pre_h8_exit (struct ufs_cal_param *p)
{
  const struct ufs_cal_phy_cfg *cfg;
  cfg = (p->tbl == HOST_CARD) ? pre_h8_exit_card : pre_h8_exit;
  return ufs_cal_config_uic (p, cfg, p->pmd);
}

ufs_cal_errno ufs_cal_pre_pmc (struct ufs_cal_param *p)
{
  const struct ufs_cal_phy_cfg *cfg;

  if ((p->pmd->mode == SLOW_MODE) || (p->pmd->mode == SLOWAUTO_MODE))
    cfg = (p->tbl == HOST_CARD) ? calib_of_pwm_card : calib_of_pwm;
  else if (p->pmd->hs_series == PA_HS_MODE_B)
    cfg = (p->tbl == HOST_CARD) ? calib_of_hs_rate_b_card : calib_of_hs_rate_b;
  else if (p->pmd->hs_series == PA_HS_MODE_A)
    cfg = (p->tbl == HOST_CARD) ? calib_of_hs_rate_a_card : calib_of_hs_rate_a;
  else
    return UFS_CAL_INV_ARG;

  return ufs_cal_config_uic (p, cfg, p->pmd);
}

ufs_cal_errno ufs_cal_post_pmc (struct ufs_cal_param *p)
{
  const struct ufs_cal_phy_cfg *cfg;

  if ((p->pmd->mode == SLOWAUTO_MODE) || (p->pmd->mode == SLOW_MODE))
    cfg = (p->tbl == HOST_CARD) ? post_calib_of_pwm_card : post_calib_of_pwm;
  else if (p->pmd->hs_series == PA_HS_MODE_B)
    cfg = (p->tbl == HOST_CARD) ? post_calib_of_hs_rate_b_card : post_calib_of_hs_rate_b;
  else if (p->pmd->hs_series == PA_HS_MODE_A)
    cfg = (p->tbl == HOST_CARD) ? post_calib_of_hs_rate_a_card : post_calib_of_hs_rate_a;
  else
    return UFS_CAL_INV_ARG;

  return ufs_cal_config_uic (p, cfg, p->pmd);
}

ufs_cal_errno ufs_cal_post_link (struct ufs_cal_param *p)
{
  ufs_cal_errno                 ret = UFS_CAL_NO_ERROR;
  const struct ufs_cal_phy_cfg *cfg;

  ufs_cal_calib_hibern8_values (p->host);

  switch (p->max_gear) {
  case GEAR_1:
  case GEAR_2:
  case GEAR_3:
    cfg = (p->evt_ver == 0) ? post_init_cfg_evt0_g3 : post_init_cfg_evt1_g3;
    if (p->tbl == HOST_CARD) cfg = post_init_cfg_card;
    break;
  case GEAR_4:
    cfg = (p->evt_ver == 0) ? post_init_cfg_evt0_g4 : post_init_cfg_evt1_g4;
    if (p->tbl == HOST_CARD) cfg = post_init_cfg_card;
    break;
  default:
    return UFS_CAL_INV_ARG;
  }

  ret = ufs_cal_config_uic (p, cfg, NULL);

  if (ret == UFS_CAL_NO_ERROR) {
    if ((p->available_lane == 2) && (p->connected_rx_lane == 1)) {
      cfg = (p->tbl == HOST_CARD) ? lane1_sq_off_card : lane1_sq_off;
      ret = ufs_cal_config_uic (p, cfg, NULL);
    }
  }

  return ret;
}

ufs_cal_errno ufs_cal_pre_link (struct ufs_cal_param *p)
{
  const struct ufs_cal_phy_cfg *cfg;

  if (p->tbl == HOST_CARD) {
    cfg = init_cfg_card;
  } else {
    cfg = (p->evt_ver == 0) ? init_cfg_evt0 : init_cfg_evt1;
  }

  return ufs_cal_config_uic (p, cfg, NULL);
}

ufs_cal_errno ufs_cal_init (struct ufs_cal_param *p, int idx)
{
  if (idx >= NUM_OF_UFS_HOST)
    return UFS_CAL_INV_ARG;

  ufs_cal[idx] = p;
  ufs_cal_lock_timeout = ufs_lld_calc_timeout (1);

  return UFS_CAL_NO_ERROR;
}
