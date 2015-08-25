/*-
 *   BSD LICENSE
 *
 *   Copyright 2015 6WIND S.A.
 *   Copyright 2015 Mellanox.
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
 *     * Neither the name of 6WIND S.A. nor the names of its
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

#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

/* Verbs header. */
/* ISO C doesn't support unnamed structs/unions, disabling -pedantic. */
#ifdef PEDANTIC
#pragma GCC diagnostic ignored "-pedantic"
#endif
#include <infiniband/verbs.h>
#ifdef PEDANTIC
#pragma GCC diagnostic error "-pedantic"
#endif

/* DPDK headers don't like -pedantic. */
#ifdef PEDANTIC
#pragma GCC diagnostic ignored "-pedantic"
#endif
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_ethdev.h>
#ifdef PEDANTIC
#pragma GCC diagnostic error "-pedantic"
#endif

#include "mlx5.h"
#include "mlx5_rxtx.h"
#include "mlx5_utils.h"
#include "mlx5_defs.h"

/* Initialization data for hash RX queues. */
const struct hash_rxq_init hash_rxq_init[] = {
	[HASH_RXQ_TCPv4] = {
		.hash_fields = (IBV_EXP_RX_HASH_SRC_IPV4 |
				IBV_EXP_RX_HASH_DST_IPV4 |
				IBV_EXP_RX_HASH_SRC_PORT_TCP |
				IBV_EXP_RX_HASH_DST_PORT_TCP),
		.dpdk_rss_hf = ETH_RSS_NONFRAG_IPV4_TCP,
		.flow_priority = 0,
		.flow_spec.tcp_udp = {
			.type = IBV_FLOW_SPEC_TCP,
			.size = sizeof(hash_rxq_init[0].flow_spec.tcp_udp),
		},
		.underlayer = &hash_rxq_init[HASH_RXQ_IPv4],
	},
	[HASH_RXQ_UDPv4] = {
		.hash_fields = (IBV_EXP_RX_HASH_SRC_IPV4 |
				IBV_EXP_RX_HASH_DST_IPV4 |
				IBV_EXP_RX_HASH_SRC_PORT_UDP |
				IBV_EXP_RX_HASH_DST_PORT_UDP),
		.dpdk_rss_hf = ETH_RSS_NONFRAG_IPV4_UDP,
		.flow_priority = 0,
		.flow_spec.tcp_udp = {
			.type = IBV_FLOW_SPEC_UDP,
			.size = sizeof(hash_rxq_init[0].flow_spec.tcp_udp),
		},
		.underlayer = &hash_rxq_init[HASH_RXQ_IPv4],
	},
	[HASH_RXQ_IPv4] = {
		.hash_fields = (IBV_EXP_RX_HASH_SRC_IPV4 |
				IBV_EXP_RX_HASH_DST_IPV4),
		.dpdk_rss_hf = (ETH_RSS_IPV4 |
				ETH_RSS_FRAG_IPV4),
		.flow_priority = 1,
		.flow_spec.ipv4 = {
			.type = IBV_FLOW_SPEC_IPV4,
			.size = sizeof(hash_rxq_init[0].flow_spec.ipv4),
		},
		.underlayer = &hash_rxq_init[HASH_RXQ_ETH],
	},
#ifdef HASH_RXQ_IPV6
	[HASH_RXQ_TCPv6] = {
		.hash_fields = (IBV_EXP_RX_HASH_SRC_IPV6 |
				IBV_EXP_RX_HASH_DST_IPV6 |
				IBV_EXP_RX_HASH_SRC_PORT_TCP |
				IBV_EXP_RX_HASH_DST_PORT_TCP),
		.dpdk_rss_hf = ETH_RSS_NONFRAG_IPV6_TCP,
		.flow_priority = 0,
		.flow_spec.tcp_udp = {
			.type = IBV_FLOW_SPEC_TCP,
			.size = sizeof(hash_rxq_init[0].flow_spec.tcp_udp),
		},
		.underlayer = &hash_rxq_init[HASH_RXQ_IPv6],
	},
	[HASH_RXQ_UDPv6] = {
		.hash_fields = (IBV_EXP_RX_HASH_SRC_IPV6 |
				IBV_EXP_RX_HASH_DST_IPV6 |
				IBV_EXP_RX_HASH_SRC_PORT_UDP |
				IBV_EXP_RX_HASH_DST_PORT_UDP),
		.dpdk_rss_hf = ETH_RSS_NONFRAG_IPV6_UDP,
		.flow_priority = 0,
		.flow_spec.tcp_udp = {
			.type = IBV_FLOW_SPEC_UDP,
			.size = sizeof(hash_rxq_init[0].flow_spec.tcp_udp),
		},
		.underlayer = &hash_rxq_init[HASH_RXQ_IPv6],
	},
	[HASH_RXQ_IPv6] = {
		.hash_fields = (IBV_EXP_RX_HASH_SRC_IPV6 |
				IBV_EXP_RX_HASH_DST_IPV6),
		.dpdk_rss_hf = (ETH_RSS_IPV6 |
				ETH_RSS_FRAG_IPV6),
		.flow_priority = 1,
		.flow_spec.ipv6 = {
			.type = IBV_FLOW_SPEC_IPV6,
			.size = sizeof(hash_rxq_init[0].flow_spec.ipv6),
		},
		.underlayer = &hash_rxq_init[HASH_RXQ_ETH],
	},
#endif /* HASH_RXQ_IPV6 */
	[HASH_RXQ_ETH] = {
		.hash_fields = 0,
		.dpdk_rss_hf = 0,
		.flow_priority = 2,
		.flow_spec.eth = {
			.type = IBV_FLOW_SPEC_ETH,
			.size = sizeof(hash_rxq_init[0].flow_spec.eth),
		},
		.underlayer = NULL,
	},
};

/* Number of entries in hash_rxq_init[]. */
const unsigned int hash_rxq_init_n = elemof(hash_rxq_init);

/* Initialization data for hash RX queue indirection tables. */
static const struct ind_table_init ind_table_init[] = {
	[IND_TABLE_GENERIC] = {
		.max_size = -1u, /* Superseded by HW limitations. */
		.hash_types = &(const enum hash_rxq_type []){
			HASH_RXQ_TCPv4,
			HASH_RXQ_UDPv4,
			HASH_RXQ_IPv4,
#ifdef HASH_RXQ_IPV6
			HASH_RXQ_TCPv6,
			HASH_RXQ_UDPv6,
			HASH_RXQ_IPv6,
#endif /* HASH_RXQ_IPV6 */
		},
#ifdef HASH_RXQ_IPV6
		.hash_types_n = 6,
#else /* HASH_RXQ_IPV6 */
		.hash_types_n = 3,
#endif
	},
	[IND_TABLE_DRAIN] = {
		.max_size = 1,
		.hash_types = &(const enum hash_rxq_type []){
			HASH_RXQ_ETH,
		},
		.hash_types_n = 1,
	},
};

/* Indirection tables to initialize when RSS is enabled. */
static const struct ind_table_init *const ind_table_init_rss[] = {
	&ind_table_init[IND_TABLE_GENERIC],
	&ind_table_init[IND_TABLE_DRAIN],
	NULL,
};

/* Indirection tables to initialize when RSS is disabled. */
static const struct ind_table_init *const ind_table_init_no_rss[] = {
	&ind_table_init[IND_TABLE_DRAIN],
	NULL,
};

/* Default RSS hash key also used for ConnectX-3. */
uint8_t rss_hash_default_key[] = {
	0x2c, 0xc6, 0x81, 0xd1,
	0x5b, 0xdb, 0xf4, 0xf7,
	0xfc, 0xa2, 0x83, 0x19,
	0xdb, 0x1a, 0x3e, 0x94,
	0x6b, 0x9e, 0x38, 0xd9,
	0x2c, 0x9c, 0x03, 0xd1,
	0xad, 0x99, 0x44, 0xa7,
	0xd9, 0x56, 0x3d, 0x59,
	0x06, 0x3c, 0x25, 0xf3,
	0xfc, 0x1f, 0xdc, 0x2a,
};

/* Length of the default RSS hash key. */
const size_t rss_hash_default_key_len = sizeof(rss_hash_default_key);

/**
 * Populate flow steering rule for a given hash RX queue type using
 * information from hash_rxq_init[]. Nothing is written to flow_attr when
 * flow_attr_size is not large enough, but the required size is still returned.
 *
 * @param[in] priv
 *   Pointer to private structure.
 * @param[out] flow_attr
 *   Pointer to flow attribute structure to fill. Note that the allocated
 *   area must be larger and large enough to hold all flow specifications.
 * @param flow_attr_size
 *   Entire size of flow_attr and trailing room for flow specifications.
 * @param type
 *   Requested Hash RX queue type.
 *
 * @return
 *   Total size of the flow attribute buffer. No errors are defined.
 */
size_t
priv_populate_flow_attr(const struct priv *priv,
			struct ibv_flow_attr *flow_attr,
			size_t flow_attr_size,
			enum hash_rxq_type type)
{
	size_t offset = sizeof(*flow_attr);
	const struct hash_rxq_init *init = &hash_rxq_init[type];

	assert((size_t)type < elemof(hash_rxq_init));
	do {
		offset += init->flow_spec.hdr.size;
		init = init->underlayer;
	} while (init != NULL);
	if (offset > flow_attr_size)
		return offset;
	flow_attr_size = offset;
	init = &hash_rxq_init[type];
	*flow_attr = (struct ibv_flow_attr){
		.type = IBV_FLOW_ATTR_NORMAL,
		.priority = init->flow_priority,
		.num_of_specs = 0,
		.port = priv->port,
		.flags = 0,
	};
	do {
		offset -= init->flow_spec.hdr.size;
		memcpy((void *)((uintptr_t)flow_attr + offset),
		       &init->flow_spec,
		       init->flow_spec.hdr.size);
		++flow_attr->num_of_specs;
		init = init->underlayer;
	} while (init != NULL);
	return flow_attr_size;
}

/**
 * Return nearest power of two above input value.
 *
 * @param v
 *   Input value.
 *
 * @return
 *   Nearest power of two above input value.
 */
static unsigned int
log2above(unsigned int v)
{
	unsigned int l;
	unsigned int r;

	for (l = 0, r = 0; (v >> 1); ++l, v >>= 1)
		r |= (v & 1);
	return (l + r);
}

/**
 * Initialize RX hash queues and indirection table.
 *
 * @param priv
 *   Pointer to private structure.
 *
 * @return
 *   0 on success, errno value on failure.
 */
int
priv_create_hash_rxqs(struct priv *priv)
{
	/* If the requested number of WQs is not a power of two, use the
	 * maximum indirection table size for better balancing.
	 * The result is always rounded to the next power of two. */
	unsigned int wqs_n =
		(1 << log2above((priv->rxqs_n & (priv->rxqs_n - 1)) ?
				priv->ind_table_max_size :
				priv->rxqs_n));
	struct ibv_exp_wq *wqs[wqs_n];
	/* If only one RX queue is configured, RSS is not needed. */
	const struct ind_table_init *const *ind_table_init =
		((priv->rxqs_n == 1) ?
		 ind_table_init_no_rss :
		 ind_table_init_rss);
	unsigned int hash_rxqs_n = 0;
	struct hash_rxq (*hash_rxqs)[] = NULL;
	unsigned int ind_tables_n = 0;
	struct ibv_exp_rwq_ind_table *(*ind_tables)[] = NULL;
	unsigned int i;
	unsigned int j;
	unsigned int k;
	int err = 0;

	assert(priv->ind_tables == NULL);
	assert(priv->ind_tables_n == 0);
	assert(priv->hash_rxqs == NULL);
	assert(priv->hash_rxqs_n == 0);
	assert(priv->pd != NULL);
	assert(priv->ctx != NULL);
	if (priv->rxqs_n == 0)
		return EINVAL;
	assert(priv->rxqs != NULL);
	if ((wqs_n < priv->rxqs_n) || (wqs_n > priv->ind_table_max_size)) {
		ERROR("cannot handle this many RX queues (%u)", priv->rxqs_n);
		err = ERANGE;
		goto error;
	}
	if (wqs_n != priv->rxqs_n) {
		INFO("%u RX queues are configured, consider rounding this"
		     " number to the next power of two for better balancing",
		     priv->rxqs_n);
		DEBUG("indirection table extended to assume %u WQs", wqs_n);
	}
	/* When the number of RX queues is not a power of two, the remaining
	 * table entries are padded with reused WQs and hashes are not spread
	 * uniformly. */
	for (i = 0, j = 0; (i != wqs_n); ++i) {
		wqs[i] = (*priv->rxqs)[j]->wq;
		if (++j == priv->rxqs_n)
			j = 0;
	}
	/* Get number of indirection tables and hash RX queues to configure. */
	for (ind_tables_n = 0, hash_rxqs_n = 0;
	     (ind_table_init[ind_tables_n] != NULL);
	     ++ind_tables_n)
		hash_rxqs_n += ind_table_init[ind_tables_n]->hash_types_n;
	DEBUG("allocating %u RX hash queues for %u WQs, %u indirection tables",
	      hash_rxqs_n, priv->rxqs_n, ind_tables_n);
	/* Create indirection tables. */
	ind_tables = rte_calloc(__func__, ind_tables_n,
				sizeof((*ind_tables)[0]), 0);
	if (ind_tables == NULL) {
		err = ENOMEM;
		ERROR("cannot allocate indirection tables container: %s",
		      strerror(err));
		goto error;
	}
	for (i = 0; (i != ind_tables_n); ++i) {
		struct ibv_exp_rwq_ind_table_init_attr ind_init_attr = {
			.pd = priv->pd,
			.log_ind_tbl_size = 0, /* Set below. */
			.ind_tbl = wqs,
			.comp_mask = 0,
		};
		unsigned int ind_tbl_size = ind_table_init[i]->max_size;
		struct ibv_exp_rwq_ind_table *ind_table;

		if (wqs_n < ind_tbl_size)
			ind_tbl_size = wqs_n;
		ind_init_attr.log_ind_tbl_size = log2above(ind_tbl_size);
		errno = 0;
		ind_table = ibv_exp_create_rwq_ind_table(priv->ctx,
							 &ind_init_attr);
		if (ind_table != NULL) {
			(*ind_tables)[i] = ind_table;
			continue;
		}
		/* Not clear whether errno is set. */
		err = (errno ? errno : EINVAL);
		ERROR("RX indirection table creation failed with error %d: %s",
		      err, strerror(err));
		goto error;
	}
	/* Allocate array that holds hash RX queues and related data. */
	hash_rxqs = rte_calloc(__func__, hash_rxqs_n,
			       sizeof((*hash_rxqs)[0]), 0);
	if (hash_rxqs == NULL) {
		err = ENOMEM;
		ERROR("cannot allocate hash RX queues container: %s",
		      strerror(err));
		goto error;
	}
	for (i = 0, j = 0, k = 0;
	     ((i != hash_rxqs_n) && (j != ind_tables_n));
	     ++i) {
		struct hash_rxq *hash_rxq = &(*hash_rxqs)[i];
		enum hash_rxq_type type = (*ind_table_init[j]->hash_types)[k];
		struct rte_eth_rss_conf *priv_rss_conf =
			(*priv->rss_conf)[type];
		struct ibv_exp_rx_hash_conf hash_conf = {
			.rx_hash_function = IBV_EXP_RX_HASH_FUNC_TOEPLITZ,
			.rx_hash_key_len = (priv_rss_conf ?
					    priv_rss_conf->rss_key_len :
					    rss_hash_default_key_len),
			.rx_hash_key = (priv_rss_conf ?
					priv_rss_conf->rss_key :
					rss_hash_default_key),
			.rx_hash_fields_mask = hash_rxq_init[type].hash_fields,
			.rwq_ind_tbl = (*ind_tables)[j],
		};
		struct ibv_exp_qp_init_attr qp_init_attr = {
			.max_inl_recv = 0, /* Currently not supported. */
			.qp_type = IBV_QPT_RAW_PACKET,
			.comp_mask = (IBV_EXP_QP_INIT_ATTR_PD |
				      IBV_EXP_QP_INIT_ATTR_RX_HASH),
			.pd = priv->pd,
			.rx_hash_conf = &hash_conf,
			.port_num = priv->port,
		};

		DEBUG("using indirection table %u for RX hash queue %u",
		      j, i);
		*hash_rxq = (struct hash_rxq){
			.priv = priv,
			.qp = ibv_exp_create_qp(priv->ctx, &qp_init_attr),
			.type = type,
		};
		if (hash_rxq->qp == NULL) {
			err = (errno ? errno : EINVAL);
			ERROR("RX hash QP creation failure: %s",
			      strerror(err));
			goto error;
		}
		if (++k < ind_table_init[j]->hash_types_n)
			continue;
		/* Switch to the next indirection table and reset hash RX
		 * queue type array index. */
		++j;
		k = 0;
	}
	priv->ind_tables = ind_tables;
	priv->ind_tables_n = ind_tables_n;
	priv->hash_rxqs = hash_rxqs;
	priv->hash_rxqs_n = hash_rxqs_n;
	assert(err == 0);
	return 0;
error:
	if (hash_rxqs != NULL) {
		for (i = 0; (i != hash_rxqs_n); ++i) {
			struct ibv_qp *qp = (*hash_rxqs)[i].qp;

			if (qp == NULL)
				continue;
			claim_zero(ibv_destroy_qp(qp));
		}
		rte_free(hash_rxqs);
	}
	if (ind_tables != NULL) {
		for (j = 0; (j != ind_tables_n); ++j) {
			struct ibv_exp_rwq_ind_table *ind_table =
				(*ind_tables)[j];

			if (ind_table == NULL)
				continue;
			claim_zero(ibv_exp_destroy_rwq_ind_table(ind_table));
		}
		rte_free(ind_tables);
	}
	return err;
}

/**
 * Clean up RX hash queues and indirection table.
 *
 * @param priv
 *   Pointer to private structure.
 */
void
priv_destroy_hash_rxqs(struct priv *priv)
{
	unsigned int i;

	DEBUG("destroying %u RX hash queues", priv->hash_rxqs_n);
	if (priv->hash_rxqs_n == 0) {
		assert(priv->hash_rxqs == NULL);
		assert(priv->ind_tables == NULL);
		return;
	}
	for (i = 0; (i != priv->hash_rxqs_n); ++i) {
		struct hash_rxq *hash_rxq = &(*priv->hash_rxqs)[i];
		unsigned int j, k;

		assert(hash_rxq->priv == priv);
		assert(hash_rxq->qp != NULL);
		/* Also check that there are no remaining flows. */
		assert(hash_rxq->allmulti_flow == NULL);
		assert(hash_rxq->promisc_flow == NULL);
		for (j = 0; (j != elemof(hash_rxq->mac_flow)); ++j)
			for (k = 0; (k != elemof(hash_rxq->mac_flow[j])); ++k)
				assert(hash_rxq->mac_flow[j][k] == NULL);
		claim_zero(ibv_destroy_qp(hash_rxq->qp));
	}
	priv->hash_rxqs_n = 0;
	rte_free(priv->hash_rxqs);
	priv->hash_rxqs = NULL;
	for (i = 0; (i != priv->ind_tables_n); ++i) {
		struct ibv_exp_rwq_ind_table *ind_table =
			(*priv->ind_tables)[i];

		assert(ind_table != NULL);
		claim_zero(ibv_exp_destroy_rwq_ind_table(ind_table));
	}
	priv->ind_tables_n = 0;
	rte_free(priv->ind_tables);
	priv->ind_tables = NULL;
}

/**
 * Allocate RX queue elements with scattered packets support.
 *
 * @param rxq
 *   Pointer to RX queue structure.
 * @param elts_n
 *   Number of elements to allocate.
 * @param[in] pool
 *   If not NULL, fetch buffers from this array instead of allocating them
 *   with rte_pktmbuf_alloc().
 *
 * @return
 *   0 on success, errno value on failure.
 */
static int
rxq_alloc_elts_sp(struct rxq *rxq, unsigned int elts_n,
		  struct rte_mbuf **pool)
{
	unsigned int i;
	struct rxq_elt_sp (*elts)[elts_n] =
		rte_calloc_socket("RXQ elements", 1, sizeof(*elts), 0,
				  rxq->socket);
	int ret = 0;

	if (elts == NULL) {
		ERROR("%p: can't allocate packets array", (void *)rxq);
		ret = ENOMEM;
		goto error;
	}
	/* For each WR (packet). */
	for (i = 0; (i != elts_n); ++i) {
		unsigned int j;
		struct rxq_elt_sp *elt = &(*elts)[i];
		struct ibv_sge (*sges)[(elemof(elt->sges))] = &elt->sges;

		/* These two arrays must have the same size. */
		assert(elemof(elt->sges) == elemof(elt->bufs));
		/* For each SGE (segment). */
		for (j = 0; (j != elemof(elt->bufs)); ++j) {
			struct ibv_sge *sge = &(*sges)[j];
			struct rte_mbuf *buf;

			if (pool != NULL) {
				buf = *(pool++);
				assert(buf != NULL);
				rte_pktmbuf_reset(buf);
			} else
				buf = rte_pktmbuf_alloc(rxq->mp);
			if (buf == NULL) {
				assert(pool == NULL);
				ERROR("%p: empty mbuf pool", (void *)rxq);
				ret = ENOMEM;
				goto error;
			}
			elt->bufs[j] = buf;
			/* Headroom is reserved by rte_pktmbuf_alloc(). */
			assert(DATA_OFF(buf) == RTE_PKTMBUF_HEADROOM);
			/* Buffer is supposed to be empty. */
			assert(rte_pktmbuf_data_len(buf) == 0);
			assert(rte_pktmbuf_pkt_len(buf) == 0);
			/* sge->addr must be able to store a pointer. */
			assert(sizeof(sge->addr) >= sizeof(uintptr_t));
			if (j == 0) {
				/* The first SGE keeps its headroom. */
				sge->addr = rte_pktmbuf_mtod(buf, uintptr_t);
				sge->length = (buf->buf_len -
					       RTE_PKTMBUF_HEADROOM);
			} else {
				/* Subsequent SGEs lose theirs. */
				assert(DATA_OFF(buf) == RTE_PKTMBUF_HEADROOM);
				SET_DATA_OFF(buf, 0);
				sge->addr = (uintptr_t)buf->buf_addr;
				sge->length = buf->buf_len;
			}
			sge->lkey = rxq->mr->lkey;
			/* Redundant check for tailroom. */
			assert(sge->length == rte_pktmbuf_tailroom(buf));
		}
	}
	DEBUG("%p: allocated and configured %u WRs (%zu segments)",
	      (void *)rxq, elts_n, (elts_n * elemof((*elts)[0].sges)));
	rxq->elts_n = elts_n;
	rxq->elts_head = 0;
	rxq->elts.sp = elts;
	assert(ret == 0);
	return 0;
error:
	if (elts != NULL) {
		assert(pool == NULL);
		for (i = 0; (i != elemof(*elts)); ++i) {
			unsigned int j;
			struct rxq_elt_sp *elt = &(*elts)[i];

			for (j = 0; (j != elemof(elt->bufs)); ++j) {
				struct rte_mbuf *buf = elt->bufs[j];

				if (buf != NULL)
					rte_pktmbuf_free_seg(buf);
			}
		}
		rte_free(elts);
	}
	DEBUG("%p: failed, freed everything", (void *)rxq);
	assert(ret > 0);
	return ret;
}

/**
 * Free RX queue elements with scattered packets support.
 *
 * @param rxq
 *   Pointer to RX queue structure.
 */
static void
rxq_free_elts_sp(struct rxq *rxq)
{
	unsigned int i;
	unsigned int elts_n = rxq->elts_n;
	struct rxq_elt_sp (*elts)[elts_n] = rxq->elts.sp;

	DEBUG("%p: freeing WRs", (void *)rxq);
	rxq->elts_n = 0;
	rxq->elts.sp = NULL;
	if (elts == NULL)
		return;
	for (i = 0; (i != elemof(*elts)); ++i) {
		unsigned int j;
		struct rxq_elt_sp *elt = &(*elts)[i];

		for (j = 0; (j != elemof(elt->bufs)); ++j) {
			struct rte_mbuf *buf = elt->bufs[j];

			if (buf != NULL)
				rte_pktmbuf_free_seg(buf);
		}
	}
	rte_free(elts);
}

/**
 * Allocate RX queue elements.
 *
 * @param rxq
 *   Pointer to RX queue structure.
 * @param elts_n
 *   Number of elements to allocate.
 * @param[in] pool
 *   If not NULL, fetch buffers from this array instead of allocating them
 *   with rte_pktmbuf_alloc().
 *
 * @return
 *   0 on success, errno value on failure.
 */
static int
rxq_alloc_elts(struct rxq *rxq, unsigned int elts_n, struct rte_mbuf **pool)
{
	unsigned int i;
	struct rxq_elt (*elts)[elts_n] =
		rte_calloc_socket("RXQ elements", 1, sizeof(*elts), 0,
				  rxq->socket);
	int ret = 0;

	if (elts == NULL) {
		ERROR("%p: can't allocate packets array", (void *)rxq);
		ret = ENOMEM;
		goto error;
	}
	/* For each WR (packet). */
	for (i = 0; (i != elts_n); ++i) {
		struct rxq_elt *elt = &(*elts)[i];
		struct ibv_sge *sge = &(*elts)[i].sge;
		struct rte_mbuf *buf;

		if (pool != NULL) {
			buf = *(pool++);
			assert(buf != NULL);
			rte_pktmbuf_reset(buf);
		} else
			buf = rte_pktmbuf_alloc(rxq->mp);
		if (buf == NULL) {
			assert(pool == NULL);
			ERROR("%p: empty mbuf pool", (void *)rxq);
			ret = ENOMEM;
			goto error;
		}
		elt->buf = buf;
		/* Headroom is reserved by rte_pktmbuf_alloc(). */
		assert(DATA_OFF(buf) == RTE_PKTMBUF_HEADROOM);
		/* Buffer is supposed to be empty. */
		assert(rte_pktmbuf_data_len(buf) == 0);
		assert(rte_pktmbuf_pkt_len(buf) == 0);
		/* sge->addr must be able to store a pointer. */
		assert(sizeof(sge->addr) >= sizeof(uintptr_t));
		/* SGE keeps its headroom. */
		sge->addr = (uintptr_t)
			((uint8_t *)buf->buf_addr + RTE_PKTMBUF_HEADROOM);
		sge->length = (buf->buf_len - RTE_PKTMBUF_HEADROOM);
		sge->lkey = rxq->mr->lkey;
		/* Redundant check for tailroom. */
		assert(sge->length == rte_pktmbuf_tailroom(buf));
	}
	DEBUG("%p: allocated and configured %u single-segment WRs",
	      (void *)rxq, elts_n);
	rxq->elts_n = elts_n;
	rxq->elts_head = 0;
	rxq->elts.no_sp = elts;
	assert(ret == 0);
	return 0;
error:
	if (elts != NULL) {
		assert(pool == NULL);
		for (i = 0; (i != elemof(*elts)); ++i) {
			struct rxq_elt *elt = &(*elts)[i];
			struct rte_mbuf *buf = elt->buf;

			if (buf != NULL)
				rte_pktmbuf_free_seg(buf);
		}
		rte_free(elts);
	}
	DEBUG("%p: failed, freed everything", (void *)rxq);
	assert(ret > 0);
	return ret;
}

/**
 * Free RX queue elements.
 *
 * @param rxq
 *   Pointer to RX queue structure.
 */
static void
rxq_free_elts(struct rxq *rxq)
{
	unsigned int i;
	unsigned int elts_n = rxq->elts_n;
	struct rxq_elt (*elts)[elts_n] = rxq->elts.no_sp;

	DEBUG("%p: freeing WRs", (void *)rxq);
	rxq->elts_n = 0;
	rxq->elts.no_sp = NULL;
	if (elts == NULL)
		return;
	for (i = 0; (i != elemof(*elts)); ++i) {
		struct rxq_elt *elt = &(*elts)[i];
		struct rte_mbuf *buf = elt->buf;

		if (buf != NULL)
			rte_pktmbuf_free_seg(buf);
	}
	rte_free(elts);
}

/**
 * Clean up a RX queue.
 *
 * Destroy objects, free allocated memory and reset the structure for reuse.
 *
 * @param rxq
 *   Pointer to RX queue structure.
 */
void
rxq_cleanup(struct rxq *rxq)
{
	struct ibv_exp_release_intf_params params;

	DEBUG("cleaning up %p", (void *)rxq);
	if (rxq->sp)
		rxq_free_elts_sp(rxq);
	else
		rxq_free_elts(rxq);
	if (rxq->if_wq != NULL) {
		assert(rxq->priv != NULL);
		assert(rxq->priv->ctx != NULL);
		assert(rxq->wq != NULL);
		params = (struct ibv_exp_release_intf_params){
			.comp_mask = 0,
		};
		claim_zero(ibv_exp_release_intf(rxq->priv->ctx,
						rxq->if_wq,
						&params));
	}
	if (rxq->if_cq != NULL) {
		assert(rxq->priv != NULL);
		assert(rxq->priv->ctx != NULL);
		assert(rxq->cq != NULL);
		params = (struct ibv_exp_release_intf_params){
			.comp_mask = 0,
		};
		claim_zero(ibv_exp_release_intf(rxq->priv->ctx,
						rxq->if_cq,
						&params));
	}
	if (rxq->wq != NULL)
		claim_zero(ibv_exp_destroy_wq(rxq->wq));
	if (rxq->cq != NULL)
		claim_zero(ibv_destroy_cq(rxq->cq));
	if (rxq->rd != NULL) {
		struct ibv_exp_destroy_res_domain_attr attr = {
			.comp_mask = 0,
		};

		assert(rxq->priv != NULL);
		assert(rxq->priv->ctx != NULL);
		claim_zero(ibv_exp_destroy_res_domain(rxq->priv->ctx,
						      rxq->rd,
						      &attr));
	}
	if (rxq->mr != NULL)
		claim_zero(ibv_dereg_mr(rxq->mr));
	memset(rxq, 0, sizeof(*rxq));
}

/**
 * Reconfigure a RX queue with new parameters.
 *
 * rxq_rehash() does not allocate mbufs, which, if not done from the right
 * thread (such as a control thread), may corrupt the pool.
 * In case of failure, the queue is left untouched.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param rxq
 *   RX queue pointer.
 *
 * @return
 *   0 on success, errno value on failure.
 */
int
rxq_rehash(struct rte_eth_dev *dev, struct rxq *rxq)
{
	struct priv *priv = rxq->priv;
	struct rxq tmpl = *rxq;
	unsigned int mbuf_n;
	unsigned int desc_n;
	struct rte_mbuf **pool;
	unsigned int i, k;
	struct ibv_exp_wq_attr mod;
	int err;

	DEBUG("%p: rehashing queue %p", (void *)dev, (void *)rxq);
	/* Number of descriptors and mbufs currently allocated. */
	desc_n = (tmpl.elts_n * (tmpl.sp ? MLX5_PMD_SGE_WR_N : 1));
	mbuf_n = desc_n;
	/* Toggle RX checksum offload if hardware supports it. */
	if (priv->hw_csum) {
		tmpl.csum = !!dev->data->dev_conf.rxmode.hw_ip_checksum;
		rxq->csum = tmpl.csum;
	}
	if (priv->hw_csum_l2tun) {
		tmpl.csum_l2tun = !!dev->data->dev_conf.rxmode.hw_ip_checksum;
		rxq->csum_l2tun = tmpl.csum_l2tun;
	}
	/* Enable scattered packets support for this queue if necessary. */
	if ((dev->data->dev_conf.rxmode.jumbo_frame) &&
	    (dev->data->dev_conf.rxmode.max_rx_pkt_len >
	     (tmpl.mb_len - RTE_PKTMBUF_HEADROOM))) {
		tmpl.sp = 1;
		desc_n /= MLX5_PMD_SGE_WR_N;
	} else
		tmpl.sp = 0;
	DEBUG("%p: %s scattered packets support (%u WRs)",
	      (void *)dev, (tmpl.sp ? "enabling" : "disabling"), desc_n);
	/* If scatter mode is the same as before, nothing to do. */
	if (tmpl.sp == rxq->sp) {
		DEBUG("%p: nothing to do", (void *)dev);
		return 0;
	}
	/* From now on, any failure will render the queue unusable.
	 * Reinitialize WQ. */
	mod = (struct ibv_exp_wq_attr){
		.attr_mask = IBV_EXP_WQ_ATTR_STATE,
		.wq_state = IBV_EXP_WQS_RESET,
	};
	err = ibv_exp_modify_wq(tmpl.wq, &mod);
	if (err) {
		ERROR("%p: cannot reset WQ: %s", (void *)dev, strerror(err));
		assert(err > 0);
		return err;
	}
	err = ibv_resize_cq(tmpl.cq, desc_n);
	if (err) {
		ERROR("%p: cannot resize CQ: %s", (void *)dev, strerror(err));
		assert(err > 0);
		return err;
	}
	/* Allocate pool. */
	pool = rte_malloc(__func__, (mbuf_n * sizeof(*pool)), 0);
	if (pool == NULL) {
		ERROR("%p: cannot allocate memory", (void *)dev);
		return ENOBUFS;
	}
	/* Snatch mbufs from original queue. */
	k = 0;
	if (rxq->sp) {
		struct rxq_elt_sp (*elts)[rxq->elts_n] = rxq->elts.sp;

		for (i = 0; (i != elemof(*elts)); ++i) {
			struct rxq_elt_sp *elt = &(*elts)[i];
			unsigned int j;

			for (j = 0; (j != elemof(elt->bufs)); ++j) {
				assert(elt->bufs[j] != NULL);
				pool[k++] = elt->bufs[j];
			}
		}
	} else {
		struct rxq_elt (*elts)[rxq->elts_n] = rxq->elts.no_sp;

		for (i = 0; (i != elemof(*elts)); ++i) {
			struct rxq_elt *elt = &(*elts)[i];
			struct rte_mbuf *buf = elt->buf;

			pool[k++] = buf;
		}
	}
	assert(k == mbuf_n);
	tmpl.elts_n = 0;
	tmpl.elts.sp = NULL;
	assert((void *)&tmpl.elts.sp == (void *)&tmpl.elts.no_sp);
	err = ((tmpl.sp) ?
	       rxq_alloc_elts_sp(&tmpl, desc_n, pool) :
	       rxq_alloc_elts(&tmpl, desc_n, pool));
	if (err) {
		ERROR("%p: cannot reallocate WRs, aborting", (void *)dev);
		rte_free(pool);
		assert(err > 0);
		return err;
	}
	assert(tmpl.elts_n == desc_n);
	assert(tmpl.elts.sp != NULL);
	rte_free(pool);
	/* Clean up original data. */
	rxq->elts_n = 0;
	rte_free(rxq->elts.sp);
	rxq->elts.sp = NULL;
	/* Change queue state to ready. */
	mod = (struct ibv_exp_wq_attr){
		.attr_mask = IBV_EXP_WQ_ATTR_STATE,
		.wq_state = IBV_EXP_WQS_RDY,
	};
	err = ibv_exp_modify_wq(tmpl.wq, &mod);
	if (err) {
		ERROR("%p: WQ state to IBV_EXP_WQS_RDY failed: %s",
		      (void *)dev, strerror(err));
		goto error;
	}
	/* Post SGEs. */
	assert(tmpl.if_wq != NULL);
	if (tmpl.sp) {
		struct rxq_elt_sp (*elts)[rxq->elts_n] = tmpl.elts.sp;

		for (i = 0; (i != elemof(*elts)); ++i) {
			err = tmpl.if_wq->recv_sg_list
				(tmpl.wq,
				 (*elts)[i].sges,
				 elemof((*elts)[i].sges));
			if (err)
				break;
		}
	} else {
		struct rxq_elt (*elts)[rxq->elts_n] = tmpl.elts.no_sp;

		for (i = 0; (i != elemof(*elts)); ++i) {
			err = tmpl.if_wq->recv_burst(
				tmpl.wq,
				&(*elts)[i].sge,
				1);
			if (err)
				break;
		}
	}
	if (err) {
		ERROR("%p: failed to post SGEs with error %d",
		      (void *)dev, err);
		/* Set err because it does not contain a valid errno value. */
		err = EIO;
		goto error;
	}
error:
	*rxq = tmpl;
	assert(err >= 0);
	return err;
}

/**
 * Configure a RX queue.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param rxq
 *   Pointer to RX queue structure.
 * @param desc
 *   Number of descriptors to configure in queue.
 * @param socket
 *   NUMA socket on which memory must be allocated.
 * @param[in] conf
 *   Thresholds parameters.
 * @param mp
 *   Memory pool for buffer allocations.
 *
 * @return
 *   0 on success, errno value on failure.
 */
int
rxq_setup(struct rte_eth_dev *dev, struct rxq *rxq, uint16_t desc,
	  unsigned int socket, const struct rte_eth_rxconf *conf,
	  struct rte_mempool *mp)
{
	struct priv *priv = dev->data->dev_private;
	struct rxq tmpl = {
		.priv = priv,
		.mp = mp,
		.socket = socket
	};
	struct ibv_exp_wq_attr mod;
	union {
		struct ibv_exp_query_intf_params params;
		struct ibv_exp_cq_init_attr cq;
		struct ibv_exp_res_domain_init_attr rd;
		struct ibv_exp_wq_init_attr wq;
	} attr;
	enum ibv_exp_query_intf_status status;
	struct rte_mbuf *buf;
	int ret = 0;
	unsigned int i;

	(void)conf; /* Thresholds configuration (ignored). */
	if ((desc == 0) || (desc % MLX5_PMD_SGE_WR_N)) {
		ERROR("%p: invalid number of RX descriptors (must be a"
		      " multiple of %d)", (void *)dev, MLX5_PMD_SGE_WR_N);
		return EINVAL;
	}
	/* Get mbuf length. */
	buf = rte_pktmbuf_alloc(mp);
	if (buf == NULL) {
		ERROR("%p: unable to allocate mbuf", (void *)dev);
		return ENOMEM;
	}
	tmpl.mb_len = buf->buf_len;
	assert((rte_pktmbuf_headroom(buf) +
		rte_pktmbuf_tailroom(buf)) == tmpl.mb_len);
	assert(rte_pktmbuf_headroom(buf) == RTE_PKTMBUF_HEADROOM);
	rte_pktmbuf_free(buf);
	/* Toggle RX checksum offload if hardware supports it. */
	if (priv->hw_csum)
		tmpl.csum = !!dev->data->dev_conf.rxmode.hw_ip_checksum;
	if (priv->hw_csum_l2tun)
		tmpl.csum_l2tun = !!dev->data->dev_conf.rxmode.hw_ip_checksum;
	/* Enable scattered packets support for this queue if necessary. */
	if ((dev->data->dev_conf.rxmode.jumbo_frame) &&
	    (dev->data->dev_conf.rxmode.max_rx_pkt_len >
	     (tmpl.mb_len - RTE_PKTMBUF_HEADROOM))) {
		tmpl.sp = 1;
		desc /= MLX5_PMD_SGE_WR_N;
	}
	DEBUG("%p: %s scattered packets support (%u WRs)",
	      (void *)dev, (tmpl.sp ? "enabling" : "disabling"), desc);
	/* Use the entire RX mempool as the memory region. */
	tmpl.mr = ibv_reg_mr(priv->pd,
			     (void *)mp->elt_va_start,
			     (mp->elt_va_end - mp->elt_va_start),
			     (IBV_ACCESS_LOCAL_WRITE |
			      IBV_ACCESS_REMOTE_WRITE));
	if (tmpl.mr == NULL) {
		ret = EINVAL;
		ERROR("%p: MR creation failure: %s",
		      (void *)dev, strerror(ret));
		goto error;
	}
	attr.rd = (struct ibv_exp_res_domain_init_attr){
		.comp_mask = (IBV_EXP_RES_DOMAIN_THREAD_MODEL |
			      IBV_EXP_RES_DOMAIN_MSG_MODEL),
		.thread_model = IBV_EXP_THREAD_SINGLE,
		.msg_model = IBV_EXP_MSG_HIGH_BW,
	};
	tmpl.rd = ibv_exp_create_res_domain(priv->ctx, &attr.rd);
	if (tmpl.rd == NULL) {
		ret = ENOMEM;
		ERROR("%p: RD creation failure: %s",
		      (void *)dev, strerror(ret));
		goto error;
	}
	attr.cq = (struct ibv_exp_cq_init_attr){
		.comp_mask = IBV_EXP_CQ_INIT_ATTR_RES_DOMAIN,
		.res_domain = tmpl.rd,
	};
	tmpl.cq = ibv_exp_create_cq(priv->ctx, desc, NULL, NULL, 0, &attr.cq);
	if (tmpl.cq == NULL) {
		ret = ENOMEM;
		ERROR("%p: CQ creation failure: %s",
		      (void *)dev, strerror(ret));
		goto error;
	}
	DEBUG("priv->device_attr.max_qp_wr is %d",
	      priv->device_attr.max_qp_wr);
	DEBUG("priv->device_attr.max_sge is %d",
	      priv->device_attr.max_sge);
	attr.wq = (struct ibv_exp_wq_init_attr){
		.wq_context = NULL, /* Could be useful in the future. */
		.wq_type = IBV_EXP_WQT_RQ,
		/* Max number of outstanding WRs. */
		.max_recv_wr = ((priv->device_attr.max_qp_wr < desc) ?
				priv->device_attr.max_qp_wr :
				desc),
		/* Max number of scatter/gather elements in a WR. */
		.max_recv_sge = ((priv->device_attr.max_sge <
				  MLX5_PMD_SGE_WR_N) ?
				 priv->device_attr.max_sge :
				 MLX5_PMD_SGE_WR_N),
		.pd = priv->pd,
		.cq = tmpl.cq,
		.comp_mask = IBV_EXP_CREATE_WQ_RES_DOMAIN,
		.res_domain = tmpl.rd,
	};
	tmpl.wq = ibv_exp_create_wq(priv->ctx, &attr.wq);
	if (tmpl.wq == NULL) {
		ret = (errno ? errno : EINVAL);
		ERROR("%p: WQ creation failure: %s",
		      (void *)dev, strerror(ret));
		goto error;
	}
	if (tmpl.sp)
		ret = rxq_alloc_elts_sp(&tmpl, desc, NULL);
	else
		ret = rxq_alloc_elts(&tmpl, desc, NULL);
	if (ret) {
		ERROR("%p: RXQ allocation failed: %s",
		      (void *)dev, strerror(ret));
		goto error;
	}
	/* Save port ID. */
	tmpl.port_id = dev->data->port_id;
	DEBUG("%p: RTE port ID: %u", (void *)rxq, tmpl.port_id);
	attr.params = (struct ibv_exp_query_intf_params){
		.intf_scope = IBV_EXP_INTF_GLOBAL,
		.intf = IBV_EXP_INTF_CQ,
		.obj = tmpl.cq,
	};
	tmpl.if_cq = ibv_exp_query_intf(priv->ctx, &attr.params, &status);
	if (tmpl.if_cq == NULL) {
		ERROR("%p: CQ interface family query failed with status %d",
		      (void *)dev, status);
		goto error;
	}
	attr.params = (struct ibv_exp_query_intf_params){
		.intf_scope = IBV_EXP_INTF_GLOBAL,
		.intf = IBV_EXP_INTF_WQ,
		.obj = tmpl.wq,
	};
	tmpl.if_wq = ibv_exp_query_intf(priv->ctx, &attr.params, &status);
	if (tmpl.if_wq == NULL) {
		ERROR("%p: WQ interface family query failed with status %d",
		      (void *)dev, status);
		goto error;
	}
	/* Change queue state to ready. */
	mod = (struct ibv_exp_wq_attr){
		.attr_mask = IBV_EXP_WQ_ATTR_STATE,
		.wq_state = IBV_EXP_WQS_RDY,
	};
	ret = ibv_exp_modify_wq(tmpl.wq, &mod);
	if (ret) {
		ERROR("%p: WQ state to IBV_EXP_WQS_RDY failed: %s",
		      (void *)dev, strerror(ret));
		goto error;
	}
	/* Post SGEs. */
	if (tmpl.sp) {
		struct rxq_elt_sp (*elts)[tmpl.elts_n] = tmpl.elts.sp;

		for (i = 0; (i != elemof(*elts)); ++i) {
			ret = tmpl.if_wq->recv_sg_list
				(tmpl.wq,
				 (*elts)[i].sges,
				 elemof((*elts)[i].sges));
			if (ret)
				break;
		}
	} else {
		struct rxq_elt (*elts)[tmpl.elts_n] = tmpl.elts.no_sp;

		for (i = 0; (i != elemof(*elts)); ++i) {
			ret = tmpl.if_wq->recv_burst(
				tmpl.wq,
				&(*elts)[i].sge,
				1);
			if (ret)
				break;
		}
	}
	if (ret) {
		ERROR("%p: failed to post SGEs with error %d",
		      (void *)dev, ret);
		/* Set ret because it does not contain a valid errno value. */
		ret = EIO;
		goto error;
	}
	/* Clean up rxq in case we're reinitializing it. */
	DEBUG("%p: cleaning-up old rxq just in case", (void *)rxq);
	rxq_cleanup(rxq);
	*rxq = tmpl;
	DEBUG("%p: rxq updated with %p", (void *)rxq, (void *)&tmpl);
	assert(ret == 0);
	return 0;
error:
	rxq_cleanup(&tmpl);
	assert(ret > 0);
	return ret;
}

/**
 * DPDK callback to configure a RX queue.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param idx
 *   RX queue index.
 * @param desc
 *   Number of descriptors to configure in queue.
 * @param socket
 *   NUMA socket on which memory must be allocated.
 * @param[in] conf
 *   Thresholds parameters.
 * @param mp
 *   Memory pool for buffer allocations.
 *
 * @return
 *   0 on success, negative errno value on failure.
 */
int
mlx5_rx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
		    unsigned int socket, const struct rte_eth_rxconf *conf,
		    struct rte_mempool *mp)
{
	struct priv *priv = dev->data->dev_private;
	struct rxq *rxq = (*priv->rxqs)[idx];
	int ret;

	priv_lock(priv);
	DEBUG("%p: configuring queue %u for %u descriptors",
	      (void *)dev, idx, desc);
	if (idx >= priv->rxqs_n) {
		ERROR("%p: queue index out of range (%u >= %u)",
		      (void *)dev, idx, priv->rxqs_n);
		priv_unlock(priv);
		return -EOVERFLOW;
	}
	if (rxq != NULL) {
		DEBUG("%p: reusing already allocated queue index %u (%p)",
		      (void *)dev, idx, (void *)rxq);
		if (priv->started) {
			priv_unlock(priv);
			return -EEXIST;
		}
		(*priv->rxqs)[idx] = NULL;
		rxq_cleanup(rxq);
	} else {
		rxq = rte_calloc_socket("RXQ", 1, sizeof(*rxq), 0, socket);
		if (rxq == NULL) {
			ERROR("%p: unable to allocate queue index %u",
			      (void *)dev, idx);
			priv_unlock(priv);
			return -ENOMEM;
		}
	}
	ret = rxq_setup(dev, rxq, desc, socket, conf, mp);
	if (ret)
		rte_free(rxq);
	else {
		rxq->stats.idx = idx;
		DEBUG("%p: adding RX queue %p to list",
		      (void *)dev, (void *)rxq);
		(*priv->rxqs)[idx] = rxq;
		/* Update receive callback. */
		if (rxq->sp)
			dev->rx_pkt_burst = mlx5_rx_burst_sp;
		else
			dev->rx_pkt_burst = mlx5_rx_burst;
	}
	priv_unlock(priv);
	return -ret;
}

/**
 * DPDK callback to release a RX queue.
 *
 * @param dpdk_rxq
 *   Generic RX queue pointer.
 */
void
mlx5_rx_queue_release(void *dpdk_rxq)
{
	struct rxq *rxq = (struct rxq *)dpdk_rxq;
	struct priv *priv;
	unsigned int i;

	if (rxq == NULL)
		return;
	priv = rxq->priv;
	priv_lock(priv);
	for (i = 0; (i != priv->rxqs_n); ++i)
		if ((*priv->rxqs)[i] == rxq) {
			DEBUG("%p: removing RX queue %p from list",
			      (void *)priv->dev, (void *)rxq);
			(*priv->rxqs)[i] = NULL;
			break;
		}
	rxq_cleanup(rxq);
	rte_free(rxq);
	priv_unlock(priv);
}
