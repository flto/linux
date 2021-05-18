#define CTL_TOP(i)			(0x15000 + 0x1000 * (i) + 0x014)
#define CTL_FLUSH(i)			(0x15000 + 0x1000 * (i) + 0x018)
#define CTL_START(i)			(0x15000 + 0x1000 * (i) + 0x01c)
#define CTL_PREPARE(i)			(0x15000 + 0x1000 * (i) + 0x0d0)
#define CTL_SW_RESET(i)			(0x15000 + 0x1000 * (i) + 0x030)
#define CTL_SW_RESET_OVERRIDE(i)	(0x15000 + 0x1000 * (i) + 0x060)
#define CTL_STATUS(i)			(0x15000 + 0x1000 * (i) + 0x064)
#define CTL_LAYER_EXTN_OFFSET(i)	(0x15000 + 0x1000 * (i) + 0x040)
#define CTL_ROT_TOP(i)			(0x15000 + 0x1000 * (i) + 0x0c0)
#define CTL_ROT_FLUSH(i)		(0x15000 + 0x1000 * (i) + 0x0c4)
#define CTL_ROT_START(i)		(0x15000 + 0x1000 * (i) + 0x0cc)
#define CTL_MERGE_3D_ACTIVE(i)		(0x15000 + 0x1000 * (i) + 0x0e4)
#define CTL_DSC_ACTIVE(i)		(0x15000 + 0x1000 * (i) + 0x0e8)
#define CTL_WB_ACTIVE(i)		(0x15000 + 0x1000 * (i) + 0x0ec)
#define CTL_CWB_ACTIVE(i)		(0x15000 + 0x1000 * (i) + 0x0f0)
#define CTL_INTF_ACTIVE(i)		(0x15000 + 0x1000 * (i) + 0x0f4)
#define CTL_CDM_ACTIVE(i)		(0x15000 + 0x1000 * (i) + 0x0f8)
#define CTL_FETCH_PIPE_ACTIVE(i)	(0x15000 + 0x1000 * (i) + 0x0fc)
#define CTL_MERGE_3D_FLUSH(i)		(0x15000 + 0x1000 * (i) + 0x100)
#define CTL_DSC_FLUSH(i)		(0x15000 + 0x1000 * (i) + 0x104)
#define CTL_WB_FLUSH(i)			(0x15000 + 0x1000 * (i) + 0x108)
#define CTL_CWB_FLUSH(i)		(0x15000 + 0x1000 * (i) + 0x10c)
#define CTL_INTF_FLUSH(i)		(0x15000 + 0x1000 * (i) + 0x110)
#define CTL_CDM_FLUSH(i)		(0x15000 + 0x1000 * (i) + 0x114)
#define CTL_PERIPH_FLUSH(i)		(0x15000 + 0x1000 * (i) + 0x128)
#define CTL_INTF_MASTER(i)		(0x15000 + 0x1000 * (i) + 0x134)
#define CTL_UIDLE_ACTIVE(i)		(0x15000 + 0x1000 * (i) + 0x138)

#define CTL_LAYER(i, lm) 	(0x15000 + 0x1000 * (i) + ((lm) == 5 ? 0x024 : (lm) * 0x004))
#define CTL_LAYER_EXT(i, lm) 	(0x15000 + 0x1000 * (i) + 0x40 + (lm) * 0x004)
#define CTL_LAYER_EXT2(i, lm)	(0x15000 + 0x1000 * (i) + 0x70 + (lm) * 0x004)
#define CTL_LAYER_EXT3(i, lm)	(0x15000 + 0x1000 * (i) + 0xa0 + (lm) * 0x004)

#define CTL_FLUSH_LM(i) ((i) == 5 ? BIT(20) : BIT(6 + i))
#define CTL_FLUSH_MASK_CTL BIT(17) /* always set when flushing mixer ? */
#define CTL_FLUSH_DSC BIT(22)
#define CTL_FLUSH_MERGE_3D BIT(23)
#define CTL_FLUSH_INTF BIT(31)

#define SSPP_SRC_SIZE(i)		(0x04000 + 0x2000 * (i) + 0x000)
#define SSPP_SRC_XY(i)			(0x04000 + 0x2000 * (i) + 0x008)
#define SSPP_OUT_SIZE(i)		(0x04000 + 0x2000 * (i) + 0x00c)
#define SSPP_OUT_XY(i)			(0x04000 + 0x2000 * (i) + 0x010)
#define SSPP_SRC0_ADDR(i)		(0x04000 + 0x2000 * (i) + 0x014)
#define SSPP_SRC1_ADDR(i)		(0x04000 + 0x2000 * (i) + 0x018)
#define SSPP_SRC2_ADDR(i)		(0x04000 + 0x2000 * (i) + 0x01c)
#define SSPP_SRC3_ADDR(i)		(0x04000 + 0x2000 * (i) + 0x020)
#define SSPP_SRC_YSTRIDE0(i)		(0x04000 + 0x2000 * (i) + 0x024)
#define SSPP_SRC_YSTRIDE1(i)		(0x04000 + 0x2000 * (i) + 0x028)
#define SSPP_SRC_FORMAT(i)		(0x04000 + 0x2000 * (i) + 0x030)
#define SSPP_SRC_UNPACK_PATTERN(i)	(0x04000 + 0x2000 * (i) + 0x034)
#define SSPP_SRC_OP_MODE(i)		(0x04000 + 0x2000 * (i) + 0x038)
#define SSPP_SRC_SIZE_REC1(i)		(0x04000 + 0x2000 * (i) + 0x16c)
#define SSPP_SRC_XY_REC1(i)		(0x04000 + 0x2000 * (i) + 0x168)
#define SSPP_OUT_SIZE_REC1(i)		(0x04000 + 0x2000 * (i) + 0x160)
#define SSPP_OUT_XY_REC1(i)		(0x04000 + 0x2000 * (i) + 0x164)
#define SSPP_SRC_FORMAT_REC1(i)		(0x04000 + 0x2000 * (i) + 0x174)
#define SSPP_SRC_UNPACK_PATTERN_REC1(i)	(0x04000 + 0x2000 * (i) + 0x178)
#define SSPP_SRC_OP_MODE_REC1(i)	(0x04000 + 0x2000 * (i) + 0x17c)
#define SSPP_MULTIRECT_OPMODE(i)	(0x04000 + 0x2000 * (i) + 0x170)
#define SSPP_SRC_CONSTANT_COLOR_REC1(i)	(0x04000 + 0x2000 * (i) + 0x180)
#define SSPP_EXCL_REC_SIZE_REC1(i)	(0x04000 + 0x2000 * (i) + 0x184)
#define SSPP_EXCL_REC_XY_REC1(i)	(0x04000 + 0x2000 * (i) + 0x188)
#define SSPP_SRC_CONSTANT_COLOR(i)	(0x04000 + 0x2000 * (i) + 0x03c)
#define SSPP_EXCL_REC_CTL(i)		(0x04000 + 0x2000 * (i) + 0x040)
#define SSPP_UBWC_STATIC_CTRL(i)	(0x04000 + 0x2000 * (i) + 0x044)
#define SSPP_FETCH_CONFIG(i)		(0x04000 + 0x2000 * (i) + 0x048)
#define SSPP_DANGER_LUT(i)		(0x04000 + 0x2000 * (i) + 0x060)
#define SSPP_SAFE_LUT(i)		(0x04000 + 0x2000 * (i) + 0x064)
#define SSPP_CREQ_LUT(i)		(0x04000 + 0x2000 * (i) + 0x068)
#define SSPP_QOS_CTRL(i)		(0x04000 + 0x2000 * (i) + 0x06c)
#define SSPP_DECIMATION_CONFIG(i)	(0x04000 + 0x2000 * (i) + 0x0b4)
#define SSPP_SRC_ADDR_SW_STATUS(i)	(0x04000 + 0x2000 * (i) + 0x070)
#define SSPP_CREQ_LUT_0(i)		(0x04000 + 0x2000 * (i) + 0x074)
#define SSPP_CREQ_LUT_1(i)		(0x04000 + 0x2000 * (i) + 0x078)
#define SSPP_SW_PIX_EXT_C0_LR(i)	(0x04000 + 0x2000 * (i) + 0x100)
#define SSPP_SW_PIX_EXT_C0_TB(i)	(0x04000 + 0x2000 * (i) + 0x104)
#define SSPP_SW_PIX_EXT_C0_REQ_PIXELS(i)(0x04000 + 0x2000 * (i) + 0x108)
#define SSPP_SW_PIX_EXT_C1C2_LR(i)	(0x04000 + 0x2000 * (i) + 0x110)
#define SSPP_SW_PIX_EXT_C1C2_TB(i)	(0x04000 + 0x2000 * (i) + 0x114)
#define SSPP_SW_PIX_EXT_C1C2_REQ_PIXELS(i)(0x04000 + 0x2000 * (i) + 0x118)
#define SSPP_SW_PIX_EXT_C3_LR(i)	(0x04000 + 0x2000 * (i) + 0x120)
#define SSPP_SW_PIX_EXT_C3_TB(i)	(0x04000 + 0x2000 * (i) + 0x124)
#define SSPP_SW_PIX_EXT_C3_REQ_PIXELS(i)(0x04000 + 0x2000 * (i) + 0x128)
#define SSPP_TRAFFIC_SHAPER(i)		(0x04000 + 0x2000 * (i) + 0x130)
#define SSPP_CDP_CNTL(i)		(0x04000 + 0x2000 * (i) + 0x134)
#define SSPP_UBWC_ERROR_STATUS(i)	(0x04000 + 0x2000 * (i) + 0x138)
#define SSPP_TRAFFIC_SHAPER_PREFILL(i)	(0x04000 + 0x2000 * (i) + 0x150)
#define SSPP_TRAFFIC_SHAPER_REC1_PREFILL(i)(0x04000 + 0x2000 * (i) + 0x154)
#define SSPP_TRAFFIC_SHAPER_REC1(i)	(0x04000 + 0x2000 * (i) + 0x158)
#define SSPP_EXCL_REC_SIZE(i)		(0x04000 + 0x2000 * (i) + 0x1b4)
#define SSPP_EXCL_REC_XY(i)		(0x04000 + 0x2000 * (i) + 0x1b8)
#define SSPP_UIDLE_CTRL_VALUE(i)	(0x04000 + 0x2000 * (i) + 0x1f0)

/* SSPP_SRC_OP_MODE */
#define MDSS_MDP_OP_DEINTERLACE            BIT(22)
#define MDSS_MDP_OP_DEINTERLACE_ODD        BIT(23)
#define MDSS_MDP_OP_IGC_ROM_1              BIT(18)
#define MDSS_MDP_OP_IGC_ROM_0              BIT(17)
#define MDSS_MDP_OP_IGC_EN                 BIT(16)
#define MDSS_MDP_OP_FLIP_UD                BIT(14)
#define MDSS_MDP_OP_FLIP_LR                BIT(13)
#define MDSS_MDP_OP_SPLIT_ORDER            BIT(4)
#define MDSS_MDP_OP_BWC_EN                 BIT(0)
#define MDSS_MDP_OP_PE_OVERRIDE            BIT(31)
#define MDSS_MDP_OP_BWC_LOSSLESS           (0 << 1)
#define MDSS_MDP_OP_BWC_Q_HIGH             (1 << 1)
#define MDSS_MDP_OP_BWC_Q_MED              (2 << 1)

/* SSPP_QOS_CTRL */
#define SSPP_QOS_CTRL_VBLANK_EN            BIT(16)
#define SSPP_QOS_CTRL_DANGER_SAFE_EN       BIT(0)
#define SSPP_QOS_CTRL_DANGER_VBLANK_MASK   0x3
#define SSPP_QOS_CTRL_DANGER_VBLANK_OFF    4
#define SSPP_QOS_CTRL_CREQ_VBLANK_MASK     0x3
#define SSPP_QOS_CTRL_CREQ_VBLANK_OFF      20

#define QSEED3_HW_VERSION(i)		(0x04a00 + 0x2000 * (i) + 0x000)
#define QSEED3_OP_MODE(i)		(0x04a00 + 0x2000 * (i) + 0x004)
#define QSEED3_RGB2Y_COEFF(i)		(0x04a00 + 0x2000 * (i) + 0x008)
#define QSEED3_PHASE_INIT(i)		(0x04a00 + 0x2000 * (i) + 0x00c)
#define QSEED3_PHASE_STEP_Y_H(i)	(0x04a00 + 0x2000 * (i) + 0x010)
#define QSEED3_PHASE_STEP_Y_V(i)	(0x04a00 + 0x2000 * (i) + 0x014)
#define QSEED3_PHASE_STEP_UV_H(i)	(0x04a00 + 0x2000 * (i) + 0x018)
#define QSEED3_PHASE_STEP_UV_V(i)	(0x04a00 + 0x2000 * (i) + 0x01c)
#define QSEED3_PRELOAD(i)		(0x04a00 + 0x2000 * (i) + 0x020)
#define QSEED3_DE_SHARPEN(i)		(0x04a00 + 0x2000 * (i) + 0x024)
#define QSEED3_DE_SHARPEN_CTL(i)	(0x04a00 + 0x2000 * (i) + 0x028)
#define QSEED3_DE_SHAPE_CTL(i)		(0x04a00 + 0x2000 * (i) + 0x02c)
#define QSEED3_DE_THRESHOLD(i)		(0x04a00 + 0x2000 * (i) + 0x030)
#define QSEED3_DE_ADJUST_DATA_0(i)	(0x04a00 + 0x2000 * (i) + 0x034)
#define QSEED3_DE_ADJUST_DATA_1(i)	(0x04a00 + 0x2000 * (i) + 0x038)
#define QSEED3_DE_ADJUST_DATA_2(i)	(0x04a00 + 0x2000 * (i) + 0x03c)
#define QSEED3_SRC_SIZE_Y_RGB_A(i)	(0x04a00 + 0x2000 * (i) + 0x040)
#define QSEED3_SRC_SIZE_UV(i)		(0x04a00 + 0x2000 * (i) + 0x044)
#define QSEED3_DST_SIZE(i)		(0x04a00 + 0x2000 * (i) + 0x048)
#define QSEED3_COEF_LUT_CTRL(i)		(0x04a00 + 0x2000 * (i) + 0x04c)

#define SSPP_VIG_CSC_10_OP_MODE(i)	(0x05a00 + 0x2000 * (i) + 0x000)

#define LM_OP_MODE(i)			(0x44000 + 0x1000 * (i) + 0x000)
#define LM_OUT_SIZE(i)			(0x44000 + 0x1000 * (i) + 0x004)
#define LM_BORDER_COLOR_0(i)		(0x44000 + 0x1000 * (i) + 0x008)
#define LM_BORDER_COLOR_1(i)		(0x44000 + 0x1000 * (i) + 0x010)
#define LM_MISR_CTRL(i)			(0x44000 + 0x1000 * (i) + 0x310)
#define LM_MISR_SIGNATURE(i)		(0x44000 + 0x1000 * (i) + 0x314)
#define LM_BLEND0_OP(i, j)		(0x44020 + 0x1000 * (i) + 0x18 * (j) + 0x00)
#define LM_BLEND0_CONST_ALPHA(i, j)	(0x44020 + 0x1000 * (i) + 0x18 * (j) + 0x04)
#define LM_FG_COLOR_FILL_COLOR_0(i, j)	(0x44020 + 0x1000 * (i) + 0x18 * (j) + 0x08)
#define LM_FG_COLOR_FILL_COLOR_1(i, j)	(0x44020 + 0x1000 * (i) + 0x18 * (j) + 0x0c)
#define LM_FG_COLOR_FILL_SIZE(i, j)	(0x44020 + 0x1000 * (i) + 0x18 * (j) + 0x10)
#define LM_FG_COLOR_FILL_XY(i, j)	(0x44020 + 0x1000 * (i) + 0x18 * (j) + 0x14)

#define INTF_TIMING_ENGINE_EN(i)	(0x34000 + 0x1000 * (i) + 0x000)
#define INTF_CONFIG(i)			(0x34000 + 0x1000 * (i) + 0x004)
#define INTF_HSYNC_CTL(i)		(0x34000 + 0x1000 * (i) + 0x008)
#define INTF_VSYNC_PERIOD_F0(i)		(0x34000 + 0x1000 * (i) + 0x00c)
#define INTF_VSYNC_PERIOD_F1(i)		(0x34000 + 0x1000 * (i) + 0x010)
#define INTF_VSYNC_PULSE_WIDTH_F0(i)	(0x34000 + 0x1000 * (i) + 0x014)
#define INTF_VSYNC_PULSE_WIDTH_F1(i)	(0x34000 + 0x1000 * (i) + 0x018)
#define INTF_DISPLAY_V_START_F0(i)	(0x34000 + 0x1000 * (i) + 0x01c)
#define INTF_DISPLAY_V_START_F1(i)	(0x34000 + 0x1000 * (i) + 0x020)
#define INTF_DISPLAY_V_END_F0(i)	(0x34000 + 0x1000 * (i) + 0x024)
#define INTF_DISPLAY_V_END_F1(i)	(0x34000 + 0x1000 * (i) + 0x028)
#define INTF_ACTIVE_V_START_F0(i)	(0x34000 + 0x1000 * (i) + 0x02c)
#define INTF_ACTIVE_V_START_F1(i)	(0x34000 + 0x1000 * (i) + 0x030)
#define INTF_ACTIVE_V_END_F0(i)		(0x34000 + 0x1000 * (i) + 0x034)
#define INTF_ACTIVE_V_END_F1(i)		(0x34000 + 0x1000 * (i) + 0x038)
#define INTF_DISPLAY_HCTL(i)		(0x34000 + 0x1000 * (i) + 0x03c)
#define INTF_ACTIVE_HCTL(i)		(0x34000 + 0x1000 * (i) + 0x040)
#define INTF_BORDER_COLOR(i)		(0x34000 + 0x1000 * (i) + 0x044)
#define INTF_UNDERFLOW_COLOR(i)		(0x34000 + 0x1000 * (i) + 0x048)
#define INTF_HSYNC_SKEW(i)		(0x34000 + 0x1000 * (i) + 0x04c)
#define INTF_POLARITY_CTL(i)		(0x34000 + 0x1000 * (i) + 0x050)
#define INTF_TEST_CTL(i)		(0x34000 + 0x1000 * (i) + 0x054)
#define INTF_TP_COLOR0(i)		(0x34000 + 0x1000 * (i) + 0x058)
#define INTF_TP_COLOR1(i)		(0x34000 + 0x1000 * (i) + 0x05c)
#define INTF_CONFIG2(i)			(0x34000 + 0x1000 * (i) + 0x060)
#define INTF_DISPLAY_DATA_HCTL(i)	(0x34000 + 0x1000 * (i) + 0x064)
#define INTF_ACTIVE_DATA_HCTL(i)	(0x34000 + 0x1000 * (i) + 0x068)
#define INTF_FRAME_LINE_COUNT_EN(i)	(0x34000 + 0x1000 * (i) + 0x0a8)
#define INTF_FRAME_COUNT(i)		(0x34000 + 0x1000 * (i) + 0x0ac)
#define INTF_LINE_COUNT(i)		(0x34000 + 0x1000 * (i) + 0x0b0)
#define INTF_DEFLICKER_CONFIG(i)	(0x34000 + 0x1000 * (i) + 0x0f0)
#define INTF_DEFLICKER_STRNG_COEFF(i)	(0x34000 + 0x1000 * (i) + 0x0f4)
#define INTF_DEFLICKER_WEAK_COEFF(i)	(0x34000 + 0x1000 * (i) + 0x0f8)
#define INTF_REG_SPLIT_LINK(i)		(0x34000 + 0x1000 * (i) + 0x080)
#define INTF_DSI_CMD_MODE_TRIGGER_EN(i)	(0x34000 + 0x1000 * (i) + 0x084)
#define INTF_PANEL_FORMAT(i)		(0x34000 + 0x1000 * (i) + 0x090)
#define INTF_TPG_ENABLE(i)		(0x34000 + 0x1000 * (i) + 0x100)
#define INTF_TPG_MAIN_CONTROL(i)	(0x34000 + 0x1000 * (i) + 0x104)
#define INTF_TPG_VIDEO_CONFIG(i)	(0x34000 + 0x1000 * (i) + 0x108)
#define INTF_TPG_COMPONENT_LIMITS(i)	(0x34000 + 0x1000 * (i) + 0x10c)
#define INTF_TPG_RECTANGLE(i)		(0x34000 + 0x1000 * (i) + 0x110)
#define INTF_TPG_INITIAL_VALUE(i)	(0x34000 + 0x1000 * (i) + 0x114)
#define INTF_TPG_BLK_WHITE_PATTERN_FRAMES(i) (0x34000 + 0x1000 * (i) + 0x118)
#define INTF_TPG_RGB_MAPPING(i)		(0x34000 + 0x1000 * (i) + 0x11c)
#define INTF_PROG_FETCH_START(i)	(0x34000 + 0x1000 * (i) + 0x170)
#define INTF_PROG_ROT_START(i)		(0x34000 + 0x1000 * (i) + 0x174)
#define INTF_MISR_CTRL(i)		(0x34000 + 0x1000 * (i) + 0x180)
#define INTF_MISR_SIGNATURE(i)		(0x34000 + 0x1000 * (i) + 0x184)
#define INTF_MUX(i)			(0x34000 + 0x1000 * (i) + 0x25c)
#define INTF_STATUS(i)			(0x34000 + 0x1000 * (i) + 0x26c)
#define INTF_AVR_CONTROL(i)		(0x34000 + 0x1000 * (i) + 0x270)
#define INTF_AVR_MODE(i)		(0x34000 + 0x1000 * (i) + 0x274)
#define INTF_AVR_TRIGGER(i)		(0x34000 + 0x1000 * (i) + 0x278)
#define INTF_AVR_VTOTAL(i)		(0x34000 + 0x1000 * (i) + 0x27c)
#define INTF_TEAR_MDP_VSYNC_SEL(i)	(0x34000 + 0x1000 * (i) + 0x280)
#define INTF_TEAR_TEAR_CHECK_EN(i)	(0x34000 + 0x1000 * (i) + 0x284)
#define INTF_TEAR_SYNC_CONFIG_VSYNC(i)	(0x34000 + 0x1000 * (i) + 0x288)
#define INTF_TEAR_SYNC_CONFIG_HEIGHT(i)	(0x34000 + 0x1000 * (i) + 0x28c)
#define INTF_TEAR_SYNC_WRCOUNT(i)	(0x34000 + 0x1000 * (i) + 0x290)
#define INTF_TEAR_VSYNC_INIT_VAL(i)	(0x34000 + 0x1000 * (i) + 0x294)
#define INTF_TEAR_INT_COUNT_VAL(i)	(0x34000 + 0x1000 * (i) + 0x298)
#define INTF_TEAR_SYNC_THRESH(i)	(0x34000 + 0x1000 * (i) + 0x29c)
#define INTF_TEAR_START_POS(i)		(0x34000 + 0x1000 * (i) + 0x2a0)
#define INTF_TEAR_RD_PTR_IRQ(i)		(0x34000 + 0x1000 * (i) + 0x2a4)
#define INTF_TEAR_WR_PTR_IRQ(i)		(0x34000 + 0x1000 * (i) + 0x2a8)
#define INTF_TEAR_OUT_LINE_COUNT(i)	(0x34000 + 0x1000 * (i) + 0x2ac)
#define INTF_TEAR_LINE_COUNT(i)		(0x34000 + 0x1000 * (i) + 0x2b0)
#define INTF_TEAR_AUTOREFRESH_CONFIG(i)	(0x34000 + 0x1000 * (i) + 0x2b4)
#define INTF_TEAR_TEAR_DETECT_CTRL(i)	(0x34000 + 0x1000 * (i) + 0x2b8)

#define MERGE_3D_MUX(i)			(0x4e000 + 0x1000 * (i) + 0x000)
#define MERGE_3D_MODE(i)		(0x4e000 + 0x1000 * (i) + 0x004)

/* TOP */
#define DISP_INTF_SEL                   0x004
#define INTR_EN                         0x010
#define INTR_STATUS                     0x014
#define INTR_CLEAR                      0x018
#define INTR2_EN                        0x008
#define INTR2_STATUS                    0x00c
#define INTR2_CLEAR                     0x02c
#define HIST_INTR_EN                    0x01c
#define HIST_INTR_STATUS                0x020
#define HIST_INTR_CLEAR                 0x024
#define INTF_INTR_EN                    0x1C0
#define INTF_INTR_STATUS                0x1C4
#define INTF_INTR_CLEAR                 0x1C8
#define SPLIT_DISPLAY_EN                0x2F4
#define SPLIT_DISPLAY_UPPER_PIPE_CTRL   0x2F8
#define DSPP_IGC_COLOR0_RAM_LUTN        0x300
#define DSPP_IGC_COLOR1_RAM_LUTN        0x304
#define DSPP_IGC_COLOR2_RAM_LUTN        0x308
#define HW_EVENTS_CTL                   0x37C
#define CLK_CTRL3                       0x3A8
#define CLK_STATUS3                     0x3AC
#define CLK_CTRL4                       0x3B0
#define CLK_STATUS4                     0x3B4
#define CLK_CTRL5                       0x3B8
#define CLK_STATUS5                     0x3BC
#define CLK_CTRL7                       0x3D0
#define CLK_STATUS7                     0x3D4
#define SPLIT_DISPLAY_LOWER_PIPE_CTRL   0x3F0
#define SPLIT_DISPLAY_TE_LINE_INTERVAL  0x3F4
#define INTF_SW_RESET_MASK              0x3FC
#define HDMI_DP_CORE_SELECT             0x408
#define MDP_OUT_CTL_0                   0x410
#define MDP_VSYNC_SEL                   0x414
#define DCE_SEL                         0x450

/* INTR */
#define INTF_UNDER_RUN(i) BIT(24 + 2 * (i))
#define INTF_VSYNC(i) BIT(25 + 2 * (i))
/* INTR2 */
#define INTR2_CTL_START(i) BIT(9 + (i))

/* VBIF regs - separate region */
#define VBIF_VERSION			0x0000
#define VBIF_CLK_FORCE_CTRL0		0x0008
#define VBIF_CLK_FORCE_CTRL1		0x000C
#define VBIF_QOS_REMAP_00		0x0020
#define VBIF_QOS_REMAP_01		0x0024
#define VBIF_QOS_REMAP_10		0x0028
#define VBIF_QOS_REMAP_11		0x002C
#define VBIF_WRITE_GATHER_EN		0x00AC
#define VBIF_IN_RD_LIM_CONF0		0x00B0
#define VBIF_IN_RD_LIM_CONF1		0x00B4
#define VBIF_IN_RD_LIM_CONF2		0x00B8
#define VBIF_IN_WR_LIM_CONF0		0x00C0
#define VBIF_IN_WR_LIM_CONF1		0x00C4
#define VBIF_IN_WR_LIM_CONF2		0x00C8
#define VBIF_OUT_RD_LIM_CONF0		0x00D0
#define VBIF_OUT_WR_LIM_CONF0		0x00D4
#define VBIF_OUT_AXI_AMEMTYPE_CONF0	0x0160
#define VBIF_OUT_AXI_AMEMTYPE_CONF1	0x0164
#define VBIF_XIN_PND_ERR		0x0190
#define VBIF_XIN_SRC_ERR		0x0194
#define VBIF_XIN_CLR_ERR		0x019C
#define VBIF_XIN_HALT_CTRL0		0x0200
#define VBIF_XIN_HALT_CTRL1		0x0204
#define VBIF_XINL_QOS_RP_REMAP_000	0x0550
#define VBIF_XINL_QOS_LVL_REMAP_000	0x0590

#define PP_TEAR_CHECK_EN(i)		(0x69000 + 0x1000 * (i) + 0x000)
#define PP_SYNC_CONFIG_VSYNC(i)		(0x69000 + 0x1000 * (i) + 0x004)
#define PP_SYNC_CONFIG_HEIGHT(i)	(0x69000 + 0x1000 * (i) + 0x008)
#define PP_SYNC_WRCOUNT(i)		(0x69000 + 0x1000 * (i) + 0x00c)
#define PP_VSYNC_INIT_VAL(i)		(0x69000 + 0x1000 * (i) + 0x010)
#define PP_INT_COUNT_VAL(i)		(0x69000 + 0x1000 * (i) + 0x014)
#define PP_SYNC_THRESH(i)		(0x69000 + 0x1000 * (i) + 0x018)
#define PP_START_POS(i)			(0x69000 + 0x1000 * (i) + 0x01c)
#define PP_RD_PTR_IRQ(i)		(0x69000 + 0x1000 * (i) + 0x020)
#define PP_WR_PTR_IRQ(i)		(0x69000 + 0x1000 * (i) + 0x024)
#define PP_OUT_LINE_COUNT(i)		(0x69000 + 0x1000 * (i) + 0x028)
#define PP_LINE_COUNT(i)		(0x69000 + 0x1000 * (i) + 0x02c)
#define PP_AUTOREFRESH_CONFIG(i)	(0x69000 + 0x1000 * (i) + 0x030)
#define PP_FBC_MODE(i)			(0x69000 + 0x1000 * (i) + 0x034)
#define PP_FBC_BUDGET_CTL(i)		(0x69000 + 0x1000 * (i) + 0x038)
#define PP_FBC_LOSSY_MODE(i)		(0x69000 + 0x1000 * (i) + 0x03c)
#define PP_DSC_MODE(i)			(0x69000 + 0x1000 * (i) + 0x0a0)
#define PP_DCE_DATA_IN_SWAP(i)		(0x69000 + 0x1000 * (i) + 0x0ac)
#define PP_DCE_DATA_OUT_SWAP(i)		(0x69000 + 0x1000 * (i) + 0x0c8)

// XXX WRONG for SM8350
#define DSC_COMMON_MODE(i)		(0x80000 + 0x0400 * (i) + 0x000)
#define DSC_ENC(i)			(0x80000 + 0x0400 * (i) + 0x004)
#define DSC_PICTURE(i)			(0x80000 + 0x0400 * (i) + 0x008)
#define DSC_SLICE(i)			(0x80000 + 0x0400 * (i) + 0x00c)
#define DSC_CHUNK_SIZE(i)		(0x80000 + 0x0400 * (i) + 0x010)
#define DSC_DELAY(i)			(0x80000 + 0x0400 * (i) + 0x014)
#define DSC_SCALE_INITIAL(i)		(0x80000 + 0x0400 * (i) + 0x018)
#define DSC_SCALE_DEC_INTERVAL(i)	(0x80000 + 0x0400 * (i) + 0x01c)
#define DSC_SCALE_INC_INTERVAL(i)	(0x80000 + 0x0400 * (i) + 0x020)
#define DSC_FIRST_LINE_BPG_OFFSET(i)	(0x80000 + 0x0400 * (i) + 0x024)
#define DSC_BPG_OFFSET(i)		(0x80000 + 0x0400 * (i) + 0x028)
#define DSC_DSC_OFFSET(i)		(0x80000 + 0x0400 * (i) + 0x02c)
#define DSC_FLATNESS(i)			(0x80000 + 0x0400 * (i) + 0x030)
#define DSC_RC_MODEL_SIZE(i)		(0x80000 + 0x0400 * (i) + 0x034)
#define DSC_RC(i)			(0x80000 + 0x0400 * (i) + 0x038)
#define DSC_RC_BUF_THRESH(i)		(0x80000 + 0x0400 * (i) + 0x03c)
#define DSC_RANGE_MIN_QP(i)		(0x80000 + 0x0400 * (i) + 0x074)
#define DSC_RANGE_MAX_QP(i)		(0x80000 + 0x0400 * (i) + 0x0b0)
#define DSC_RANGE_BPG_OFFSET(i)		(0x80000 + 0x0400 * (i) + 0x0ec)
#define DSC_CTL(i) 			(0x81800 + 0x4 * (i))
