#ifndef __CMDQ_ENGINE_COMMON_H__
#define __CMDQ_ENGINE_COMMON_H__

typedef enum CMDQ_ENG_ENUM {
	/* ISP */
	CMDQ_ENG_WPEI = 0,
	CMDQ_ENG_WPEO,			/* 1 */
	CMDQ_ENG_ISP_IMGI,		/* 2 */
	CMDQ_ENG_ISP_IMGO,		/* 3 */
	CMDQ_ENG_ISP_IMG2O,		/* 4 */

	/* IPU */
	CMDQ_ENG_IPUI,			/* 5 */
	CMDQ_ENG_IPUO,			/* 6 */

	/* MDP */
	CMDQ_ENG_MDP_CAMIN,		/* 7 */
	CMDQ_ENG_MDP_RDMA0,		/* 8 */
	CMDQ_ENG_MDP_RDMA1,		/* 9 */
	CMDQ_ENG_MDP_IPUIN,		/* 10 */
	CMDQ_ENG_MDP_RSZ0,		/* 11 */
	CMDQ_ENG_MDP_RSZ1,		/* 12 */
	CMDQ_ENG_MDP_RSZ2,		/* 13 */
	CMDQ_ENG_MDP_TDSHP0,		/* 14 */
	CMDQ_ENG_MDP_TDSHP1,		/* 15 */
	CMDQ_ENG_MDP_COLOR0,		/* 16 */
	CMDQ_ENG_MDP_MOUT0,		/* 17 */
	CMDQ_ENG_MDP_MOUT1,		/* 18 */
	CMDQ_ENG_MDP_PATH0_SOUT,	/* 19 */
	CMDQ_ENG_MDP_PATH1_SOUT,	/* 20 */
	CMDQ_ENG_MDP_WROT0,		/* 21 */
	CMDQ_ENG_MDP_WROT1,		/* 22 */
	CMDQ_ENG_MDP_WDMA,		/* 23 */

	/* JPEG & VENC */
	CMDQ_ENG_JPEG_ENC,		/* 24 */
	CMDQ_ENG_VIDEO_ENC,		/* 25 */
	CMDQ_ENG_JPEG_DEC,		/* 26 */
	CMDQ_ENG_JPEG_REMDC,		/* 27 */

	/* DISP */
	CMDQ_ENG_DISP_UFOE,		/* 28 */
	CMDQ_ENG_DISP_AAL,		/* 29 */
	CMDQ_ENG_DISP_COLOR0,		/* 30 */
	CMDQ_ENG_DISP_COLOR1,		/* 31 */
	CMDQ_ENG_DISP_RDMA0,		/* 32 */
	CMDQ_ENG_DISP_RDMA1,		/* 33 */
	CMDQ_ENG_DISP_RDMA2,		/* 34 */
	CMDQ_ENG_DISP_WDMA0,		/* 35 */
	CMDQ_ENG_DISP_WDMA1,		/* 36 */
	CMDQ_ENG_DISP_OVL0,		/* 37 */
	CMDQ_ENG_DISP_OVL1,		/* 38 */
	CMDQ_ENG_DISP_OVL2,		/* 39 */
	CMDQ_ENG_DISP_2L_OVL0,		/* 40 */
	CMDQ_ENG_DISP_2L_OVL1,		/* 41 */
	CMDQ_ENG_DISP_2L_OVL2,		/* 42 */
	CMDQ_ENG_DISP_GAMMA,		/* 43 */
	CMDQ_ENG_DISP_MERGE,		/* 44 */
	CMDQ_ENG_DISP_SPLIT0,		/* 45 */
	CMDQ_ENG_DISP_SPLIT1,		/* 46 */
	CMDQ_ENG_DISP_DSI0_VDO,		/* 47 */
	CMDQ_ENG_DISP_DSI1_VDO,		/* 48 */
	CMDQ_ENG_DISP_DSI0_CMD,		/* 49 */
	CMDQ_ENG_DISP_DSI1_CMD,		/* 50 */
	CMDQ_ENG_DISP_DSI0,		/* 51 */
	CMDQ_ENG_DISP_DSI1,		/* 52 */
	CMDQ_ENG_DISP_DPI,		/* 53 */

	/* DPE */
	CMDQ_ENG_DPE,			/* 54 */

	/* temp: CMDQ internal usage */
	CMDQ_ENG_CMDQ,			/* 55 */
	CMDQ_ENG_DISP_MUTEX,		/* 56 */
	CMDQ_ENG_MMSYS_CONFIG,		/* 57 */

	CMDQ_MAX_ENGINE_COUNT		/* ALWAYS keep at the end */
} CMDQ_ENG_ENUM;

#endif				/* __CMDQ_ENGINE_COMMON_H__ */
