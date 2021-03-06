/*
 * Copyright (c) 2006-2021 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include "libcxgb4.h"

#ifdef STATS
struct c4iw_stats c4iw_stats;
#endif

static void copy_wr_to_txq(struct t4_txq *t, struct txq_desc *d, u8 len16)
{
	u64 *src, *dst;

	src = (u64 *)d;
	dst = (u64 *)((u8 *)t->queue + t->wq_pidx * T4_EQ_ENTRY_SIZE);
	if (t4_txq_onchip(t)) {
		len16 = align(len16, 4);

		/* In onchip mode the copy below will be made to WC memory and
		 * could trigger DMA. In offchip mode the copy below only
		 * queues the WQE, DMA cannot start until t4_ring_sq_db
		 * happens */
		mmio_wc_start();
	}
	while (len16) {
		*dst++ = *src++;
		if (dst == (u64 *)&t->queue[t->size])
			dst = (u64 *)t->queue;
		*dst++ = *src++;
		if (dst == (u64 *)&t->queue[t->size])
			dst = (u64 *)t->queue;
		len16--;
	}
	if (t4_txq_onchip(t))
		mmio_flush_writes();
}

static void copy_wr_to_sq(struct t4_wq *wq, union t4_wr *wqe, u8 len16)
{
	u64 *src, *dst;

	src = (u64 *)wqe;
	dst = (u64 *)((u8 *)wq->sq.queue + wq->sq.wq_pidx * T4_EQ_ENTRY_SIZE);
	if (t4_sq_onchip(wq)) {
		len16 = align(len16, 4);

		/* In onchip mode the copy below will be made to WC memory and
		 * could trigger DMA. In offchip mode the copy below only
		 * queues the WQE, DMA cannot start until t4_ring_sq_db
		 * happens */
		mmio_wc_start();
	}
	while (len16) {
		*dst++ = *src++;
		if (dst == (u64 *)&wq->sq.queue[wq->sq.size])
			dst = (u64 *)wq->sq.queue;
		*dst++ = *src++;
		if (dst == (u64 *)&wq->sq.queue[wq->sq.size])
			dst = (u64 *)wq->sq.queue;
		len16--;
	}
	if (t4_sq_onchip(wq))
		mmio_flush_writes();
}

static void copy_wr_to_rq(struct t4_wq *wq, union t4_recv_wr *wqe, u8 len16)
{
	u64 *src, *dst;

	src = (u64 *)wqe;
	dst = (u64 *)((u8 *)wq->rq.queue + wq->rq.wq_pidx * T4_EQ_ENTRY_SIZE);
	while (len16) {
		*dst++ = *src++;
		if (dst >= (u64 *)&wq->rq.queue[wq->rq.size])
			dst = (u64 *)wq->rq.queue;
		*dst++ = *src++;
		if (dst >= (u64 *)&wq->rq.queue[wq->rq.size])
			dst = (u64 *)wq->rq.queue;
		len16--;
	}
}

void c4iw_copy_wr_to_srq(struct t4_srq *srq, union t4_recv_wr *wqe, u8 len16)
{
	u64 *src, *dst;

	src = (u64 *)wqe;
	dst = (u64 *)((u8 *)srq->queue + srq->wq_pidx * T4_EQ_ENTRY_SIZE);
	while (len16) {
		*dst++ = *src++;
		if (dst >= (u64 *)&srq->queue[srq->size])
			dst = (u64 *)srq->queue;
		*dst++ = *src++;
		if (dst >= (u64 *)&srq->queue[srq->size])
			dst = (u64 *)srq->queue;
		len16--;
	}
}

static int build_immd(struct t4_sq *sq, struct fw_ri_immd *immdp,
		      struct ibv_send_wr *wr, int max, u32 *plenp)
{
	u8 *dstp, *srcp;
	u32 plen = 0;
	int i;
	int len;

	dstp = (u8 *)immdp->data;
	for (i = 0; i < wr->num_sge; i++) {
		if ((plen + wr->sg_list[i].length) > max)
			return -EMSGSIZE;
		srcp = (u8 *)(unsigned long)wr->sg_list[i].addr;
		plen += wr->sg_list[i].length;
		len = wr->sg_list[i].length;
		memcpy(dstp, srcp, len);
		dstp += len;
		srcp += len;
	}
	len = ROUND_UP(plen + 8, 16) - (plen + 8);
	if (len)
		memset(dstp, 0, len);
	immdp->op = FW_RI_DATA_IMMD;
	immdp->r1 = 0;
	immdp->r2 = 0;
	immdp->immdlen = htobe32(plen);
	*plenp = plen;
	return 0;
}

static int build_isgl(__be64 *queue_start, __be64 *queue_end,
		      struct fw_ri_isgl *isglp, struct ibv_sge *sg_list,
		      int num_sge, u32 *plenp)
{
	int i;
	u32 plen = 0;
	__be64 *flitp;

	if ((__be64 *)isglp == queue_end)
		isglp = (struct fw_ri_isgl *)queue_start;

	flitp = (__be64 *)isglp->sge;
	for (i = 0; i < num_sge; i++) {
		if ((plen + sg_list[i].length) < plen)
			return -EMSGSIZE;
		plen += sg_list[i].length;
		*flitp = htobe64(((u64)sg_list[i].lkey << 32) |
				   sg_list[i].length);
		if (++flitp == queue_end)
			flitp = queue_start;
		*flitp = htobe64(sg_list[i].addr);
		if (++flitp == queue_end)
			flitp = queue_start;
	}
	*flitp = 0;
	isglp->op = FW_RI_DATA_ISGL;
	isglp->r1 = 0;
	isglp->nsge = htobe16(num_sge);
	isglp->r2 = 0;
	if (plenp)
		*plenp = plen;
	return 0;
}

static int build_rdma_send(struct t4_sq *sq, union t4_wr *wqe,
			   struct ibv_send_wr *wr, u8 *len16)
{
	u32 plen;
	int size;
	int ret;

	if (wr->num_sge > T4_MAX_SEND_SGE)
		return -EINVAL;
	switch (wr->opcode) {
	case IBV_WR_SEND:
		if (wr->send_flags & IBV_SEND_SOLICITED)
			wqe->send.sendop_pkd = htobe32(
				V_FW_RI_SEND_WR_SENDOP(FW_RI_SEND_WITH_SE));
		else
			wqe->send.sendop_pkd = htobe32(
				V_FW_RI_SEND_WR_SENDOP(FW_RI_SEND));
		wqe->send.stag_inv = 0;
		break;
	case IBV_WR_SEND_WITH_INV:
		if (wr->send_flags & IBV_SEND_SOLICITED)
			wqe->send.sendop_pkd = htobe32(
				V_FW_RI_SEND_WR_SENDOP(FW_RI_SEND_WITH_SE_INV));
		else
			wqe->send.sendop_pkd = htobe32(
				V_FW_RI_SEND_WR_SENDOP(FW_RI_SEND_WITH_INV));
		wqe->send.stag_inv = htobe32(wr->invalidate_rkey);
		break;
	default:
		return -EINVAL;
	}
	wqe->send.r3 = 0;
	wqe->send.r4 = 0;

	plen = 0;
	if (wr->num_sge) {
		if (wr->send_flags & IBV_SEND_INLINE) {
			ret = build_immd(sq, wqe->send.u.immd_src, wr,
					 T4_MAX_SEND_INLINE, &plen);
			if (ret)
				return ret;
			size = sizeof wqe->send + sizeof(struct fw_ri_immd) +
			       plen;
		} else {
			ret = build_isgl((__be64 *)sq->queue,
					 (__be64 *)&sq->queue[sq->size],
					 wqe->send.u.isgl_src,
					 wr->sg_list, wr->num_sge, &plen);
			if (ret)
				return ret;
			size = sizeof wqe->send + sizeof(struct fw_ri_isgl) +
			       wr->num_sge * sizeof (struct fw_ri_sge);
		}
	} else {
		wqe->send.u.immd_src[0].op = FW_RI_DATA_IMMD;
		wqe->send.u.immd_src[0].r1 = 0;
		wqe->send.u.immd_src[0].r2 = 0;
		wqe->send.u.immd_src[0].immdlen = 0;
		size = sizeof wqe->send + sizeof(struct fw_ri_immd);
		plen = 0;
	}
	*len16 = DIV_ROUND_UP(size, 16);
	wqe->send.plen = htobe32(plen);
	return 0;
}

static int build_rdma_write(struct t4_sq *sq, union t4_wr *wqe,
			    struct ibv_send_wr *wr, u8 *len16)
{
	u32 plen;
	int size;
	int ret;

	if (wr->num_sge > T4_MAX_SEND_SGE)
		return -EINVAL;
	if (wr->opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
		wqe->write.immd_data = wr->imm_data;
	else
		wqe->write.immd_data = 0;
	wqe->write.stag_sink = htobe32(wr->wr.rdma.rkey);
	wqe->write.to_sink = htobe64(wr->wr.rdma.remote_addr);
	if (wr->num_sge) {
		if (wr->send_flags & IBV_SEND_INLINE) {
			ret = build_immd(sq, wqe->write.u.immd_src, wr,
					 T4_MAX_WRITE_INLINE, &plen);
			if (ret)
				return ret;
			size = sizeof wqe->write + sizeof(struct fw_ri_immd) +
			       plen;
		} else {
			ret = build_isgl((__be64 *)sq->queue,
					 (__be64 *)&sq->queue[sq->size],
					 wqe->write.u.isgl_src,
					 wr->sg_list, wr->num_sge, &plen);
			if (ret)
				return ret;
			size = sizeof wqe->write + sizeof(struct fw_ri_isgl) +
			       wr->num_sge * sizeof (struct fw_ri_sge);
		}
	} else {
		wqe->write.u.immd_src[0].op = FW_RI_DATA_IMMD;
		wqe->write.u.immd_src[0].r1 = 0;
		wqe->write.u.immd_src[0].r2 = 0;
		wqe->write.u.immd_src[0].immdlen = 0;
		size = sizeof wqe->write + sizeof(struct fw_ri_immd);
		plen = 0;
	}
	*len16 = DIV_ROUND_UP(size, 16);
	wqe->write.plen = htobe32(plen);
	return 0;
}

static void build_immd_cmpl(struct t4_sq *sq, struct fw_ri_immd_cmpl *immdp,
			   struct ibv_send_wr *wr)
{
	memcpy((u8 *)immdp->data, (u8 *)(uintptr_t)wr->sg_list->addr, 16);
	memset(immdp->r1, 0, 6);
	immdp->op = FW_RI_DATA_IMMD;
	immdp->immdlen = 16;
	return;
}

#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

static void build_rdma_write_cmpl(struct t4_sq *sq,
				  struct fw_ri_rdma_write_cmpl_wr *wcwr,
				  struct ibv_send_wr *wr, u8 *len16)
{
	u32 plen;
	int size;

	/*
	 * This code assumes the struct fields preceeding the write isgl
	 * fit in one 64B WR slot.  This is because the WQE is built
	 * directly in the dma queue, and wrapping is only handled
	 * by the code buildling sgls.  IE the "fixed part" of the wr
	 * structs must all fit in 64B.  The WQE build code should probably be
	 * redesigned to avoid this restriction, but for now just add
	 * the BUILD_BUG_ON() to catch if this WQE struct gets too big.
	 */
	BUILD_BUG_ON(offsetof(struct fw_ri_rdma_write_cmpl_wr, u) > 64);

	wcwr->stag_sink = htobe32(wr->wr.rdma.rkey);
	wcwr->to_sink = htobe64(wr->wr.rdma.remote_addr);
	if (wr->next->opcode == IBV_WR_SEND)
		wcwr->stag_inv = 0;
	else
		wcwr->stag_inv = htobe32(wr->next->invalidate_rkey);
	wcwr->r2 = 0;
	wcwr->r3 = 0;

 	/* SEND_INV SGL */
	if (wr->next->send_flags & IBV_SEND_INLINE)
		build_immd_cmpl(sq, &wcwr->u_cmpl.immd_src, wr->next);
	else
		build_isgl((__be64 *)sq->queue, (__be64 *)&sq->queue[sq->size],
			   &wcwr->u_cmpl.isgl_src, wr->next->sg_list, 1, NULL);

	/* WRITE SGL */
	build_isgl((__be64 *)sq->queue, (__be64 *)&sq->queue[sq->size],
		   wcwr->u.isgl_src, wr->sg_list, wr->num_sge, &plen);

	size = sizeof *wcwr + sizeof(struct fw_ri_isgl) +
	       wr->num_sge * sizeof(struct fw_ri_sge);
	wcwr->plen = htobe32(plen);
	*len16 = DIV_ROUND_UP(size, 16);

	return;
}

static int build_rdma_read(union t4_wr *wqe, struct ibv_send_wr *wr, u8 *len16)
{
	if (wr->num_sge > 1)
		return -EINVAL;
	if (wr->num_sge && wr->sg_list[0].length) {
		wqe->read.stag_src = htobe32(wr->wr.rdma.rkey);
		wqe->read.to_src_hi = htobe32((u32)(wr->wr.rdma.remote_addr >>32));
		wqe->read.to_src_lo = htobe32((u32)wr->wr.rdma.remote_addr);
		wqe->read.stag_sink = htobe32(wr->sg_list[0].lkey);
		wqe->read.plen = htobe32(wr->sg_list[0].length);
		wqe->read.to_sink_hi = htobe32((u32)(wr->sg_list[0].addr >> 32));
		wqe->read.to_sink_lo = htobe32((u32)(wr->sg_list[0].addr));
	} else {
		wqe->read.stag_src = htobe32(2);
		wqe->read.to_src_hi = 0;
		wqe->read.to_src_lo = 0;
		wqe->read.stag_sink = htobe32(2);
		wqe->read.plen = 0;
		wqe->read.to_sink_hi = 0;
		wqe->read.to_sink_lo = 0;
	}
	wqe->read.r2 = 0;
	wqe->read.r5 = 0;
	*len16 = DIV_ROUND_UP(sizeof wqe->read, 16);
	return 0;
}

static int build_rdma_recv(struct t4_rq *rq, union t4_recv_wr *wqe,
			   struct ibv_recv_wr *wr, u8 *len16)
{
	int ret;

	ret = build_isgl((__be64 *)rq->queue, (__be64 *)&rq->queue[rq->size],
			 &wqe->recv.isgl, wr->sg_list, wr->num_sge, NULL);
	if (ret)
		return ret;
	*len16 = DIV_ROUND_UP(sizeof wqe->recv +
			      wr->num_sge * sizeof(struct fw_ri_sge), 16);
	return 0;
}

static int build_srq_recv(union t4_recv_wr *wqe, struct ibv_recv_wr *wr,
			  u8 *len16)
{
	int ret;

	ret = build_isgl((__be64 *)wqe, (__be64 *)(wqe + 1),
			 &wqe->recv.isgl, wr->sg_list, wr->num_sge, NULL);
	if (ret)
		return ret;
	*len16 = DIV_ROUND_UP(sizeof wqe->recv +
			      wr->num_sge * sizeof(struct fw_ri_sge), 16);
	return 0;
}

static void ring_kernel_db(struct c4iw_qp *qhp, u32 qid, u16 idx)
{
	struct ibv_modify_qp cmd;
	struct ibv_qp_attr attr;
	int mask;
	int __attribute__((unused)) ret;

	/* FIXME: Why do we need this barrier if the kernel is going to
	   trigger the DMA? */
	udma_to_device_barrier();
	if (qid == qhp->wq.sq.qid) {
		attr.sq_psn = idx;
		mask = IBV_QP_SQ_PSN;
	} else  {
		attr.rq_psn = idx;
		mask = IBV_QP_RQ_PSN;
	}
	ret = ibv_cmd_modify_qp(&qhp->ibv_qp, &attr, mask, &cmd, sizeof cmd);
	assert(!ret);
}

static void ring_kernel_txq_db(struct c4iw_raw_qp *rqp, u16 idx)
{
	struct ibv_modify_qp cmd;
	struct ibv_qp_attr attr;
	int mask;
	int  __attribute__((unused)) ret;

	/* FIXME: Why do we need this barrier if the kernel is going to
	   trigger the DMA? */
	udma_to_device_barrier();
	attr.sq_psn = idx;
	mask = IBV_QP_SQ_PSN;
	ret = ibv_cmd_modify_qp(&rqp->ibv_qp, &attr, mask, &cmd, sizeof cmd);
	assert(!ret);
}

static void ring_kernel_fl_db(struct c4iw_raw_qp *rqp, struct t4_raw_fl *f)
{
	struct ibv_modify_qp cmd;
	struct ibv_qp_attr attr;
	int mask;
	int  __attribute__((unused)) ret;

	if (f->pend_cred >= 8) {

		/* FIXME: Why do we need this barrier if the kernel is going to
		   trigger the DMA? */
		udma_to_device_barrier();
		attr.rq_psn = f->pend_cred / 8;
		mask = IBV_QP_RQ_PSN;
		ret = ibv_cmd_modify_qp(&rqp->ibv_qp, &attr, mask, &cmd,
					sizeof cmd);
		assert(!ret);
		f->pend_cred &= 7;
	}
}

static void ring_kernel_srq_db(struct c4iw_raw_srq *srq, struct t4_raw_fl *f)
{
	struct ibv_modify_srq cmd;
	struct ibv_srq_attr attr;
	int mask;
	int  __attribute__((unused)) ret;

	if (f->pend_cred >= 8) {

		/* FIXME: Why do we need this barrier if the kernel is going to
		   trigger the DMA? */
		udma_to_device_barrier();
		attr.max_sge |= f->pend_cred << 16;
		attr.srq_limit = f->pend_cred / 8;
		mask = IBV_SRQ_LIMIT;
		ret = ibv_cmd_modify_srq(&srq->ibv_srq, &attr, mask, &cmd,
					sizeof cmd);
		assert(!ret);
		f->pend_cred &= 7;
	}
}

static int ibv_to_fw_opcode(int ib_opcode)
{
	int opcode;

	switch (ib_opcode) {
	case IBV_WR_SEND_WITH_INV:
		opcode = FW_RI_SEND_WITH_INV;
		break;
	case IBV_WR_SEND:
		opcode = FW_RI_SEND;
		break;
	case IBV_WR_RDMA_WRITE:
		opcode = FW_RI_RDMA_WRITE;
		break;
	case IBV_WR_RDMA_WRITE_WITH_IMM:
		opcode = FW_RI_WRITE_IMMEDIATE;
		break;
	case IBV_WR_RDMA_READ:
		opcode = FW_RI_READ_REQ;
		break;
	default:
		opcode = -EINVAL;
	}
	return opcode;
}

static int complete_sq_drain_wr(struct c4iw_qp *qhp, struct ibv_send_wr *wr)
{
	struct t4_cqe cqe = {};
	struct c4iw_cq *schp;
	struct t4_cq *cq;
	int opcode;

	schp = to_c4iw_cq(qhp->ibv_qp.send_cq);
	cq = &schp->cq;

	opcode = ibv_to_fw_opcode(wr->opcode);
	if (opcode < 0)
		return opcode;

	PDBG("drain sq id %u\n", qhp->wq.sq.qid);
	cqe.u.rdma.u.drain_cookie = wr->wr_id;
	cqe.u.rdma.header = htobe32(V_CQE_STATUS(T4_ERR_SWFLUSH) |
				 V_CQE_OPCODE(opcode) |
				 V_CQE_TYPE(1) |
				 V_CQE_SWCQE(1) |
				 V_CQE_DRAIN(1) |
				 V_CQE_QPID(qhp->wq.sq.qid));

	pthread_spin_lock(&schp->lock);
	cqe.bits_type_ts = htobe64(V_CQE_GENBIT((u64)cq->gen));
	cq->sw_queue[cq->sw_pidx] = cqe;
	t4_swcq_produce(cq);
	pthread_spin_unlock(&schp->lock);

	t4_clear_cq_armed(&schp->cq);
	return 0;
}

static int complete_sq_drain_wrs(struct c4iw_qp *qhp, struct ibv_send_wr *wr,
				 struct ibv_send_wr **bad_wr)
{
	int ret = 0;

	while (wr) {
		ret = complete_sq_drain_wr(qhp, wr);
		if (ret) {
			*bad_wr = wr;
			break;
		}
		wr = wr->next;
	}
	return ret;
}

static void complete_rq_drain_wr(struct c4iw_qp *qhp, struct ibv_recv_wr *wr)
{
	struct t4_cqe cqe = {};
	struct c4iw_cq *rchp;
	struct t4_cq *cq;

	rchp = to_c4iw_cq(qhp->ibv_qp.recv_cq);
	cq = &rchp->cq;

	PDBG("drain rq id %u\n", qhp->wq.sq.qid);
	cqe.u.rdma.u.drain_cookie = wr->wr_id;
	cqe.u.rdma.header = htobe32(V_CQE_STATUS(T4_ERR_SWFLUSH) |
				 V_CQE_OPCODE(FW_RI_SEND) |
				 V_CQE_TYPE(0) |
				 V_CQE_SWCQE(1) |
				 V_CQE_DRAIN(1) |
				 V_CQE_QPID(qhp->wq.sq.qid));

	pthread_spin_lock(&rchp->lock);
	cqe.bits_type_ts = htobe64(V_CQE_GENBIT((u64)cq->gen));
	cq->sw_queue[cq->sw_pidx] = cqe;
	t4_swcq_produce(cq);
	pthread_spin_unlock(&rchp->lock);

	t4_clear_cq_armed(&rchp->cq);
}

static void complete_rq_drain_wrs(struct c4iw_qp *qhp, struct ibv_recv_wr *wr)
{
	while (wr) {
		complete_rq_drain_wr(qhp, wr);
		wr = wr->next;
	}
}

static void post_write_cmpl(struct c4iw_qp *qhp, struct ibv_send_wr *wr)
{
	bool send_signaled = (wr->next->send_flags & IBV_SEND_SIGNALED) ||
			     qhp->sq_sig_all;
	bool write_signaled = (wr->send_flags & IBV_SEND_SIGNALED) ||
			      qhp->sq_sig_all;
	struct t4_swsqe *swsqe;
	union t4_wr *wqe;
	u16 write_wrid;
	u8 len16;
	u16 idx;

	/*
	 * The sw_sq entries still look like a WRITE and a SEND and consume
	 * 2 slots. The FW WR, however, will be a single uber-WR.
	 */
	wqe = (union t4_wr *)((u8 *)qhp->wq.sq.queue +
	      qhp->wq.sq.wq_pidx * T4_EQ_ENTRY_SIZE);
	build_rdma_write_cmpl(&qhp->wq.sq, &wqe->write_cmpl, wr, &len16);

	/* WRITE swsqe */
	swsqe = &qhp->wq.sq.sw_sq[qhp->wq.sq.pidx];
	swsqe->opcode = FW_RI_RDMA_WRITE;
	swsqe->idx = qhp->wq.sq.pidx;
	swsqe->complete = 0;
	swsqe->signaled = write_signaled;
	swsqe->flushed = 0;
	swsqe->wr_id = wr->wr_id;

	write_wrid = qhp->wq.sq.pidx;

	/* just bump the sw_sq */
	qhp->wq.sq.in_use++;
	if (++qhp->wq.sq.pidx == qhp->wq.sq.size)
		qhp->wq.sq.pidx = 0;

	/* SEND swsqe */
	swsqe = &qhp->wq.sq.sw_sq[qhp->wq.sq.pidx];
	if (wr->next->opcode == IBV_WR_SEND)
		swsqe->opcode = FW_RI_SEND;
	else
		swsqe->opcode = FW_RI_SEND_WITH_INV;
	swsqe->idx = qhp->wq.sq.pidx;
	swsqe->complete = 0;
	swsqe->signaled = send_signaled;
	swsqe->flushed = 0;
	swsqe->wr_id = wr->next->wr_id;

	wqe->write_cmpl.flags_send = send_signaled ? FW_RI_COMPLETION_FLAG : 0;
	wqe->write_cmpl.wrid_send = qhp->wq.sq.pidx;

	init_wr_hdr(wqe, write_wrid, FW_RI_RDMA_WRITE_CMPL_WR,
		    write_signaled ? FW_RI_COMPLETION_FLAG : 0, len16);
	t4_sq_produce(&qhp->wq, len16);
	idx = DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);

	t4_ring_sq_db(&qhp->wq, idx, dev_is_t4(qhp->rhp),
		      len16, wqe);
	return;
}

static int post_rc_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
	           struct ibv_send_wr **bad_wr)
{
	int err = 0;
	u8 uninitialized_var(len16);
	enum fw_wr_opcodes fw_opcode;
	enum fw_ri_wr_flags fw_flags;
	struct c4iw_qp *qhp;
	union t4_wr *wqe, lwqe;
	u32 num_wrs;
	struct t4_swsqe *swsqe;
	u16 idx = 0;

	qhp = to_c4iw_qp(ibqp);
	pthread_spin_lock(&qhp->lock);

	/*
	 * If the qp has been flushed, then just insert a special
	 * drain cqe.
	 */
	if (qhp->wq.flushed) {
		pthread_spin_unlock(&qhp->lock);
		err = complete_sq_drain_wrs(qhp, wr, bad_wr);
		return err;
	}
	num_wrs = t4_sq_avail(&qhp->wq);
	if (num_wrs == 0) {
		pthread_spin_unlock(&qhp->lock);
		*bad_wr = wr;
		return -ENOMEM;
	}

	/*
	 * Fastpath for NVMe-oF target WRITE + SEND_WITH_INV wr chain which is
	 * the response for small NVMEe-oF READ requests.  If the chain is
	 * exactly a WRITE->SEND_WITH_INV or a WRITE->SEND and the sgl depths 
	 * and lengths meet the requirements of the fw_ri_write_cmpl_wr work 
	 * request, then build and post the write_cmpl WR.  If any of the tests
	 * below are not true, then we continue on with the tradtional WRITE
	 * and SEND WRs.
	 */
	if (qhp->rhp->write_cmpl_supported &&
	    qhp->rhp->chip_version >= CHELSIO_T5 &&
	    wr && wr->next && !wr->next->next &&
	    wr->opcode == IBV_WR_RDMA_WRITE && wr->sg_list[0].length &&
	    wr->num_sge <= T4_WRITE_CMPL_MAX_SGL &&
	    (wr->next->opcode == IBV_WR_SEND_WITH_INV ||
	    wr->next->opcode == IBV_WR_SEND) &&
	    wr->next->sg_list[0].length == T4_WRITE_CMPL_MAX_CQE &&
	    wr->next->num_sge == 1 && num_wrs >= 2) {
		post_write_cmpl(qhp, wr);
		pthread_spin_unlock(&qhp->lock);
		return 0;
	}

	while (wr) {
		if (num_wrs == 0) {
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		wqe = &lwqe;
		fw_flags = 0;
		if (wr->send_flags & IBV_SEND_SOLICITED)
			fw_flags |= FW_RI_SOLICITED_EVENT_FLAG;
		if (wr->send_flags & IBV_SEND_SIGNALED || qhp->sq_sig_all)
			fw_flags |= FW_RI_COMPLETION_FLAG;
		swsqe = &qhp->wq.sq.sw_sq[qhp->wq.sq.pidx];
		switch (wr->opcode) {
		case IBV_WR_SEND_WITH_INV:
		case IBV_WR_SEND:
			INC_STAT(send);
			if (wr->send_flags & IBV_SEND_FENCE)
				fw_flags |= FW_RI_READ_FENCE_FLAG;
			fw_opcode = FW_RI_SEND_WR;
			if (wr->opcode == IBV_WR_SEND)
				swsqe->opcode = FW_RI_SEND;
			else
				swsqe->opcode = FW_RI_SEND_WITH_INV;
			err = build_rdma_send(&qhp->wq.sq, wqe, wr, &len16);
			break;
		case IBV_WR_RDMA_WRITE_WITH_IMM:
			if (unlikely(!(qhp->wq.sq.flags & T4_SQ_WRITE_W_IMM))) {
				err = -ENOSYS;
				break;
			}
			fw_flags |= FW_RI_RDMA_WRITE_WITH_IMMEDIATE;
			/*FALLTHROUGH*/
		case IBV_WR_RDMA_WRITE:
			INC_STAT(write);
			fw_opcode = FW_RI_RDMA_WRITE_WR;
			swsqe->opcode = FW_RI_RDMA_WRITE;
			err = build_rdma_write(&qhp->wq.sq, wqe, wr, &len16);
			break;
		case IBV_WR_RDMA_READ:
			INC_STAT(read);
			fw_opcode = FW_RI_RDMA_READ_WR;
			swsqe->opcode = FW_RI_READ_REQ;
			fw_flags = 0;
			err = build_rdma_read(wqe, wr, &len16);
			if (err)
				break;
			swsqe->read_len = wr->sg_list ? wr->sg_list[0].length : 0;
			if (!qhp->wq.sq.oldest_read)
				qhp->wq.sq.oldest_read = swsqe;
			break;
		default:
			PDBG("%s post of type=%d TBD!\n", __func__,
			     wr->opcode);
			err = -EINVAL;
		}
		if (err) {
			*bad_wr = wr;
			break;
		}
		swsqe->idx = qhp->wq.sq.pidx;
		swsqe->complete = 0;
		swsqe->signaled = (wr->send_flags & IBV_SEND_SIGNALED) ||
				  qhp->sq_sig_all;
		swsqe->flushed = 0;
		swsqe->wr_id = wr->wr_id;

		init_wr_hdr(wqe, qhp->wq.sq.pidx, fw_opcode, fw_flags, len16);
		PDBG("%s cookie 0x%llx pidx 0x%x opcode 0x%x\n",
		     __func__, (unsigned long long)wr->wr_id, qhp->wq.sq.pidx,
		     swsqe->opcode);
		wr = wr->next;
		num_wrs--;
		copy_wr_to_sq(&qhp->wq, wqe, len16);
		t4_sq_produce(&qhp->wq, len16);
		idx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	}
	if (t4_wq_db_enabled(&qhp->wq)) {
		t4_ring_sq_db(&qhp->wq, idx, dev_is_t4(qhp->rhp),
			      len16, wqe);
	} else
		ring_kernel_db(qhp, qhp->wq.sq.qid, idx);
	qhp->wq.sq.queue[qhp->wq.sq.size].status.host_wq_pidx = (qhp->wq.sq.wq_pidx);
	pthread_spin_unlock(&qhp->lock);
	return err;
}

static int sge_to_d(struct c4iw_dev *dev, struct ibv_sge *sge, __be64 *d)
{
	struct c4iw_mr *mhp;
	unsigned long pg_off;
	unsigned long pg_id;
	unsigned long mr_off;
	u64 pa;

	pthread_spin_lock(&dev->lock);
	mhp = dev->mmid2ptr[c4iw_mmid(sge->lkey)];
	pthread_spin_unlock(&dev->lock);
	if (!mhp)
		return EINVAL;

	if (sge->addr < mhp->va_fbo)
		return EINVAL;
	if (sge->addr + sge->length > mhp->va_fbo + mhp->len)
		return EINVAL;
	if (sge->length > mhp->page_size)
		return EINVAL;

	pg_off = sge->addr & ~mhp->page_mask;
	mr_off = sge->addr - mhp->va_fbo;
	pg_id = ((mhp->va_fbo & ~mhp->page_mask) + mr_off) >> mhp->page_shift;
	pa = mhp->sw_pbl[pg_id] + pg_off;
	if (sge->length == 16384)
		pa |= 8;
	*d = htobe64(pa);
	return 0;
}

static int next_pbe_from_sge(struct c4iw_dev *dev, struct ibv_send_wr *wr, int *sg_idx,
			      struct ibv_sge *cur_sge, struct ibv_sge *pbe)
{
	struct c4iw_mr *mhp;
	u64 r;
	u32 len;

	if (!cur_sge->length) {
		if (*sg_idx == wr->num_sge)
			return 0;
		*cur_sge = wr->sg_list[*sg_idx];
		(*sg_idx)++;
	}

	pthread_spin_lock(&dev->lock);
	mhp = dev->mmid2ptr[c4iw_mmid(cur_sge->lkey)];
	pthread_spin_unlock(&dev->lock);
	if (!mhp)
		return EINVAL;

	r = ROUND_UP(cur_sge->addr + 1, mhp->page_size);
	if ((cur_sge->addr + cur_sge->length) > r) {
		len = min(mhp->page_size,  r - cur_sge->addr);
		pbe->addr = cur_sge->addr;
		pbe->length = len;
		pbe->lkey = cur_sge->lkey;
		cur_sge->length -= len;
		cur_sge->addr += len;
	} else {
		*pbe = *cur_sge;
		cur_sge->length = 0;
	}
	return 1;
}

static int write_raw_sgl(struct c4iw_raw_qp *rqp, struct ibv_send_wr *wr,
			 struct ulptx_sgl *s, void *end, u32 *immdlen,
			 u32 *plen)
{
	struct ulptx_sge_pair *sp = (struct ulptx_sge_pair *)(s+1);
	struct ibv_sge cur_sge = {0,0,0}, pbe;
	int cur_idx = 0;
	int sp_idx = 0;
	int pbl_count = 0;
	int err = 0;

	*immdlen = 0;
	*plen = 0;
	cur_sge.length = 0;

	if ((unsigned long)(s + 1) > (unsigned long)end)
		return ENOSPC;
	if (next_pbe_from_sge(rqp->rhp, wr, &cur_idx, &cur_sge, &pbe)) {
		s->len0 = htonl(pbe.length);
		err = sge_to_d(rqp->rhp, &pbe, &s->addr0);
		if (err)
			return err;
		*plen += pbe.length;
		*immdlen += sizeof *s;
		pbl_count++;
	}
	while (next_pbe_from_sge(rqp->rhp, wr, &cur_idx, &cur_sge, &pbe)) {
		if ((unsigned long)(sp + 1) > (unsigned long)end)
			return ENOSPC;
		pbl_count++;
		*plen += pbe.length;
		sp->len[sp_idx] = htonl(pbe.length);
		err = sge_to_d(rqp->rhp, &pbe, &sp->addr[sp_idx]);
		if (err)
			return err;

		if (++sp_idx == 2) {
			sp_idx = 0;
			*immdlen += sizeof *sp;
			sp++;
		}
	}
	if (sp_idx == 1) {
		sp->len[1] = 0;
		*immdlen += sizeof *sp - sizeof sp->addr[1];
	} else if ((unsigned long) sp & 8)
		*(u64 *)sp = 0;
	s->cmd_nsge = htonl(V_ULPTX_CMD(ULP_TX_SC_DSGL) |
			    V_ULPTX_NSGE(pbl_count));
	return 0;
}

static int write_raw_immd(struct c4iw_raw_qp *rqp, struct ibv_send_wr *wr,
			  void *d, void *end, u32 *plen)
{
	struct ibv_sge *sge;
	unsigned rem;
	int i;

	*plen = 0;
	sge = wr->sg_list;
	for (i = 0; i < wr->num_sge; i++) {
		if (((unsigned long)d + ROUND_UP(sge->length, 16)) >=
		    (unsigned long)end)
			return ENOSPC;
		memcpy(d, (void *)(uintptr_t)sge->addr, sge->length);
		d += sge->length;
		*plen += sge->length;
		sge++;
	}
	rem = ROUND_UP(*plen, 16);
	if (rem != *plen)
		memset(d, 0, rem - *plen);
	return 0;
}

#define is_tcp(wr) (*(u8 *)((wr)->sg_list[0].addr + 23) == IPPROTO_TCP)
#define is_tcp6(wr) (*(u8 *)((wr)->sg_list[0].addr + 20) == IPPROTO_TCP)

static int build_tx_pkt(struct c4iw_raw_qp *rqp, struct fw_eth_tx_pkt_wr *wqe,
			void *end, struct ibv_send_wr *wr, u8 *len16)
{
	struct cpl_tx_pkt_core *cpl;
	u32 plen = 0;
	struct t4_txq *t = &rqp->txq;
	int err;
	u32 immdlen;
	u64 ctrl1;

	cpl = (void *)(wqe + 1);
	if (wr->send_flags & IBV_SEND_INLINE) {
		err = write_raw_immd(rqp, wr, cpl + 1, end, &plen);
		if (err)
			return err;
		immdlen = plen + sizeof *cpl;
		wqe->op_immdlen = htonl(V_FW_WR_OP(FW_ETH_TX_PKT_WR) |
					V_FW_WR_IMMDLEN(plen + sizeof *cpl));
	} else {
		err = write_raw_sgl(rqp, wr, (struct ulptx_sgl *)(cpl + 1),
				    end, &immdlen, &plen);
		if (err)
			return err;
		immdlen += sizeof *cpl;
		wqe->op_immdlen = htonl(V_FW_WR_OP(FW_ETH_TX_PKT_WR) |
					V_FW_WR_IMMDLEN(sizeof *cpl));
	}
	cpl->ctrl0 = htonl(V_TXPKT_OPCODE(CPL_TX_PKT_XT) |
			   V_TXPKT_INTF(t->tx_chan) | V_TXPKT_PF(t->pf));
	cpl->pack = 0;
	cpl->len = htons(plen);

	if (wr->send_flags & IBV_SEND_IP_CSUM) {
		ctrl1 = (is_tcp(wr) ? V_TXPKT_CSUM_TYPE(TX_CSUM_TCPIP) :
				      V_TXPKT_CSUM_TYPE(TX_CSUM_UDPIP)) |
			V_TXPKT_IPHDR_LEN(20) |
			((rqp->rhp->chip_version <= CHELSIO_T5) ?
			V_TXPKT_ETHHDR_LEN(0) : V_T6_TXPKT_ETHHDR_LEN(0));
	} else if (wr->send_flags & IBV_SEND_IP6_CSUM) {
		ctrl1 = (is_tcp6(wr) ? V_TXPKT_CSUM_TYPE(TX_CSUM_TCPIP6) :
				       V_TXPKT_CSUM_TYPE(TX_CSUM_UDPIP6)) |
			V_TXPKT_IPHDR_LEN(40) |
			((rqp->rhp->chip_version <= CHELSIO_T5) ?
			V_TXPKT_ETHHDR_LEN(0) : V_T6_TXPKT_ETHHDR_LEN(0));
	} else {
		ctrl1 = F_TXPKT_L4CSUM_DIS | F_TXPKT_IPCSUM_DIS;
	}
	if (t->flags & T4_SQ_VLAN) {
		ctrl1 |= F_TXPKT_VLAN_VLD | V_TXPKT_VLAN(t->vlan_pri);
	}
	cpl->ctrl1 = htobe64(ctrl1);

	*len16 = DIV_ROUND_UP(sizeof *wqe + immdlen, 16);
	wqe->equiq_to_len16 = htonl(
		((wr->send_flags & IBV_SEND_SIGNALED) ||
		 rqp->sq_sig_all ? F_FW_WR_EQUIQ : 0) |
		*len16);
	wqe->r3 = 0;
	return 0;
}

static int post_raw_send_one(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
			 struct ibv_send_wr **bad_wr)
{
	struct c4iw_raw_qp *rqp = to_c4iw_raw_qp(ibqp);
	int err;
	struct t4_txq *t = &rqp->txq;
	u8 len16 = 0;
	struct txq_desc ld;

	pthread_spin_lock(&rqp->lock);
	if (!t4_txq_avail(t)) {
		*bad_wr = wr;
		err = ENOMEM;
		goto out;
	}
	if (wr->num_sge > T4_MAX_TXQ_SGE) {
		*bad_wr = wr;
		err = EINVAL;
		goto out;
	}
	err = build_tx_pkt(rqp, (void *)&ld, &ld + 1, wr, &len16);
	if (err) {
		*bad_wr = wr;
		goto out;
	}
	copy_wr_to_txq(t, &ld, len16);
	if (t4_fl_db_enabled(rqp->srq ? &rqp->srq->fl : &rqp->fl))
		t4_ring_txq_db(t, DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE),
			       dev_is_t4(rqp->rhp), &ld);
	else
		ring_kernel_txq_db(rqp, DIV_ROUND_UP(len16*16,
				   T4_EQ_ENTRY_SIZE));
	t4_hwtxq_produce(t, len16);
	t->sw_queue[t->pidx].wr_id = wr->wr_id;
	t->sw_queue[t->pidx].wq_pidx = t->wq_pidx;
	t->sw_queue[t->pidx].signaled = (wr->send_flags & IBV_SEND_SIGNALED) ||
					rqp->sq_sig_all;
	t4_swtxq_produce(t);
	((struct t4_status_page *)&t->queue[t->size])->host_wq_pidx = t->wq_pidx;
out:
	pthread_spin_unlock(&rqp->lock);
	return err;
}

static void init_pkts_wqe(struct fw_eth_tx_pkts_wr *pkts_wqe, u32 tot_immd,
			  u32 tot_plen, u32 tot_pkts, u8 len16, u32 sig)
{
	pkts_wqe->op_pkd = htobe32(V_FW_WR_OP(FW_ETH_TX_PKTS_WR));
	pkts_wqe->equiq_to_len16 = htobe32(sig | len16);
	pkts_wqe->plen = htobe16(tot_plen);
	pkts_wqe->npkt = tot_pkts;
	pkts_wqe->r3 = 0;
	pkts_wqe->type = 0;
}

static int add_to_pkts_wqe(struct c4iw_raw_qp *rqp, struct ibv_send_wr *wr,
			   void **cur, void *end, u32 *tot_immd, u32 *tot_plen,
			   u32 *tot_pkts, u8 *len16)
{
	u32 immdlen;
	u32 plen;
	struct ulp_txpkt *mc = *cur;
	struct ulptx_idata *sc_imm = (void *)(mc + 1);
	struct cpl_tx_pkt_core *cpl = (void *)(sc_imm + 1);
	int err;
	struct t4_txq *t = &rqp->txq;
	u64 ctrl1;

	if (wr->send_flags & IBV_SEND_INLINE) {
		err = write_raw_immd(rqp, wr, cpl + 1, end, &plen);
		if (err)
			return err;
		immdlen = sizeof *cpl + sizeof *mc + sizeof *sc_imm + plen;
		sc_imm->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM));
		sc_imm->len = htobe32(sizeof *cpl + plen);
	} else {
		err = write_raw_sgl(rqp, wr, (struct ulptx_sgl *)(cpl + 1),
				    end, &immdlen, &plen);
		if (err)
			return err;
		immdlen += sizeof *cpl + sizeof *mc + sizeof *sc_imm;
		sc_imm->cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM) |
					       0x800000);
		sc_imm->len = htobe32(sizeof *cpl);
	}
	*tot_immd += immdlen;
	*tot_plen += plen;
	*tot_pkts += 1;
	mc->cmd_dest = htobe32(V_ULPTX_CMD(4) | V_ULP_TXPKT_DEST(0) |
		   V_ULP_TXPKT_FID(rqp->srq ? rqp->srq->iq.qid : rqp->iq.qid));
	mc->len = htobe32(DIV_ROUND_UP(immdlen, 16));
	cpl->ctrl0 = htobe32(V_TXPKT_OPCODE(CPL_TX_PKT_XT) |
				 V_TXPKT_INTF(t->tx_chan) | V_TXPKT_PF(t->pf));
	cpl->pack = 0;
	if (wr->send_flags & IBV_SEND_IP_CSUM) {
		ctrl1 = (is_tcp(wr) ? V_TXPKT_CSUM_TYPE(TX_CSUM_TCPIP) :
				      V_TXPKT_CSUM_TYPE(TX_CSUM_UDPIP)) |
			V_TXPKT_IPHDR_LEN(20) |
			((rqp->rhp->chip_version <= CHELSIO_T5) ?
			V_TXPKT_ETHHDR_LEN(0) : V_T6_TXPKT_ETHHDR_LEN(0));
	} else if (wr->send_flags & IBV_SEND_IP6_CSUM) {
		ctrl1 = (is_tcp6(wr) ? V_TXPKT_CSUM_TYPE(TX_CSUM_TCPIP6) :
				       V_TXPKT_CSUM_TYPE(TX_CSUM_UDPIP6)) |
			V_TXPKT_IPHDR_LEN(40) |
			((rqp->rhp->chip_version <= CHELSIO_T5) ?
			V_TXPKT_ETHHDR_LEN(0) : V_T6_TXPKT_ETHHDR_LEN(0));
	} else {
		ctrl1 = F_TXPKT_L4CSUM_DIS | F_TXPKT_IPCSUM_DIS;
	}
	if (t->flags & T4_SQ_VLAN) {
		ctrl1 |= F_TXPKT_VLAN_VLD | V_TXPKT_VLAN(t->vlan_pri);
	}
	cpl->ctrl1 = htobe64(ctrl1);
	cpl->len = htobe16(plen);
	*len16 += DIV_ROUND_UP(immdlen, 16);
	*cur = (void *)mc + ROUND_UP(immdlen, 16);
	return 0;
}

static int post_raw_send_many(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
			      struct ibv_send_wr **bad_wr)
{
	struct c4iw_raw_qp *rqp = to_c4iw_raw_qp(ibqp);
	int err = 0;
	unsigned int num_wrs;
	struct t4_txq *t = &rqp->txq;
	unsigned int ndesc = 0;
	struct txq_desc ld[2];
	int coalescing = 1;
	u32 tot_immd = 0;
	u32 tot_plen = 0;
	u32 tot_pkts = 0;
	void *cur = (void *)ld + sizeof(struct fw_eth_tx_pkts_wr);
	u8 len16 = DIV_ROUND_UP(sizeof(struct fw_eth_tx_pkts_wr), 16);

	pthread_spin_lock(&rqp->lock);
	num_wrs = t4_txq_avail(t);
	while (wr) {
		if (!num_wrs) {
			*bad_wr = wr;
			err = ENOMEM;
			break;
		}
		if (wr->num_sge > T4_MAX_TXQ_SGE) {
			*bad_wr = wr;
			err = EINVAL;
			break;
		}
again:
		if (!coalescing) {
			tot_immd = 0;
			tot_plen = 0;
			tot_pkts = 0;
			coalescing = 1;
			cur = (void *)ld + sizeof(struct fw_eth_tx_pkts_wr);
			len16 = DIV_ROUND_UP(sizeof(struct fw_eth_tx_pkts_wr),
					     16);
		}

		err = add_to_pkts_wqe(rqp, wr, &cur, ld + 2, &tot_immd,
				      &tot_plen, &tot_pkts, &len16);
		if (err) {
			if (err == ENOSPC) {
				init_pkts_wqe((void *)ld, tot_immd, tot_plen,
					      tot_pkts, len16, 0);
				copy_wr_to_txq(t, ld, len16);
				t4_hwtxq_produce(t, len16);
				ndesc += DIV_ROUND_UP(len16*16,
						      T4_EQ_ENTRY_SIZE);
				coalescing = 0;
				goto again;
			}
			*bad_wr = wr;
			break;
		}

		if ((wr->send_flags & IBV_SEND_SIGNALED) || rqp->sq_sig_all) {
			init_pkts_wqe((void *)ld, tot_immd, tot_plen, tot_pkts,
				      len16, F_FW_WR_EQUIQ);
			copy_wr_to_txq(t, ld, len16);
			t4_hwtxq_produce(t, len16);
			ndesc += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
			coalescing = 0;
		}

		t->sw_queue[t->pidx].wr_id = wr->wr_id;
		t->sw_queue[t->pidx].wq_pidx = t->wq_pidx;
		t->sw_queue[t->pidx].signaled = (wr->send_flags &
					IBV_SEND_SIGNALED) || rqp->sq_sig_all;
		t4_swtxq_produce(t);
		wr = wr->next;
		num_wrs--;
	}
	if (coalescing) {
		init_pkts_wqe((void *)ld, tot_immd, tot_plen, tot_pkts, len16,
			      0);
		copy_wr_to_txq(t, ld, len16);
		t4_hwtxq_produce(t, len16);
		ndesc += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	}
	if (ndesc) {
		if (t4_fl_db_enabled(rqp->srq ? &rqp->srq->fl : &rqp->fl))
			t4_ring_txq_db(t, ndesc, dev_is_t4(rqp->rhp), ld);
		else
			ring_kernel_txq_db(rqp, ndesc);
		((struct t4_status_page *)&t->queue[t->size])->host_wq_pidx = t->wq_pidx;
	}
	pthread_spin_unlock(&rqp->lock);
	return err;
}

int c4iw_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
	           struct ibv_send_wr **bad_wr)
{
	switch (ibqp->qp_type) {
	case IBV_QPT_RC:
		return post_rc_send(ibqp, wr, bad_wr);
	case IBV_QPT_RAW_ETH:
		if (wr->next)
			return post_raw_send_many(ibqp, wr, bad_wr);
		else
			return post_raw_send_one(ibqp, wr, bad_wr);
	default:
		return ENOSYS;
	}
}

static int post_rc_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
			   struct ibv_recv_wr **bad_wr)
{
	int err = 0;
	struct c4iw_qp *qhp;
	union t4_recv_wr *wqe, lwqe;
	u32 num_wrs;
	u8 len16 = 0;
	u16 idx = 0;

	qhp = to_c4iw_qp(ibqp);
	pthread_spin_lock(&qhp->lock);

	/*
	 * If the qp has been flushed, then just insert a special
	 * drain cqe.
	 */
	if (qhp->wq.flushed) {
		pthread_spin_unlock(&qhp->lock);
		complete_rq_drain_wrs(qhp, wr);
		return err;
	}
	INC_STAT(recv);
	num_wrs = t4_rq_avail(&qhp->wq);
	if (num_wrs == 0) {
		pthread_spin_unlock(&qhp->lock);
		*bad_wr = wr;
		return -ENOMEM;
	}
	while (wr) {
		if (wr->num_sge > T4_MAX_RECV_SGE) {
			err = -EINVAL;
			*bad_wr = wr;
			break;
		}
		wqe = &lwqe;
		if (num_wrs)
			err = build_rdma_recv(&qhp->wq.rq, wqe, wr, &len16);
		else
			err = -ENOMEM;
		if (err) {
			*bad_wr = wr;
			break;
		}

		qhp->wq.rq.sw_rq[qhp->wq.rq.pidx].wr_id = wr->wr_id;

		wqe->recv.opcode = FW_RI_RECV_WR;
		wqe->recv.r1 = 0;
		wqe->recv.wrid = qhp->wq.rq.pidx;
		wqe->recv.r2[0] = 0;
		wqe->recv.r2[1] = 0;
		wqe->recv.r2[2] = 0;
		wqe->recv.len16 = len16;
		PDBG("%s cookie 0x%llx pidx %u\n", __func__,
		     (unsigned long long) wr->wr_id, qhp->wq.rq.pidx);
		copy_wr_to_rq(&qhp->wq, wqe, len16);
		t4_rq_produce(&qhp->wq, len16);
		idx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
		wr = wr->next;
		num_wrs--;
	}
	if (t4_wq_db_enabled(&qhp->wq))
		t4_ring_rq_db(&qhp->wq, idx, dev_is_t4(qhp->rhp),
			      len16, wqe);
	else
		ring_kernel_db(qhp, qhp->wq.rq.qid, idx);
	qhp->wq.rq.queue[qhp->wq.rq.size].status.host_wq_pidx = (qhp->wq.rq.wq_pidx);
	pthread_spin_unlock(&qhp->lock);
	return err;
}


static int post_raw_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
			    struct ibv_recv_wr **bad_wr)
{
	struct c4iw_raw_qp *rqp = to_c4iw_raw_qp(ibqp);
	struct t4_raw_fl *f = &rqp->fl;
	unsigned int num_wrs;
	__be64 *d;
	unsigned int cred;
	int err = 0;

	pthread_spin_lock(&rqp->lock);
	num_wrs = t4_raw_fl_avail(f);
	if (num_wrs == 0) {
		pthread_spin_unlock(&rqp->lock);
		*bad_wr = wr;
		return ENOMEM;
	}
	cred = 0;
	while (wr) {
		d = (__be64 *)&f->queue[f->pidx];
		if (wr->num_sge > 1) {
			err = EINVAL;
			*bad_wr = wr;
			break;
		}
		if (wr->sg_list->addr & ~c4iw_page_mask) {
			err = EINVAL;
			*bad_wr = wr;
			break;
		}
		if (wr->sg_list->length != c4iw_page_size &&
		    wr->sg_list->length != 16384) {
			printf("bad len %d\n", wr->sg_list->length);
			err = EINVAL;
			*bad_wr = wr;
			break;
		}
		if (num_wrs)
			err = sge_to_d(rqp->rhp, wr->sg_list, d);
		else
			err = ENOMEM;
		if (err) {
			*bad_wr = wr;
			break;
		}
		f->sw_queue[f->pidx] = wr->wr_id;
		t4_raw_fl_produce(f);
		num_wrs--;
		cred++;
		wr = wr->next;
	}
	f->pend_cred += cred;
	if (t4_fl_db_enabled(f))
		t4_ring_fl_db(f, rqp->rhp->chip_version);
	else
		ring_kernel_fl_db(rqp, f);
	((struct t4_status_page *)&f->queue[f->size])->host_wq_pidx = f->pidx / 8;
	pthread_spin_unlock(&rqp->lock);
	return err;
}

static int post_raw_srq_recv(struct ibv_srq *ibsrq, struct ibv_recv_wr *wr,
			     struct ibv_recv_wr **bad_wr)
{
	struct c4iw_raw_srq *srq = to_c4iw_raw_srq(ibsrq);
	struct t4_raw_fl *f = &srq->fl;
	unsigned int num_wrs;
	__be64 *d;
	unsigned int cred;
	int err = 0;

	pthread_spin_lock(&srq->lock);
	num_wrs = t4_raw_fl_avail(f);
	if (num_wrs == 0) {
		pthread_spin_unlock(&srq->lock);
		return ENOMEM;
	}
	cred = 0;
	while (wr) {
		d = (__be64 *)&f->queue[f->pidx];
		if (wr->num_sge > 1) {
			err = EINVAL;
			*bad_wr = wr;
			break;
		}
		if (wr->sg_list->addr & ~c4iw_page_mask) {
			err = EINVAL;
			*bad_wr = wr;
			break;
		}
		if (wr->sg_list->length != c4iw_page_size &&
		    wr->sg_list->length != 16384) {
			err = EINVAL;
			*bad_wr = wr;
			break;
		}
		if (num_wrs)
			err = sge_to_d(srq->rhp, wr->sg_list, d);
		else
			err = ENOMEM;
		if (err) {
			*bad_wr = wr;
			break;
		}
		f->sw_queue[f->pidx] = wr->wr_id;
		t4_raw_fl_produce(f);
		num_wrs--;
		cred++;
		wr = wr->next;
	}
	f->pend_cred += cred;
	if (t4_fl_db_enabled(f))
		t4_ring_fl_db(f, srq->rhp->chip_version);
	else
		ring_kernel_srq_db(srq, f);
	((struct t4_status_page *)&f->queue[f->size])->host_wq_pidx = f->pidx / 8;
	pthread_spin_unlock(&srq->lock);
	return err;
}


static void defer_srq_wr(struct t4_srq *srq, union t4_recv_wr *wqe, uint64_t wr_id, u8 len16)
{
	struct t4_srq_pending_wr *pwr = &srq->pending_wrs[srq->pending_pidx];

	PDBG("%s cidx %u pidx %u wq_pidx %u in_use %u ooo_count %u wr_id 0x%llx "
		"pending_cidx %u pending_pidx %u pending_in_use %u\n",
		__func__, srq->cidx, srq->pidx, srq->wq_pidx,
		srq->in_use, srq->ooo_count, (unsigned long long)wr_id, srq->pending_cidx,
		srq->pending_pidx, srq->pending_in_use);
	pwr->wr_id = wr_id;
	pwr->len16 = len16;
	memcpy(&pwr->wqe, wqe, len16*16);
	t4_srq_produce_pending_wr(srq);
}

static int post_srq_recv(struct ibv_srq *ibsrq, struct ibv_recv_wr *wr,
			 struct ibv_recv_wr **bad_wr)
{
	int err = 0;
	struct c4iw_srq *srq;
	union t4_recv_wr *wqe, lwqe;
	u32 num_wrs;
	u8 len16 = 0;
	u16 idx = 0;

	srq = to_c4iw_srq(ibsrq);
	pthread_spin_lock(&srq->lock);
	INC_STAT(srq_recv);
	num_wrs = t4_srq_avail(&srq->wq);
	if (num_wrs == 0) {
		pthread_spin_unlock(&srq->lock);
		return -ENOMEM;
	}
	while (wr) {
		if (wr->num_sge > T4_MAX_RECV_SGE) {
			err = -EINVAL;
			*bad_wr = wr;
			break;
		}
		wqe = &lwqe;
		if (num_wrs)
			err = build_srq_recv(wqe, wr, &len16);
		else
			err = -ENOMEM;
		if (err) {
			*bad_wr = wr;
			break;
		}

		wqe->recv.opcode = FW_RI_RECV_WR;
		wqe->recv.r1 = 0;
		wqe->recv.wrid = srq->wq.pidx;
		wqe->recv.r2[0] = 0;
		wqe->recv.r2[1] = 0;
		wqe->recv.r2[2] = 0;
		wqe->recv.len16 = len16;

		if (srq->wq.ooo_count || srq->wq.pending_in_use || srq->wq.sw_rq[srq->wq.pidx].valid)
			defer_srq_wr(&srq->wq, wqe, wr->wr_id, len16);
		else {
			srq->wq.sw_rq[srq->wq.pidx].wr_id = wr->wr_id;
			srq->wq.sw_rq[srq->wq.pidx].valid = 1;
			c4iw_copy_wr_to_srq(&srq->wq, wqe, len16);
			PDBG("%s cidx %u pidx %u wq_pidx %u in_use %u "
				"wr_id 0x%llx \n", __func__, srq->wq.cidx,
				srq->wq.pidx, srq->wq.wq_pidx, srq->wq.in_use,
				(unsigned long long)wr->wr_id);
			t4_srq_produce(&srq->wq, len16);
			idx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
		}
		wr = wr->next;
		num_wrs--;
	}

	if (idx) {
		t4_ring_srq_db(&srq->wq, idx, len16, wqe);
		srq->wq.queue[srq->wq.size].status.host_wq_pidx =
			srq->wq.wq_pidx;
	}
	pthread_spin_unlock(&srq->lock);
	return err;
}

int c4iw_post_srq_recv(struct ibv_srq *ibsrq, struct ibv_recv_wr *wr,
		       struct ibv_recv_wr **bad_wr)
{
	struct c4iw_srq *srq = to_c4iw_srq(ibsrq);

	if (srq->type == C4IW_SRQ_RAW)
		return post_raw_srq_recv(ibsrq, wr, bad_wr);
	return post_srq_recv(ibsrq, wr, bad_wr);
}

int c4iw_post_receive(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
		      struct ibv_recv_wr **bad_wr)
{
	switch (ibqp->qp_type) {
	case IBV_QPT_RC:
		return post_rc_recv(ibqp, wr, bad_wr);
	case IBV_QPT_RAW_ETH:
		return post_raw_recv(ibqp, wr, bad_wr);
	default:
		return ENOSYS;
	}
}

/*
 * Assumes qhp lock is held.
 */
void c4iw_flush_qp(struct c4iw_qp *qhp)
{
	struct c4iw_cq *rchp, *schp;
	int count;
	u32 srqidx = t4_wq_srqidx(&qhp->wq);

	rchp = to_c4iw_cq(qhp->ibv_qp.recv_cq);
	schp = to_c4iw_cq(qhp->ibv_qp.send_cq);

	/* locking heirarchy: cq lock first, then qp lock. */
	pthread_spin_lock(&rchp->lock);
	if (rchp != schp)
		pthread_spin_lock(&schp->lock);
	pthread_spin_lock(&qhp->lock);

	if (qhp->wq.flushed) {
		pthread_spin_unlock(&qhp->lock);
		if (rchp != schp)
			pthread_spin_unlock(&schp->lock);
		pthread_spin_unlock(&rchp->lock);
		return;
	}

	qhp->wq.flushed = 1;
	t4_set_wq_in_error(&qhp->wq);

	if (qhp->srq)
		pthread_spin_lock(&qhp->srq->lock);

	if (srqidx)
		c4iw_flush_srqidx(qhp, srqidx);

	qhp->ibv_qp.state = IBV_QPS_ERR;

	c4iw_flush_hw_cq(rchp);
	c4iw_count_rcqes(&rchp->cq, &qhp->wq, &count);
	if (!qhp->srq)
		c4iw_flush_rq(&qhp->wq, &rchp->cq, count);

	if (schp != rchp)
		c4iw_flush_hw_cq(schp);
	c4iw_flush_sq(qhp);
	if (qhp->srq)
		pthread_spin_unlock(&qhp->srq->lock);
	pthread_spin_unlock(&qhp->lock);
	if (rchp != schp)
		pthread_spin_unlock(&schp->lock);
	pthread_spin_unlock(&rchp->lock);
}

void c4iw_flush_qps(struct c4iw_dev *dev)
{
	int i;

	pthread_spin_lock(&dev->lock);
	for (i=0; i < dev->max_qp; i++) {
		struct c4iw_qp *qhp = dev->qpid2ptr[i];
		if (qhp && qhp->ibv_qp.qp_type == IBV_QPT_RC) {
			if (!qhp->wq.flushed && t4_wq_in_error(&qhp->wq))
				c4iw_flush_qp(qhp);
		}
	}
	pthread_spin_unlock(&dev->lock);
}
