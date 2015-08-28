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
#include <errno.h>
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
#include <rte_ethdev.h>
#ifdef PEDANTIC
#pragma GCC diagnostic error "-pedantic"
#endif

#include "mlx5.h"
#include "mlx5_rxtx.h"
#include "mlx5_utils.h"

static void hash_rxq_promiscuous_disable(struct hash_rxq *);
static void hash_rxq_allmulticast_disable(struct hash_rxq *);

/**
 * Enable promiscuous mode in a RX hash queue.
 *
 * @param hash_rxq
 *   Pointer to RX hash queue structure.
 *
 * @return
 *   0 on success, errno value on failure.
 */
static int
hash_rxq_promiscuous_enable(struct hash_rxq *hash_rxq)
{
	struct ibv_exp_flow *flow;
	struct priv *priv = hash_rxq->priv;
	FLOW_ATTR_SPEC_ETH(data, priv_populate_flow_attr(priv, NULL, 0,
							 hash_rxq->type));
	struct ibv_exp_flow_attr *attr = &data->attr;

	if (hash_rxq->priv->vf)
		return 0;
	DEBUG("%p: enabling promiscuous mode", (void *)hash_rxq);
	if (hash_rxq->promisc_flow != NULL)
		return EBUSY;
	/* Promiscuous flows only differ from normal flows by not filtering
	 * on specific MAC addresses. */
	priv_populate_flow_attr(priv, attr, sizeof(data), hash_rxq->type);
	errno = 0;
	flow = ibv_exp_create_flow(hash_rxq->qp, attr);
	if (flow == NULL) {
		/* It's not clear whether errno is always set in this case. */
		ERROR("%p: flow configuration failed, errno=%d: %s",
		      (void *)hash_rxq, errno,
		      (errno ? strerror(errno) : "Unknown error"));
		if (errno)
			return errno;
		return EINVAL;
	}
	hash_rxq->promisc_flow = flow;
	DEBUG("%p: promiscuous mode enabled", (void *)hash_rxq);
	return 0;
}

/**
 * Enable promiscuous mode in all RX hash queues.
 *
 * @param priv
 *   Private structure.
 *
 * @return
 *   0 on success, errno value on failure.
 */
int
priv_promiscuous_enable(struct priv *priv)
{
	unsigned int i;

	if (priv->promisc)
		return 0;
	for (i = 0; (i != priv->hash_rxqs_n); ++i) {
		struct hash_rxq *hash_rxq = &(*priv->hash_rxqs)[i];
		int ret;

		/* Remove normal MAC flows first. */
		hash_rxq_mac_addrs_del(hash_rxq);
		ret = hash_rxq_promiscuous_enable(hash_rxq);
		if (!ret)
			continue;
		/* Failure, rollback. */
		while (i != 0) {
			hash_rxq = &(*priv->hash_rxqs)[--i];
			hash_rxq_promiscuous_disable(hash_rxq);
			/* Restore MAC flows. */
			if (priv->started)
				hash_rxq_mac_addrs_add(hash_rxq);
		}
		return ret;
	}
	priv->promisc = 1;
	return 0;
}

/**
 * DPDK callback to enable promiscuous mode.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
void
mlx5_promiscuous_enable(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	int ret;

	priv_lock(priv);
	priv->promisc_req = 1;
	ret = priv_promiscuous_enable(priv);
	if (ret)
		ERROR("cannot enable promiscuous mode: %s", strerror(ret));
	priv_unlock(priv);
}

/**
 * Disable promiscuous mode in a RX hash queue.
 *
 * @param hash_rxq
 *   Pointer to RX hash queue structure.
 */
static void
hash_rxq_promiscuous_disable(struct hash_rxq *hash_rxq)
{
	if (hash_rxq->priv->vf)
		return;
	DEBUG("%p: disabling promiscuous mode", (void *)hash_rxq);
	if (hash_rxq->promisc_flow == NULL)
		return;
	claim_zero(ibv_exp_destroy_flow(hash_rxq->promisc_flow));
	hash_rxq->promisc_flow = NULL;
	DEBUG("%p: promiscuous mode disabled", (void *)hash_rxq);
}

/**
 * Disable promiscuous mode in all RX hash queues.
 *
 * @param priv
 *   Private structure.
 */
void
priv_promiscuous_disable(struct priv *priv)
{
	unsigned int i;

	if (!priv->promisc)
		return;
	for (i = 0; (i != priv->hash_rxqs_n); ++i) {
		struct hash_rxq *hash_rxq = &(*priv->hash_rxqs)[i];

		hash_rxq_promiscuous_disable(hash_rxq);
		/* Restore MAC flows. */
		if (priv->started)
			hash_rxq_mac_addrs_add(hash_rxq);
	}
	priv->promisc = 0;
}

/**
 * DPDK callback to disable promiscuous mode.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
void
mlx5_promiscuous_disable(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;

	priv_lock(priv);
	priv->promisc_req = 0;
	priv_promiscuous_disable(priv);
	priv_unlock(priv);
}

/**
 * Enable allmulti mode in a RX hash queue.
 *
 * @param hash_rxq
 *   Pointer to RX hash queue structure.
 *
 * @return
 *   0 on success, errno value on failure.
 */
static int
hash_rxq_allmulticast_enable(struct hash_rxq *hash_rxq)
{
	struct ibv_exp_flow *flow;
	struct ibv_exp_flow_attr attr = {
		.type = IBV_EXP_FLOW_ATTR_MC_DEFAULT,
		.num_of_specs = 0,
		.port = hash_rxq->priv->port,
		.flags = 0
	};

	DEBUG("%p: enabling allmulticast mode", (void *)hash_rxq);
	if (hash_rxq->allmulti_flow != NULL)
		return EBUSY;
	errno = 0;
	flow = ibv_exp_create_flow(hash_rxq->qp, &attr);
	if (flow == NULL) {
		/* It's not clear whether errno is always set in this case. */
		ERROR("%p: flow configuration failed, errno=%d: %s",
		      (void *)hash_rxq, errno,
		      (errno ? strerror(errno) : "Unknown error"));
		if (errno)
			return errno;
		return EINVAL;
	}
	hash_rxq->allmulti_flow = flow;
	DEBUG("%p: allmulticast mode enabled", (void *)hash_rxq);
	return 0;
}

/**
 * Enable allmulti mode in all RX hash queues.
 *
 * @param priv
 *   Private structure.
 *
 * @return
 *   0 on success, errno value on failure.
 */
int
priv_allmulticast_enable(struct priv *priv)
{
	unsigned int i;

	if (priv->allmulti)
		return 0;
	for (i = 0; (i != priv->hash_rxqs_n); ++i) {
		struct hash_rxq *hash_rxq = &(*priv->hash_rxqs)[i];
		int ret;

		ret = hash_rxq_allmulticast_enable(hash_rxq);
		if (!ret)
			continue;
		/* Failure, rollback. */
		while (i != 0) {
			hash_rxq = &(*priv->hash_rxqs)[--i];
			hash_rxq_allmulticast_disable(hash_rxq);
		}
		return ret;
	}
	priv->allmulti = 1;
	return 0;
}

/**
 * DPDK callback to enable allmulti mode.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
void
mlx5_allmulticast_enable(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	int ret;

	priv_lock(priv);
	priv->allmulti_req = 1;
	ret = priv_allmulticast_enable(priv);
	if (ret)
		ERROR("cannot enable allmulticast mode: %s", strerror(ret));
	priv_unlock(priv);
}

/**
 * Disable allmulti mode in a RX hash queue.
 *
 * @param hash_rxq
 *   Pointer to RX hash queue structure.
 */
static void
hash_rxq_allmulticast_disable(struct hash_rxq *hash_rxq)
{
	DEBUG("%p: disabling allmulticast mode", (void *)hash_rxq);
	if (hash_rxq->allmulti_flow == NULL)
		return;
	claim_zero(ibv_exp_destroy_flow(hash_rxq->allmulti_flow));
	hash_rxq->allmulti_flow = NULL;
	DEBUG("%p: allmulticast mode disabled", (void *)hash_rxq);
}

/**
 * Disable allmulti mode in all RX hash queues.
 *
 * @param priv
 *   Private structure.
 */
void
priv_allmulticast_disable(struct priv *priv)
{
	unsigned int i;

	if (!priv->allmulti)
		return;
	for (i = 0; (i != priv->hash_rxqs_n); ++i)
		hash_rxq_allmulticast_disable(&(*priv->hash_rxqs)[i]);
	priv->allmulti = 0;
}

/**
 * DPDK callback to disable allmulti mode.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
void
mlx5_allmulticast_disable(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;

	priv_lock(priv);
	priv->allmulti_req = 0;
	priv_allmulticast_disable(priv);
	priv_unlock(priv);
}
