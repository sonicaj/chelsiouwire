/*-
 *   BSD LICENSE
 *
 *   Copyright (c) 2020, Western Digital Corporation. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * NVMe driver public API extension for Zoned Namespace Command Set
 */

#ifndef SPDK_NVME_ZNS_H
#define SPDK_NVME_ZNS_H

#include "spdk/stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "spdk/nvme.h"

/**
 * Get the Zoned Namespace Command Set Specific Identify Namespace data
 * as defined by the NVMe Zoned Namespace Command Set Specification.
 *
 * This function is thread safe and can be called at any point while the controller
 * is attached to the SPDK NVMe driver.
 *
 * \param ns Namespace.
 *
 * \return a pointer to the namespace data, or NULL if the namespace is not
 * a Zoned Namespace.
 */
const struct spdk_nvme_zns_ns_data *spdk_nvme_zns_ns_get_data(struct spdk_nvme_ns *ns);

/**
 * Get the zone size, in bytes, of the given namespace.
 *
 * This function is thread safe and can be called at any point while the controller
 * is attached to the SPDK NVMe driver.
 *
 * \param ns Namespace to query.
 *
 * \return the zone size of the given namespace in bytes.
 */
uint64_t spdk_nvme_zns_ns_get_zone_size(struct spdk_nvme_ns *ns);

/**
 * Get the number of zones for the given namespace.
 *
 * This function is thread safe and can be called at any point while the controller
 * is attached to the SPDK NVMe driver.
 *
 * \param ns Namespace to query.
 *
 * \return the number of zones.
 */
uint64_t spdk_nvme_zns_ns_get_num_zones(struct spdk_nvme_ns *ns);

/**
 * Get the Zoned Namespace Command Set Specific Identify Controller data
 * as defined by the NVMe Zoned Namespace Command Set Specification.
 *
 * This function is thread safe and can be called at any point while the controller
 * is attached to the SPDK NVMe driver.
 *
 * \param ctrlr Opaque handle to NVMe controller.
 *
 * \return pointer to the controller data, or NULL if the controller does not
 * support the Zoned Command Set.
 */
const struct spdk_nvme_zns_ctrlr_data *spdk_nvme_zns_ctrlr_get_data(struct spdk_nvme_ctrlr *ctrlr);

/**
 * Submit a Close Zone operation to the specified NVMe namespace.
 *
 * \param ns Namespace.
 * \param qpair I/O queue pair to submit the request.
 * \param slba starting LBA of the zone to operate on.
 * \param select_all If this is set, slba will be ignored, and operation will
 * be performed on all zones that are in ZSIO or ZSEO state.
 * \param cb_fn Callback function invoked when the I/O command completes.
 * \param cb_arg Argument passed to callback function.
 *
 * \return 0 on success. Negated errno on failure.
 */
int spdk_nvme_zns_close_zone(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			     uint64_t slba, bool select_all,
			     spdk_nvme_cmd_cb cb_fn, void *cb_arg);

/**
 * Submit a Finish Zone operation to the specified NVMe namespace.
 *
 * \param ns Namespace.
 * \param qpair I/O queue pair to submit the request.
 * \param slba starting LBA of the zone to operate on.
 * \param select_all If this is set, slba will be ignored, and operation will
 * be performed on all zones that are in ZSIO, ZSEO, or ZSC state.
 * \param cb_fn Callback function invoked when the I/O command completes.
 * \param cb_arg Argument passed to callback function.
 *
 * \return 0 on success. Negated errno on failure.
 */
int spdk_nvme_zns_finish_zone(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			      uint64_t slba, bool select_all,
			      spdk_nvme_cmd_cb cb_fn, void *cb_arg);

/**
 * Submit a Open Zone operation to the specified NVMe namespace.
 *
 * \param ns Namespace.
 * \param qpair I/O queue pair to submit the request.
 * \param slba starting LBA of the zone to operate on.
 * \param select_all If this is set, slba will be ignored, and operation will
 * be performed on all zones that are in ZSC state.
 * \param cb_fn Callback function invoked when the I/O command completes.
 * \param cb_arg Argument passed to callback function.
 *
 * \return 0 on success. Negated errno on failure.
 */
int spdk_nvme_zns_open_zone(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			    uint64_t slba, bool select_all,
			    spdk_nvme_cmd_cb cb_fn, void *cb_arg);

/**
 * Submit a Reset Zone operation to the specified NVMe namespace.
 *
 * \param ns Namespace.
 * \param qpair I/O queue pair to submit the request.
 * \param slba starting LBA of the zone to operate on.
 * \param select_all If this is set, slba will be ignored, and operation will
 * be performed on all zones that are in ZSIO, ZSEO, ZSC, or ZSF state.
 * \param cb_fn Callback function invoked when the I/O command completes.
 * \param cb_arg Argument passed to callback function.
 *
 * \return 0 on success. Negated errno on failure.
 */
int spdk_nvme_zns_reset_zone(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			     uint64_t slba, bool select_all,
			     spdk_nvme_cmd_cb cb_fn, void *cb_arg);

/**
 * Submit a Offline Zone operation to the specified NVMe namespace.
 *
 * \param ns Namespace.
 * \param qpair I/O queue pair to submit the request.
 * \param slba starting LBA of the zone to operate on.
 * \param select_all If this is set, slba will be ignored, and operation will
 * be performed on all zones that are in ZSRO state.
 * \param cb_fn Callback function invoked when the I/O command completes.
 * \param cb_arg Argument passed to callback function.
 *
 * \return 0 on success. Negated errno on failure.
 */
int spdk_nvme_zns_offline_zone(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			       uint64_t slba, bool select_all,
			       spdk_nvme_cmd_cb cb_fn, void *cb_arg);

/**
 * Get a zone report from the specified NVMe namespace.
 *
 * \param ns Namespace.
 * \param qpair I/O queue pair to submit the request.
 * \param payload The pointer to the payload buffer.
 * \param payload_size The size of payload buffer.
 * \param slba starting LBA of the zone to operate on.
 * \param report_opts Filter on which zone states to include in the zone report.
 * \param partial_report If true, nr_zones field in the zone report indicates the number of zone
 * descriptors that were successfully written to the zone report. If false, nr_zones field in the
 * zone report indicates the number of zone descriptors that match the report_opts criteria.
 * \param cb_fn Callback function invoked when the I/O command completes.
 * \param cb_arg Argument passed to callback function.
 *
 * \return 0 on success. Negated errno on failure.
 */
int spdk_nvme_zns_report_zones(struct spdk_nvme_ns *ns, struct spdk_nvme_qpair *qpair,
			       void *payload, uint32_t payload_size, uint64_t slba,
			       enum spdk_nvme_zns_zra_report_opts report_opts, bool partial_report,
			       spdk_nvme_cmd_cb cb_fn, void *cb_arg);

#ifdef __cplusplus
}
#endif

#endif
