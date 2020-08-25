#include "core.h"

/* common */
#define BUFFER_ALIGNMENT_SIZE(x) x

#define VENUS_DMA_ALIGNMENT BUFFER_ALIGNMENT_SIZE(256)

#define MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE   64
#define MAX_FE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE   64
#define MAX_FE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE   64
#define MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE   640
#define MAX_FE_NBR_DATA_CB_LINE_BUFFER_SIZE     320
#define MAX_FE_NBR_DATA_CR_LINE_BUFFER_SIZE     320

#define MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE     (128 / 8)
#define MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE     (128 / 8)
#define MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE     (128 / 8)

#define MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE     (64 * 2 * 3)
#define MAX_PE_NBR_DATA_LCU32_LINE_BUFFER_SIZE     (32 * 2 * 3)
#define MAX_PE_NBR_DATA_LCU16_LINE_BUFFER_SIZE     (16 * 2 * 3)

#define MAX_TILE_COLUMNS 32     /* 8K/256 */

#define NUM_HW_PIC_BUF 10
#define BIN_BUFFER_THRESHOLD (1280 * 736)

#define HFI_IRIS2_ENC_PERSIST_SIZE 102400

#define HFI_MAX_COL_FRAME 6
#define HFI_VENUS_VENC_TRE_WB_BUFF_SIZE (65 << 4) // bytes
#define HFI_VENUS_VENC_DB_LINE_BUFF_PER_MB      512
#define HFI_VENUS_VPPSG_MAX_REGISTERS  2048
#define HFI_VENUS_WIDTH_ALIGNMENT 128
#define HFI_VENUS_WIDTH_TEN_BIT_ALIGNMENT 192
#define HFI_VENUS_HEIGHT_ALIGNMENT 32

#define SYSTEM_LAL_TILE10 192
#define NUM_MBS_720P (((1280 + 15) >> 4) * ((720 + 15) >> 4))
#define NUM_MBS_4k (((4096 + 15) >> 4) * ((2304 + 15) >> 4))
#define MB_SIZE_IN_PIXEL (16 * 16)
#define HDR10PLUS_PAYLOAD_SIZE 1024
#define HDR10_HIST_EXTRADATA_SIZE 4096

static inline u32 size_vpss_lb(u32 width, u32 height, u32 num_vpp_pipes)
{
	u32 vpss_4tap_top_buffer_size, vpss_div2_top_buffer_size;
	u32 vpss_4tap_left_buffer_size, vpss_div2_left_buffer_size;
	u32 opb_wr_top_line_luma_buf_size, opb_wr_top_line_chroma_buf_size;
	u32 opb_lb_wr_llb_y_buffer_size, opb_lb_wr_llb_uv_buffer_size;
	u32 macrotiling_size;
	u32 size = 0;

	vpss_4tap_top_buffer_size = vpss_div2_top_buffer_size =
		vpss_4tap_left_buffer_size = vpss_div2_left_buffer_size = 0;
	macrotiling_size = 32;
	opb_wr_top_line_luma_buf_size = ALIGN(width, macrotiling_size) /
		macrotiling_size * 256;
	opb_wr_top_line_luma_buf_size = ALIGN(opb_wr_top_line_luma_buf_size,
		VENUS_DMA_ALIGNMENT) + (MAX_TILE_COLUMNS - 1) * 256;
	opb_wr_top_line_luma_buf_size = max(opb_wr_top_line_luma_buf_size,
		(32 * ALIGN(height, 16)));
	opb_wr_top_line_chroma_buf_size = opb_wr_top_line_luma_buf_size;
	opb_lb_wr_llb_uv_buffer_size = opb_lb_wr_llb_y_buffer_size =
		ALIGN((ALIGN(height, 16) / 2) *
			64, BUFFER_ALIGNMENT_SIZE(32));
	size = num_vpp_pipes * 2 * (vpss_4tap_top_buffer_size +
		vpss_div2_top_buffer_size) +
		2 * (vpss_4tap_left_buffer_size +
		vpss_div2_left_buffer_size) +
	opb_wr_top_line_luma_buf_size +
	opb_wr_top_line_chroma_buf_size +
	opb_lb_wr_llb_uv_buffer_size +
	opb_lb_wr_llb_y_buffer_size;

	return size;
}

/* H264 */
#define H264D_MAX_SLICE 1800
#define SIZE_H264D_BUFTAB_T  256 // sizeof(h264d_buftab_t) aligned to 256
#define SIZE_H264D_HW_PIC_T (1 << 11) // sizeof(h264d_hw_pic_t) 32 aligned
#define SIZE_H264D_BSE_CMD_PER_BUF (32 * 4)
#define SIZE_H264D_VPP_CMD_PER_BUF 512

// Line Buffer definitions
/* one for luma and 1/2 for each chroma */
#define SIZE_H264D_LB_FE_TOP_DATA(width, height) \
	(MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE * \
	ALIGN(width, 16) * 3)

#define SIZE_H264D_LB_FE_TOP_CTRL(width, height) \
	(MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * \
	((width + 15) >> 4))

#define SIZE_H264D_LB_FE_LEFT_CTRL(width, height) \
	(MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * \
	((height + 15) >> 4))

#define SIZE_H264D_LB_SE_TOP_CTRL(width, height) \
	(MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * \
	((width + 15) >> 4))

#define SIZE_H264D_LB_SE_LEFT_CTRL(width, height) \
	(MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * \
	((height + 15) >> 4))

#define SIZE_H264D_LB_PE_TOP_DATA(width, height) \
	(MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE * \
	((width + 15) >> 4))

#define SIZE_H264D_LB_VSP_TOP(width, height) \
	((((width + 15) >> 4) << 7))

#define SIZE_H264D_LB_RECON_DMA_METADATA_WR(width, height) \
	(ALIGN(height, 16) * 32)

#define SIZE_H264D_QP(width, height) \
	(((width + 63) >> 6) * ((height + 63) >> 6) * 128)

#define SIZE_HW_PIC(sizePerBuf) \
	(NUM_HW_PIC_BUF * sizePerBuf)

#define H264_CABAC_HDR_RATIO_HD_TOT 1
#define H264_CABAC_RES_RATIO_HD_TOT 3

#define NUM_SLIST_BUF_H264	(256 + 32)
#define SIZE_SLIST_BUF_H264	512

static inline u32 hfi_iris2_h264d_comv_size(u32 width, u32 height,
	u32 yuv_buf_min_count)
{
	u32 comv_size = 0;
	u32 frame_width_in_mbs = ((width + 15) >> 4);
	u32 frame_height_in_mbs = ((height + 15) >> 4);
	u32 col_mv_aligned_width = (frame_width_in_mbs << 6);
	u32 col_zero_aligned_width = (frame_width_in_mbs << 2);
	u32 col_zero_size = 0, size_colloc = 0;

	col_mv_aligned_width = ALIGN(col_mv_aligned_width, 16);
	col_zero_aligned_width = ALIGN(col_zero_aligned_width, 16);
	col_zero_size = col_zero_aligned_width * ((frame_height_in_mbs + 1) >> 1);
	col_zero_size = ALIGN(col_zero_size, BUFFER_ALIGNMENT_SIZE(64));
	col_zero_size <<= 1;
	col_zero_size = ALIGN(col_zero_size, BUFFER_ALIGNMENT_SIZE(512));
	size_colloc = col_mv_aligned_width  * ((frame_height_in_mbs + 1) >> 1);
	size_colloc = ALIGN(size_colloc, BUFFER_ALIGNMENT_SIZE(64));
	size_colloc <<= 1;
	size_colloc = ALIGN(size_colloc, BUFFER_ALIGNMENT_SIZE(512));
	size_colloc += (col_zero_size + SIZE_H264D_BUFTAB_T * 2);
	comv_size = size_colloc * yuv_buf_min_count;
	comv_size += BUFFER_ALIGNMENT_SIZE(512);

	return comv_size;
}

static inline u32 size_h264d_bse_cmd_buf(u32 height)
{
	u32 aligned_height = ALIGN(height, BUFFER_ALIGNMENT_SIZE(32));

	return min_t(u32, (((aligned_height + 15) >> 4) * 3 * 4),
		H264D_MAX_SLICE) *
		SIZE_H264D_BSE_CMD_PER_BUF;
}

static inline u32 size_h264d_vpp_cmd_buf(u32 height)
{
	u32 aligned_height = ALIGN(height, BUFFER_ALIGNMENT_SIZE(32));

	return min_t(u32, (((aligned_height + 15) >> 4) * 3 * 4),
		H264D_MAX_SLICE) *
		SIZE_H264D_VPP_CMD_PER_BUF;
}

static inline u32 hfi_iris2_h264d_non_comv_size(u32 width, u32 height,
	u32 num_vpp_pipes)
{
	u32 size;
	u32 size_bse, size_vpp;

	size_bse = size_h264d_bse_cmd_buf(height);
	size_vpp = size_h264d_vpp_cmd_buf(height);
	size = ALIGN(size_bse, VENUS_DMA_ALIGNMENT) +
		ALIGN(size_vpp, VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_HW_PIC(SIZE_H264D_HW_PIC_T), VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H264D_LB_FE_TOP_DATA(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H264D_LB_FE_TOP_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H264D_LB_FE_LEFT_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(SIZE_H264D_LB_SE_TOP_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H264D_LB_SE_LEFT_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(SIZE_H264D_LB_PE_TOP_DATA(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H264D_LB_VSP_TOP(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H264D_LB_RECON_DMA_METADATA_WR(width, height),
			VENUS_DMA_ALIGNMENT) * 2 +
		ALIGN(SIZE_H264D_QP(width, height), VENUS_DMA_ALIGNMENT);
	size = ALIGN(size, VENUS_DMA_ALIGNMENT);
	return size;
}

static inline u32 size_h264d_hw_bin_buffer(u32 width, u32 height)
{
	u32 size_yuv, size_bin_hdr, size_bin_res;
	u32 size = 0;
	u32 product;

	product = width * height;
	size_yuv = (product <= BIN_BUFFER_THRESHOLD) ?
			((BIN_BUFFER_THRESHOLD * 3) >> 1) :
			((product * 3) >> 1);

	size_bin_hdr = size_yuv * H264_CABAC_HDR_RATIO_HD_TOT;
	size_bin_res = size_yuv * H264_CABAC_RES_RATIO_HD_TOT;
	size_bin_hdr = ALIGN(size_bin_hdr, VENUS_DMA_ALIGNMENT);
	size_bin_res = ALIGN(size_bin_res, VENUS_DMA_ALIGNMENT);
	size = size_bin_hdr + size_bin_res;
	return size;
}

/* H265 */
#define LCU_MAX_SIZE_PELS 64
#define LCU_MIN_SIZE_PELS 16

#define H265D_MAX_SLICE 600
#define SIZE_H265D_HW_PIC_T SIZE_H264D_HW_PIC_T
#define SIZE_H265D_BSE_CMD_PER_BUF (16 * sizeof(u32))
#define SIZE_H265D_VPP_CMD_PER_BUF 256

#define SIZE_H265D_LB_FE_TOP_DATA(width, height) \
	(MAX_FE_NBR_DATA_LUMA_LINE_BUFFER_SIZE * \
	(ALIGN(width, 64) + 8) * 2)

#define SIZE_H265D_LB_FE_TOP_CTRL(width, height) \
	(MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * \
	(ALIGN(width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS))

#define SIZE_H265D_LB_FE_LEFT_CTRL(width, height) \
	(MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE * \
	(ALIGN(height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS))

#define SIZE_H265D_LB_SE_TOP_CTRL(width, height) \
	((LCU_MAX_SIZE_PELS / 8 * (128 / 8)) * \
	((width + 15) >> 4))

#define SIZE_H265D_LB_SE_LEFT_CTRL(width, height) \
	(max(((height + 16 - 1) / 8) * MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE,\
	max(((height + 32 - 1) / 8) * MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE, \
	((height + 64 - 1) / 8) * MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE)))

#define SIZE_H265D_LB_PE_TOP_DATA(width, height) \
	(MAX_PE_NBR_DATA_LCU64_LINE_BUFFER_SIZE * \
	(ALIGN(width, LCU_MIN_SIZE_PELS) / LCU_MIN_SIZE_PELS))

#define SIZE_H265D_LB_VSP_TOP(width, height) \
	(((width + 63) >> 6) * 128)

#define SIZE_H265D_LB_VSP_LEFT(width, height) \
	(((height + 63) >> 6) * 128)

#define SIZE_H265D_LB_RECON_DMA_METADATA_WR(width, height) \
	SIZE_H264D_LB_RECON_DMA_METADATA_WR(width, height)

#define SIZE_H265D_QP(width, height) SIZE_H264D_QP(width, height)

#define H265_CABAC_HDR_RATIO_HD_TOT 2
#define H265_CABAC_RES_RATIO_HD_TOT 2

#define SIZE_SLIST_BUF_H265 	(1 << 10)
#define NUM_SLIST_BUF_H265	(80 + 20)
#define H265_NUM_TILE_COL 	32
#define H265_NUM_TILE_ROW 	128
#define H265_NUM_TILE 		(H265_NUM_TILE_ROW * H265_NUM_TILE_COL + 1)

static inline u32 size_h265d_bse_cmd_buf(u32 width, u32 height)
{
	u32 size;

	size = (ALIGN(width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
		(ALIGN(height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
		NUM_HW_PIC_BUF;
	size = min_t(u32, size, H265D_MAX_SLICE + 1);
	size = 2 * size * SIZE_H265D_BSE_CMD_PER_BUF;
	size = ALIGN(size, VENUS_DMA_ALIGNMENT);

	return size;
}

static inline u32 size_h265d_vpp_cmd_buf(u32 width, u32 height)
{
	u32 size = 0;

	size = (ALIGN(width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
		(ALIGN(height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
		NUM_HW_PIC_BUF;
	size = min_t(u32, size, H265D_MAX_SLICE + 1);
	size = ALIGN(size, 4);
	size = 2 * size * SIZE_H265D_VPP_CMD_PER_BUF;
	size = ALIGN(size, VENUS_DMA_ALIGNMENT);

	return size;
}

static inline u32 hfi_iris2_h265d_comv_size(u32 width, u32 height,
	u32 yuv_buf_count_min)
{
	u32 size = 0;

	size = ALIGN(((((width + 15) >> 4) * ((height + 15) >> 4)) << 8),
		BUFFER_ALIGNMENT_SIZE(512));
	size *= yuv_buf_count_min;
	size += BUFFER_ALIGNMENT_SIZE(512);

	return size;
}

static inline u32 hfi_iris2_h265d_non_comv_size(u32 width, u32 height,
	u32 num_vpp_pipes)
{
	u32 size_bse, size_vpp;
	u32 size = 0;

	size_bse = size_h265d_bse_cmd_buf(width, height);
	size_vpp = size_h265d_vpp_cmd_buf(width, height);
	size = ALIGN(size_bse, VENUS_DMA_ALIGNMENT) +
		ALIGN(size_vpp, VENUS_DMA_ALIGNMENT) +
		ALIGN(NUM_HW_PIC_BUF * 20 * 22 * 4, VENUS_DMA_ALIGNMENT) +
		ALIGN(2 * sizeof(u16) *
		(ALIGN(width, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS) *
			(ALIGN(height, LCU_MAX_SIZE_PELS) / LCU_MIN_SIZE_PELS),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_HW_PIC(SIZE_H265D_HW_PIC_T), VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H265D_LB_FE_TOP_DATA(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H265D_LB_FE_TOP_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H265D_LB_FE_LEFT_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(SIZE_H265D_LB_SE_LEFT_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(SIZE_H265D_LB_SE_TOP_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H265D_LB_PE_TOP_DATA(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H265D_LB_VSP_TOP(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_H265D_LB_VSP_LEFT(width, height),
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(SIZE_H265D_LB_RECON_DMA_METADATA_WR(width, height),
			VENUS_DMA_ALIGNMENT) * 4 +
		ALIGN(SIZE_H265D_QP(width, height), VENUS_DMA_ALIGNMENT);
	size = ALIGN(size, VENUS_DMA_ALIGNMENT);
	return size;
}

static inline u32 size_h265d_hw_bin_buffer(u32 width, u32 height)
{
	u32 size = 0;
	u32 size_yuv, size_bin_hdr, size_bin_res;
	u32 product;

	product = width * height;
	size_yuv = (product <= BIN_BUFFER_THRESHOLD) ?
		((BIN_BUFFER_THRESHOLD * 3) >> 1) :
		((product * 3) >> 1);
	size_bin_hdr = size_yuv * H265_CABAC_HDR_RATIO_HD_TOT;
	size_bin_res = size_yuv * H265_CABAC_RES_RATIO_HD_TOT;
	size_bin_hdr = ALIGN(size_bin_hdr, VENUS_DMA_ALIGNMENT);
	size_bin_res = ALIGN(size_bin_res, VENUS_DMA_ALIGNMENT);
	size = size_bin_hdr + size_bin_res;

	return size;
}

/* VPx */
#define SIZE_VPXD_LB_FE_LEFT_CTRL(width, height) \
	max(((height + 15) >> 4) * MAX_FE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE, \
	max(((height + 31) >> 5) * MAX_FE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE, \
	((height + 63) >> 6) * MAX_FE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE))
#define SIZE_VPXD_LB_FE_TOP_CTRL(width, height) \
	(((ALIGN(width, 64) + 8) * 10 * 2)) /* + small line */
#define SIZE_VPXD_LB_SE_TOP_CTRL(width, height) \
	(((width + 15) >> 4) * MAX_FE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE)
#define SIZE_VPXD_LB_SE_LEFT_CTRL(width, height) \
	max(((height + 15) >> 4) * MAX_SE_NBR_CTRL_LCU16_LINE_BUFFER_SIZE, \
	max(((height + 31) >> 5) * MAX_SE_NBR_CTRL_LCU32_LINE_BUFFER_SIZE, \
	((height + 63) >> 6) * MAX_SE_NBR_CTRL_LCU64_LINE_BUFFER_SIZE))
#define SIZE_VPXD_LB_RECON_DMA_METADATA_WR(width, height) \
	ALIGN((ALIGN(height, 16) / (4 / 2)) * 64, BUFFER_ALIGNMENT_SIZE(32))
#define SIZE_VP8D_LB_FE_TOP_DATA(width, height) \
	((ALIGN(width, 16) + 8) * 10 * 2)
#define SIZE_VP9D_LB_FE_TOP_DATA(width, height) \
	((ALIGN(ALIGN(width, 16), 64) + 8) * 10 * 2)
#define SIZE_VP8D_LB_PE_TOP_DATA(width, height) \
	((ALIGN(width, 16) >> 4) * 64)
#define SIZE_VP9D_LB_PE_TOP_DATA(width, height) \
	((ALIGN(ALIGN(width, 16), 64) >> 6) * 176)
#define SIZE_VP8D_LB_VSP_TOP(width, height) \
	(((ALIGN(width, 16) >> 4) * 64 / 2) + 256)
#define SIZE_VP9D_LB_VSP_TOP(width, height) \
	(((ALIGN(ALIGN(width, 16), 64) >> 6) * 64 * 8) + 256)


#define HFI_IRIS2_VP9D_COMV_SIZE \
	((((8192 + 63) >> 6) * ((4320 + 63) >> 6) * 8 * 8 * 2 * 8))

#define VPX_DECODER_FRAME_CONCURENCY_LVL 2
#define VPX_DECODER_FRAME_BIN_HDR_BUDGET_RATIO_NUM 1
#define VPX_DECODER_FRAME_BIN_HDR_BUDGET_RATIO_DEN 2
#define VPX_DECODER_FRAME_BIN_RES_BUDGET_RATIO_NUM 3
#define VPX_DECODER_FRAME_BIN_RES_BUDGET_RATIO_DEN 2

#define VP8_NUM_FRAME_INFO_BUF (5 + 1)
#define VP9_NUM_FRAME_INFO_BUF (8 + 2 + 1 + 8)
#define VP8_NUM_PROBABILITY_TABLE_BUF (VP8_NUM_FRAME_INFO_BUF)
#define VP9_NUM_PROBABILITY_TABLE_BUF (VP9_NUM_FRAME_INFO_BUF + 4)
#define VP8_PROB_TABLE_SIZE 3840
#define VP9_PROB_TABLE_SIZE 3840

#define VP9_UDC_HEADER_BUF_SIZE (3 * 128)
#define MAX_SUPERFRAME_HEADER_LEN (34)
#define CCE_TILE_OFFSET_SIZE ALIGN(32 * 4 * 4, BUFFER_ALIGNMENT_SIZE(32))

static inline u32 size_vpxd_hw_bin_buffer(u32 width, u32 height)
{
	u32 size_yuv = width * height * 3 / 2;
	u32 binbuffer1_size = 0, binbufer2_size = 0;

	binbuffer1_size = max_t(u32, size_yuv,
		((BIN_BUFFER_THRESHOLD * 3) >> 1)) *
		VPX_DECODER_FRAME_CONCURENCY_LVL *
		VPX_DECODER_FRAME_BIN_HDR_BUDGET_RATIO_NUM /
		VPX_DECODER_FRAME_BIN_HDR_BUDGET_RATIO_DEN;
	binbufer2_size = max_t(u32, size_yuv,
		((BIN_BUFFER_THRESHOLD * 3) >> 1)) *
		VPX_DECODER_FRAME_CONCURENCY_LVL *
		VPX_DECODER_FRAME_BIN_RES_BUDGET_RATIO_NUM /
		VPX_DECODER_FRAME_BIN_RES_BUDGET_RATIO_DEN;
	return ALIGN(binbuffer1_size + binbufer2_size,
		VENUS_DMA_ALIGNMENT);
}

static inline u32 hfi_iris2_vp8d_comv_size(u32 width, u32 height)
{
	return (((width + 15) >> 4) * ((height + 15) >> 4) * 8 * 2);
}

/* MPEG2 */
#define QMATRIX_SIZE 		(sizeof(u32) * 128 + 256)
#define MP2D_QPDUMP_SIZE 	115200

static inline u32
enc_scratch_size(u32 width, u32 height, u32 bitbin_size,
		 u32 total_bitbin_buffers, u32 num_vpp_pipes, bool rc_lossless)
{
	u32 size_singlePipe, sao_bin_buffer_size, padded_bin_size;

	if (num_vpp_pipes > 2)
		size_singlePipe = bitbin_size / 2;
	else
		size_singlePipe = bitbin_size;
	if (rc_lossless)
		size_singlePipe <<= 1;
	size_singlePipe = ALIGN(size_singlePipe, VENUS_DMA_ALIGNMENT);
	sao_bin_buffer_size = (64 * (((width + BUFFER_ALIGNMENT_SIZE(32)) *
		(height + BUFFER_ALIGNMENT_SIZE(32))) >> 10)) + 384;
	padded_bin_size = ALIGN(size_singlePipe, VENUS_DMA_ALIGNMENT);
	size_singlePipe = sao_bin_buffer_size + padded_bin_size;
	size_singlePipe = ALIGN(size_singlePipe, VENUS_DMA_ALIGNMENT);
	bitbin_size = size_singlePipe * num_vpp_pipes;
	return ALIGN(bitbin_size, VENUS_DMA_ALIGNMENT) * total_bitbin_buffers + 512;
}

u32 msm_vidc_calculate_enc_output_frame_size(struct venus_inst *inst)
{
	u32 frame_size;
	u32 mbs_per_frame;
	u32 width, height;
#if 0
	struct v4l2_format *f;

	f = &inst->fmts[OUTPUT_PORT].v4l2_fmt;
	/*
	 * Encoder output size calculation: 32 Align width/height
	 * For resolution < 720p : YUVsize * 4
	 * For resolution > 720p & <= 4K : YUVsize / 2
	 * For resolution > 4k : YUVsize / 4
	 * Initially frame_size = YUVsize * 2;
	 */

	if (is_grid_session(inst)) {
		f->fmt.pix_mp.width = f->fmt.pix_mp.height = HEIC_GRID_DIMENSION;
	}
#endif
	width = ALIGN(inst->width, BUFFER_ALIGNMENT_SIZE(32));
	height = ALIGN(inst->height, BUFFER_ALIGNMENT_SIZE(32));
	mbs_per_frame = width * height / 256;
	frame_size = (width * height * 3);

	if (mbs_per_frame < NUM_MBS_720P)
		frame_size = frame_size << 1;
	else if (mbs_per_frame <= NUM_MBS_4k)
		frame_size = frame_size >> 2;
	else
		frame_size = frame_size >> 3;

#if 0
	if ((inst->rc_type == RATE_CONTROL_OFF) ||
		(inst->rc_type == V4L2_MPEG_VIDEO_BITRATE_MODE_CQ))
		frame_size = frame_size << 1;

	if (inst->rc_type == RATE_CONTROL_LOSSLESS)
		frame_size = (width * height * 9) >> 2;
#endif

	/* multiply by 10/8 (1.25) to get size for 10 bit case */
	if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC)
		frame_size = frame_size + (frame_size >> 2);

	return ALIGN(frame_size, SZ_4K);
}

static u32 scratch_size(struct venus_inst *inst)
{
	bool is_interlaced = false; /* TODO */
	u32 width = ALIGN(inst->width, 16);
	u32 height = ALIGN(inst->height, 16);

	if (inst->session_type == VIDC_SESSION_TYPE_ENC) {
		u32 work_mode = VIDC_WORK_MODE_2; /* TODO */
		bool rc_lossless = false; /* TODO */
		u32 num_vpp_pipes = 4; /* sm8250 num_vpp_pipes */
		u32 bitbin_size, total_bitbin_buffers;

		if (work_mode == VIDC_WORK_MODE_2) {
			bitbin_size = msm_vidc_calculate_enc_output_frame_size(inst);
			bitbin_size = ALIGN(bitbin_size, VENUS_DMA_ALIGNMENT);

			total_bitbin_buffers = 3;
			bitbin_size = ALIGN(bitbin_size * 17 / 10, 256);
		} else {
			total_bitbin_buffers = 1;
			if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC) {
				width = ALIGN(width, 32);
				height = ALIGN(height, 32);
			}
			bitbin_size = ALIGN(width * height * 3, 256);
		}

		return enc_scratch_size(inst->width, inst->height, bitbin_size,
					total_bitbin_buffers, num_vpp_pipes, rc_lossless);
	}

	if (is_interlaced)
		return 0;

	switch (inst->fmt_out->pixfmt) {
	case V4L2_PIX_FMT_H264:
		return size_h264d_hw_bin_buffer(width, height);
	case V4L2_PIX_FMT_HEVC:
		return size_h265d_hw_bin_buffer(width, height);
	case V4L2_PIX_FMT_VP8:
	case V4L2_PIX_FMT_VP9:
		return size_vpxd_hw_bin_buffer(width, height);
	case V4L2_PIX_FMT_MPEG2:
		return 0;
	default:
		unreachable();
	}
}

static inline u32 calculate_enc_scratch1_size(
	u32 width, u32 height, u32 lcu_size, u32 num_ref, bool ten_bit,
	u32 num_vpp_pipes, bool is_h265)
{
	u32 line_buf_ctrl_size, line_buf_data_size, leftline_buf_ctrl_size;
	u32 line_buf_sde_size, sps_pps_slice_hdr, topline_buf_ctrl_size_FE;
	u32 leftline_buf_ctrl_size_FE, line_buf_recon_pix_size;
	u32 leftline_buf_recon_pix_size, lambda_lut_size, override_buffer_size;
	u32 col_mv_buf_size, vpp_reg_buffer_size, ir_buffer_size;
	u32 vpss_line_buf, leftline_buf_meta_recony, h265e_colrcbuf_size;
	u32 h265e_framerc_bufsize, h265e_lcubitcnt_bufsize;
	u32 h265e_lcubitmap_bufsize, se_stats_bufsize;
	u32 bse_reg_buffer_size, bse_slice_cmd_buffer_size, slice_info_bufsize;
	u32 line_buf_ctrl_size_buffid2, slice_cmd_buffer_size;
	u32 width_lcu_num, height_lcu_num, width_coded, height_coded;
	u32 frame_num_lcu, linebuf_meta_recon_uv, topline_bufsize_fe_1stg_sao;
	u32 output_mv_bufsize = 0, temp_scratch_mv_bufsize = 0;
	u32 size, bit_depth, num_LCUMB;
	u32 vpss_lineBufferSize_1 = 0;

	width_lcu_num = ((width)+(lcu_size)-1) / (lcu_size);
	height_lcu_num = ((height)+(lcu_size)-1) / (lcu_size);
	frame_num_lcu = width_lcu_num * height_lcu_num;
	width_coded = width_lcu_num * (lcu_size);
	height_coded = height_lcu_num * (lcu_size);
	num_LCUMB = (height_coded / lcu_size) * ((width_coded + lcu_size * 8) / lcu_size);
	slice_info_bufsize = (256 + (frame_num_lcu << 4));
	slice_info_bufsize = ALIGN(slice_info_bufsize, VENUS_DMA_ALIGNMENT);
	line_buf_ctrl_size = ALIGN(width_coded, VENUS_DMA_ALIGNMENT);
	line_buf_ctrl_size_buffid2 = ALIGN(width_coded, VENUS_DMA_ALIGNMENT);

	bit_depth = ten_bit ? 10 : 8;
	line_buf_data_size = (((((bit_depth * width_coded + 1024) +
	(VENUS_DMA_ALIGNMENT - 1)) & (~(VENUS_DMA_ALIGNMENT - 1))) * 1) +
	(((((bit_depth * width_coded + 1024) >> 1) +
	(VENUS_DMA_ALIGNMENT - 1)) &
	(~(VENUS_DMA_ALIGNMENT - 1))) * 2));
	leftline_buf_ctrl_size = (is_h265) ?
		((height_coded + (BUFFER_ALIGNMENT_SIZE(32))) /
		BUFFER_ALIGNMENT_SIZE(32) * 4 * 16) :
		((height_coded + 15) / 16 * 5 * 16);
	if (num_vpp_pipes > 1) {
		leftline_buf_ctrl_size += BUFFER_ALIGNMENT_SIZE(512);
		leftline_buf_ctrl_size = ALIGN(leftline_buf_ctrl_size,
			BUFFER_ALIGNMENT_SIZE(512)) * num_vpp_pipes;
	}
	leftline_buf_ctrl_size = ALIGN(leftline_buf_ctrl_size,
		VENUS_DMA_ALIGNMENT);
	leftline_buf_recon_pix_size = (((ten_bit + 1) * 2 *
		(height_coded)+VENUS_DMA_ALIGNMENT) +
	(VENUS_DMA_ALIGNMENT << (num_vpp_pipes - 1)) - 1) &
		(~((VENUS_DMA_ALIGNMENT << (num_vpp_pipes - 1)) - 1)) * 1;
	topline_buf_ctrl_size_FE = (is_h265) ? (64 * (width_coded >> 5)) :
		(VENUS_DMA_ALIGNMENT + 16 * (width_coded >> 4));
	topline_buf_ctrl_size_FE = ALIGN(topline_buf_ctrl_size_FE,
		VENUS_DMA_ALIGNMENT);
	leftline_buf_ctrl_size_FE = (((VENUS_DMA_ALIGNMENT + 64 *
		(height_coded >> 4)) +
		(VENUS_DMA_ALIGNMENT << (num_vpp_pipes - 1)) - 1) &
		(~((VENUS_DMA_ALIGNMENT << (num_vpp_pipes - 1)) - 1)) * 1) *
		num_vpp_pipes;
	leftline_buf_meta_recony = (VENUS_DMA_ALIGNMENT + 64 *
		((height_coded) / (8 * (ten_bit ? 4 : 8))));
	leftline_buf_meta_recony = ALIGN(leftline_buf_meta_recony,
		VENUS_DMA_ALIGNMENT);
	leftline_buf_meta_recony = leftline_buf_meta_recony *
		num_vpp_pipes;
	linebuf_meta_recon_uv = (VENUS_DMA_ALIGNMENT + 64 *
		((height_coded) / (4 * (ten_bit ? 4 : 8))));
	linebuf_meta_recon_uv = ALIGN(linebuf_meta_recon_uv,
		VENUS_DMA_ALIGNMENT);
	linebuf_meta_recon_uv = linebuf_meta_recon_uv *
		num_vpp_pipes;
	line_buf_recon_pix_size = ((ten_bit ? 3 : 2) * width_coded);
	line_buf_recon_pix_size = ALIGN(line_buf_recon_pix_size,
		VENUS_DMA_ALIGNMENT);
	slice_cmd_buffer_size = ALIGN(20480, VENUS_DMA_ALIGNMENT);
	sps_pps_slice_hdr = 2048 + 4096;
	col_mv_buf_size = (is_h265) ? (16 * ((frame_num_lcu << 2) +
		BUFFER_ALIGNMENT_SIZE(32))) :
		(3 * 16 * (width_lcu_num * height_lcu_num +
		BUFFER_ALIGNMENT_SIZE(32)));
	col_mv_buf_size = ALIGN(col_mv_buf_size, VENUS_DMA_ALIGNMENT)
		* (num_ref + 1);
	h265e_colrcbuf_size = (((width_lcu_num + 7) >> 3) *
		16 * 2 * height_lcu_num);
	if (num_vpp_pipes > 1)
		h265e_colrcbuf_size = ALIGN(h265e_colrcbuf_size,
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes;
	h265e_colrcbuf_size = ALIGN(h265e_colrcbuf_size,
		VENUS_DMA_ALIGNMENT) * HFI_MAX_COL_FRAME;
	h265e_framerc_bufsize = (is_h265) ? (256 + 16 *
		(14 + (((height_coded >> 5) + 7) >> 3))) :
		(256 + 16 * (14 + (((height_coded >> 4) + 7) >> 3)));
	h265e_framerc_bufsize *= 6;   /* multiply by max numtilescol*/
	if (num_vpp_pipes > 1)
		h265e_framerc_bufsize = ALIGN(h265e_framerc_bufsize,
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes;

	h265e_framerc_bufsize = ALIGN(h265e_framerc_bufsize,
		BUFFER_ALIGNMENT_SIZE(512)) * HFI_MAX_COL_FRAME;
	h265e_lcubitcnt_bufsize = (256 + 4 * frame_num_lcu);
	h265e_lcubitcnt_bufsize = ALIGN(h265e_lcubitcnt_bufsize,
		VENUS_DMA_ALIGNMENT);
	h265e_lcubitmap_bufsize = 256 + (frame_num_lcu >> 3);
	h265e_lcubitmap_bufsize = ALIGN(h265e_lcubitmap_bufsize,
		VENUS_DMA_ALIGNMENT);
	line_buf_sde_size = 256 + 16 * (width_coded >> 4);
	line_buf_sde_size = ALIGN(line_buf_sde_size, VENUS_DMA_ALIGNMENT);
	if ((width_coded * height_coded) > (4096 * 2160))
		se_stats_bufsize = 0;
	else if ((width_coded * height_coded) > (1920 * 1088))
		se_stats_bufsize = (40 * 4 * frame_num_lcu + 256 + 256);
	else
		se_stats_bufsize = (1024 * frame_num_lcu + 256 + 256);

	se_stats_bufsize = ALIGN(se_stats_bufsize, VENUS_DMA_ALIGNMENT) * 2;
	bse_slice_cmd_buffer_size = ((((8192 << 2) + 7) & (~7)) * 6);
	bse_reg_buffer_size = ((((512 << 3) + 7) & (~7)) * 4);
	vpp_reg_buffer_size = ((((HFI_VENUS_VPPSG_MAX_REGISTERS << 3) + 31) &
		(~31)) * 10);
	lambda_lut_size = (256 * 11);
	override_buffer_size = 16 * ((num_LCUMB + 7) >> 3);
	override_buffer_size = ALIGN(override_buffer_size,
		VENUS_DMA_ALIGNMENT) * 2;
	ir_buffer_size = (((frame_num_lcu << 1) + 7) & (~7)) * 3;
	vpss_lineBufferSize_1 = ((((8192) >> 2) << 5) * num_vpp_pipes) + 64;
	vpss_line_buf = (((((max(width_coded, height_coded) + 3) >> 2) << 5) + 256) * 16) +
		vpss_lineBufferSize_1;
	topline_bufsize_fe_1stg_sao = (16 * (width_coded >> 5));
	topline_bufsize_fe_1stg_sao = ALIGN(topline_bufsize_fe_1stg_sao,
		VENUS_DMA_ALIGNMENT);
	size = line_buf_ctrl_size + line_buf_data_size +
		line_buf_ctrl_size_buffid2 + leftline_buf_ctrl_size +
		vpss_line_buf + col_mv_buf_size + topline_buf_ctrl_size_FE +
		leftline_buf_ctrl_size_FE + line_buf_recon_pix_size +
		leftline_buf_recon_pix_size + leftline_buf_meta_recony +
		linebuf_meta_recon_uv + h265e_colrcbuf_size +
		h265e_framerc_bufsize + h265e_lcubitcnt_bufsize +
		h265e_lcubitmap_bufsize + line_buf_sde_size +
		topline_bufsize_fe_1stg_sao + override_buffer_size +
		bse_reg_buffer_size + vpp_reg_buffer_size +
		sps_pps_slice_hdr + slice_cmd_buffer_size +
		bse_slice_cmd_buffer_size + ir_buffer_size + slice_info_bufsize
		+ lambda_lut_size + se_stats_bufsize + temp_scratch_mv_bufsize
		+ output_mv_bufsize + 1024;
	return size;
}

static u32 scratch1_size(struct venus_inst *inst)
{
	u32 width = inst->width;
	u32 height = inst->height;
	u32 num_vpp_pipes = 4; /* sm8250 num_vpp_pipes */
	u32 min_buf_count = 8; /* TODO */
	bool split_mode_enabled = true; /* TODO */
	u32 size;

	if (inst->session_type == VIDC_SESSION_TYPE_ENC) {
		u32 lcu_size = 16;
		u32 num_ref = 1; /* TODO */
		bool ten_bit = false;
		bool is_h265 = false;
		if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC) {
			lcu_size = 32;
			is_h265 = true;
		} else if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_VP8) {
			num_vpp_pipes = 1;
		}
		return calculate_enc_scratch1_size(width, height, lcu_size,
				num_ref, ten_bit, num_vpp_pipes, is_h265);
	}

	switch (inst->fmt_out->pixfmt) {
	case V4L2_PIX_FMT_H264:
		size = hfi_iris2_h264d_comv_size(width, height, min_buf_count) +
			hfi_iris2_h264d_non_comv_size(width, height, num_vpp_pipes);
		break;
	case V4L2_PIX_FMT_HEVC:
		size = hfi_iris2_h265d_comv_size(width, height, min_buf_count) +
			hfi_iris2_h265d_non_comv_size(width, height, num_vpp_pipes) +
			HDR10_HIST_EXTRADATA_SIZE;
		break;
	case V4L2_PIX_FMT_VP8:
		size = hfi_iris2_vp8d_comv_size(width, height) +
			ALIGN(SIZE_VPXD_LB_FE_LEFT_CTRL(width, height), 256) * num_vpp_pipes +
			ALIGN(SIZE_VPXD_LB_SE_LEFT_CTRL(width, height), 256) * num_vpp_pipes +
			ALIGN(SIZE_VP8D_LB_VSP_TOP(width, height), 256) +
			ALIGN(SIZE_VPXD_LB_FE_TOP_CTRL(width, height), 256) +
			ALIGN(SIZE_VPXD_LB_RECON_DMA_METADATA_WR(width, height), 256) * 2 +
			ALIGN(SIZE_VPXD_LB_SE_TOP_CTRL(width, height), 256) +
			ALIGN(SIZE_VP8D_LB_PE_TOP_DATA(width, height), 256) +
			ALIGN(SIZE_VP8D_LB_FE_TOP_DATA(width, height), 256);
		break;
	case V4L2_PIX_FMT_VP9:
		size = ALIGN(SIZE_VPXD_LB_FE_LEFT_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(SIZE_VPXD_LB_SE_LEFT_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(SIZE_VP9D_LB_VSP_TOP(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_VPXD_LB_FE_TOP_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) +
		2 * ALIGN(SIZE_VPXD_LB_RECON_DMA_METADATA_WR(width, height),
				VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_VPXD_LB_SE_TOP_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_VP9D_LB_PE_TOP_DATA(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_VP9D_LB_FE_TOP_DATA(width, height),
			VENUS_DMA_ALIGNMENT) +
		HDR10_HIST_EXTRADATA_SIZE;
		break;
	case V4L2_PIX_FMT_MPEG2:
		size = ALIGN(SIZE_VPXD_LB_FE_LEFT_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(SIZE_VPXD_LB_SE_LEFT_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) * num_vpp_pipes +
		ALIGN(SIZE_VP8D_LB_VSP_TOP(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_VPXD_LB_FE_TOP_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) +
		2 * ALIGN(SIZE_VPXD_LB_RECON_DMA_METADATA_WR(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_VPXD_LB_SE_TOP_CTRL(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_VP8D_LB_PE_TOP_DATA(width, height),
			VENUS_DMA_ALIGNMENT) +
		ALIGN(SIZE_VP8D_LB_FE_TOP_DATA(width, height),
			VENUS_DMA_ALIGNMENT);
		break;
	default:
		unreachable();
	}

	if (split_mode_enabled)
		size += size_vpss_lb(width, height, num_vpp_pipes);

	return size;
}

#define BUFFER_ALIGNMENT_4096_BYTES 4096

#define HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_WIDTH 32
#define HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_HEIGHT 8
#define HFI_COLOR_FORMAT_YUV420_NV12_UBWC_UV_TILE_WIDTH 16
#define HFI_COLOR_FORMAT_YUV420_NV12_UBWC_UV_TILE_HEIGHT 8
#define HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_WIDTH 48
#define HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_HEIGHT 4

#define VENUS_METADATA_STRIDE_MULTIPLE     64
#define VENUS_METADATA_HEIGHT_MULTIPLE     16

static inline u32 hfi_ubwc_calc_metadata_plane_stride(u32 width,
	u32 metadata_stride_multi, u32 tile_width_pels)
{
	return ALIGN(((width + (tile_width_pels - 1)) / tile_width_pels),
		metadata_stride_multi);
}

static inline u32 hfi_ubwc_metadata_plane_bufheight(u32 height,
	u32 metadata_height_multi, u32 tile_height_pels)
{
	return ALIGN(((height + (tile_height_pels - 1)) / tile_height_pels),
		metadata_height_multi);
}

static inline u32 hfi_ubwc_metadata_plane_buffer_size(u32 metadata_stride,
	u32 metadata_buf_height)
{
	return ALIGN(metadata_stride * metadata_buf_height,
		BUFFER_ALIGNMENT_4096_BYTES);
}

static inline u32 hfi_ubwc_uv_metadata_plane_stride(u32 width,
	u32 metadata_stride_multi, u32 tile_width_pels)
{
	return ALIGN(((((width + 1) >> 1) + (tile_width_pels - 1)) /
		tile_width_pels), metadata_stride_multi);
}

static inline u32 hfi_ubwc_uv_metadata_plane_bufheight(u32 height,
	u32 metadata_height_multi, u32 tile_height_pels)
{
	return ALIGN(((((height + 1) >> 1) + (tile_height_pels - 1)) /
		tile_height_pels), metadata_height_multi);
}

static inline u32 calculate_enc_scratch2_size(
	u32 width, u32 height, u32 num_ref, bool ten_bit)
{
	u32 aligned_width, aligned_height, chroma_height, ref_buf_height;
	u32 luma_size, chroma_size;
	u32 metadata_stride, meta_buf_height, meta_size_y, meta_size_c;
	u32 ref_luma_stride_bytes, ref_chroma_height_bytes;
	u32 ref_buf_size = 0, ref_stride;
	u32 size;

	if (!ten_bit) {
		aligned_height = ALIGN(height, HFI_VENUS_HEIGHT_ALIGNMENT);
		chroma_height = height >> 1;
		chroma_height = ALIGN(chroma_height,
			HFI_VENUS_HEIGHT_ALIGNMENT);
		aligned_width = ALIGN(width, HFI_VENUS_WIDTH_ALIGNMENT);
		metadata_stride = hfi_ubwc_calc_metadata_plane_stride(width,
			64, HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_WIDTH);
		meta_buf_height = hfi_ubwc_metadata_plane_bufheight(height,
			16, HFI_COLOR_FORMAT_YUV420_NV12_UBWC_Y_TILE_HEIGHT);
		meta_size_y = hfi_ubwc_metadata_plane_buffer_size(
			metadata_stride, meta_buf_height);
		meta_size_c = hfi_ubwc_metadata_plane_buffer_size(
			metadata_stride, meta_buf_height);
		size = (aligned_height + chroma_height) * aligned_width +
			meta_size_y + meta_size_c;
		size = (size * (num_ref+3)) + 4096;
	} else {
		ref_buf_height = (height + (HFI_VENUS_HEIGHT_ALIGNMENT - 1))
			& (~(HFI_VENUS_HEIGHT_ALIGNMENT - 1));
		ref_luma_stride_bytes = ((width + SYSTEM_LAL_TILE10 - 1) /
			SYSTEM_LAL_TILE10) * SYSTEM_LAL_TILE10;
		ref_stride = 4 * (ref_luma_stride_bytes / 3);
		ref_stride = (ref_stride + (BUFFER_ALIGNMENT_SIZE(128) - 1)) &
			(~(BUFFER_ALIGNMENT_SIZE(128) - 1));
		luma_size = ref_buf_height * ref_stride;
		ref_chroma_height_bytes = (((height + 1) >> 1) +
			(BUFFER_ALIGNMENT_SIZE(32) - 1)) &
			(~(BUFFER_ALIGNMENT_SIZE(32) - 1));
		chroma_size = ref_stride * ref_chroma_height_bytes;
		luma_size = (luma_size + (BUFFER_ALIGNMENT_4096_BYTES - 1)) &
			(~(BUFFER_ALIGNMENT_4096_BYTES - 1));
		chroma_size = (chroma_size +
			(BUFFER_ALIGNMENT_4096_BYTES - 1)) &
			(~(BUFFER_ALIGNMENT_4096_BYTES - 1));
		ref_buf_size = luma_size + chroma_size;
		metadata_stride = hfi_ubwc_calc_metadata_plane_stride(
			width,
			VENUS_METADATA_STRIDE_MULTIPLE,
			HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_WIDTH);
		meta_buf_height = hfi_ubwc_metadata_plane_bufheight(
			height,
			VENUS_METADATA_HEIGHT_MULTIPLE,
			HFI_COLOR_FORMAT_YUV420_TP10_UBWC_Y_TILE_HEIGHT);
		meta_size_y = hfi_ubwc_metadata_plane_buffer_size(
			metadata_stride, meta_buf_height);
		meta_size_c = hfi_ubwc_metadata_plane_buffer_size(
			metadata_stride, meta_buf_height);
		size = ref_buf_size + meta_size_y + meta_size_c;
		size = (size * (num_ref+3)) + 4096;
	}
	return size;
}

static u32 scratch2_size(struct venus_inst *inst)
{
	u32 num_ref = 1; /* TODO */
	bool ten_bit = false;

	if (inst->session_type != VIDC_SESSION_TYPE_ENC)
		return 0;

	return calculate_enc_scratch2_size(inst->width, inst->height, num_ref, ten_bit);
}

static u32 persist1_size(struct venus_inst *inst)
{
	if (inst->session_type == VIDC_SESSION_TYPE_ENC)
		return 0;

	// TODO:align VENUS_DMA_ALIGNMENT
	switch (inst->fmt_out->pixfmt) {
	case V4L2_PIX_FMT_H264:
		return ALIGN(SIZE_SLIST_BUF_H264 * NUM_SLIST_BUF_H264, 256);
	case V4L2_PIX_FMT_HEVC:
		return ALIGN(SIZE_SLIST_BUF_H265 * NUM_SLIST_BUF_H265 + H265_NUM_TILE * sizeof(u32), 256);
	case V4L2_PIX_FMT_VP8:
		return ALIGN(VP8_NUM_PROBABILITY_TABLE_BUF * VP8_PROB_TABLE_SIZE, 256);
	case V4L2_PIX_FMT_VP9:
		return ALIGN(VP9_NUM_PROBABILITY_TABLE_BUF * VP9_PROB_TABLE_SIZE, 256) +
		       ALIGN(HFI_IRIS2_VP9D_COMV_SIZE, 256) +
		       ALIGN(MAX_SUPERFRAME_HEADER_LEN, 256) +
		       ALIGN(VP9_UDC_HEADER_BUF_SIZE, 256) +
		       ALIGN(VP9_NUM_FRAME_INFO_BUF * CCE_TILE_OFFSET_SIZE, 256);
	case V4L2_PIX_FMT_MPEG2:
		return QMATRIX_SIZE + MP2D_QPDUMP_SIZE;
	default:
		unreachable();
	}
}

/* minimum number of input buffers */
#define MIN_INPUT_BUFFERS 4
/* Max number of PERF eligible sessions */
#define MAX_PERF_ELIGIBLE_SESSIONS 4

/* Decoder buffer count macros */
/* total input buffers in case of decoder batch */
#define BATCH_DEC_TOTAL_INPUT_BUFFERS 6

/* total input buffers for decoder HFR usecase (fps capability > 480) */
#define MAX_HFR_DEC_TOTAL_INPUT_BUFFERS 12

/* total input buffers for decoder HFR usecase (fps capablity <= 480) */
#define MIN_HFR_DEC_TOTAL_INPUT_BUFFERS 8


/* extra output buffers in case of decoder batch */
#define BATCH_DEC_EXTRA_OUTPUT_BUFFERS 6

/* Encoder buffer count macros */
/* total input buffers for encoder HFR usecase */
#define HFR_ENC_TOTAL_INPUT_BUFFERS 16

/* extra output buffers for encoder HFR usecase */
#define HFR_ENC_TOTAL_OUTPUT_BUFFERS 12

/* extra output buffers for encoder HEIF usecase */
#define HEIF_ENC_TOTAL_OUTPUT_BUFFERS 12

static u32 input_count(struct venus_inst *inst)
{
	bool is_thumbnail_session = false; /* TODO */

	if (is_thumbnail_session)
		return 1;

	/* TODO: extra buffers for realtime session - msm_vidc_get_extra_input_buff_count() */
	return 4;
}

static u32 output_count(struct venus_inst *inst)
{
	bool is_thumbnail_session = false; /* TODO */

	if (is_thumbnail_session)
		return inst->fmt_out->pixfmt == V4L2_PIX_FMT_VP9 ? 8 : 1;

	if (inst->session_type == VIDC_SESSION_TYPE_ENC)
		return 4;

	switch (inst->fmt_out->pixfmt) {
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_HEVC:
		return 8;
	case V4L2_PIX_FMT_VP8:
	case V4L2_PIX_FMT_MPEG2:
		return 6;
	case V4L2_PIX_FMT_VP9:
		return 9;
	default:
		unreachable();
	}

	/* TODO: extra buffers for realtime session - msm_vidc_get_extra_output_buff_count() */
}

int venus_helper_get_bufreq_iris2(struct venus_inst *inst, u32 type, struct hfi_buffer_requirements *req)
{
	switch (type) {
	case HFI_BUFFER_INPUT:
		req->count_actual = req->hold_count = req->count_min = input_count(inst);
		return 0;
	case HFI_BUFFER_OUTPUT:
	case HFI_BUFFER_OUTPUT2:
		req->count_actual = req->hold_count = req->count_min = output_count(inst);
		return 0;
	case HFI_BUFFER_INTERNAL_SCRATCH(HFI_VERSION_4XX):
		req->size = scratch_size(inst);
		break;
	case HFI_BUFFER_INTERNAL_SCRATCH_1(HFI_VERSION_4XX):
		req->size = scratch1_size(inst);
		break;
	case HFI_BUFFER_INTERNAL_SCRATCH_2(HFI_VERSION_4XX):
		req->size = scratch2_size(inst);
		break;
	case HFI_BUFFER_INTERNAL_PERSIST:
		if (inst->session_type == VIDC_SESSION_TYPE_ENC)
			req->size = HFI_IRIS2_ENC_PERSIST_SIZE;
		else
			req->size = 0;
		break;
	case HFI_BUFFER_INTERNAL_PERSIST_1:
		req->size = persist1_size(inst);
		break;
	}

	req->size = ALIGN(req->size, SZ_4K);
	req->type = type;
	req->count_actual = 1;

	return 0;
}
