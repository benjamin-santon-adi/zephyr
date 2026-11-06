/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Definitions for Ethernet bridge FDB
 */

#ifndef ZEPHYR_INCLUDE_NET_ETHERNET_BRIDGE_FDB_H_
#define ZEPHYR_INCLUDE_NET_ETHERNET_BRIDGE_FDB_H_

/**
 * @brief Definitions for Ethernet bridge FDB
 * @defgroup eth_bridge_fdb Ethernet bridge FDB
 * @since 4.4
 * @version 0.2.0
 * @ingroup networking
 * @{
 */

#include <zephyr/net/net_if.h>
#include <zephyr/net/ethernet.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief FDB entry flags
 */
#define ETHERNET_BRIDGE_FDB_FLAG_STATIC  BIT(0) /**< Static entry (manually added) */
#define ETHERNET_BRIDGE_FDB_FLAG_DYNAMIC BIT(1) /**< Dynamic entry (learned) */
/** Entry with a port map, held by a device that forwards in hardware.
 *  Always static, and @ref eth_bridge_fdb_entry.iface is NULL.
 */
#define ETHERNET_BRIDGE_FDB_FLAG_HW      BIT(2)

/**
 * @brief FDB entry structure
 */
struct eth_bridge_fdb_entry {
	/** MAC address */
	struct net_eth_addr mac;
	/** Interface, NULL for an entry with @ref ETHERNET_BRIDGE_FDB_FLAG_HW */
	struct net_if *iface;
	/** Bridge interface the entry belongs to */
	struct net_if *bridge;
	/** Bridge ports the frames for @ref mac go to, bit n for the bridge port n.
	 *  Only for an entry with @ref ETHERNET_BRIDGE_FDB_FLAG_HW.
	 */
	uint32_t portmap;
	/** The frames for @ref mac are also passed to the host. Only for an
	 *  entry with @ref ETHERNET_BRIDGE_FDB_FLAG_HW.
	 */
	bool host_rx;
	/** Entry flags */
	uint8_t flags;
	/** Uptime in seconds when a frame from @ref mac was last received.
	 *  Used to age out dynamic entries.
	 */
	uint32_t last_seen;
	/** Linked list node */
	sys_snode_t node;
};

/**
 * @brief Add a static FDB entry
 *
 * On a port whose device reports a bridge set with eth_bridge_set_hw_forwarding() the entry is
 * also given to the hooks of the bridge, for the device to forward the frames for @p mac to the
 * ports of @p portmap and to the host if @p host_rx is set. All the ports of the
 * bridge must then be of such devices. The entry replaces the one the bridge
 * has for @p mac. The bridge does not learn into or forward the frames of those
 * ports itself, and a frame the host sends to @p mac only goes out of the
 * ports of @p portmap.
 *
 * @param mac MAC address
 * @param iface Interface, a port of the bridge
 * @param portmap Bridge ports the frames for @p mac go to, bit n for the
 *                bridge port n, which eth_bridge_get_port_iface() gives for n.
 *                0 for @p iface alone. Without a bridge set with eth_bridge_set_hw_forwarding()
 *                it must be 0 or @p iface alone.
 * @param host_rx The frames are also passed to the host. Needs
 *                a bridge set with eth_bridge_set_hw_forwarding().
 * @retval 0 on success
 * @retval -EINVAL A bad argument, or @p portmap has a bit of no port
 * @retval -ENOTSUP @p portmap or @p host_rx without a bridge set with eth_bridge_set_hw_forwarding()
 *         on every port of the bridge
 * @retval -ENOMEM The table is full
 * @return Another negative errno the device gave, the entry is then unchanged
 */
int eth_bridge_fdb_add(struct net_eth_addr *mac, struct net_if *iface, uint32_t portmap,
		       bool host_rx);

/**
 * @brief Delete an FDB entry
 *
 * @param mac MAC address
 * @param iface Interface (NULL to delete from any interface). With
 *              a bridge set with eth_bridge_set_hw_forwarding() the entry of the bridge of @p iface is
 *              deleted, whatever its ports.
 * @return 0 on success, negative errno on failure
 */
int eth_bridge_fdb_del(struct net_eth_addr *mac, struct net_if *iface);

/**
 * @brief Delete FDB entries on an interface
 *
 * @param iface Interface (NULL to delete from any interface)
 * @return 0 on success, negative errno on failure
 */
int eth_bridge_fdb_del_iface(struct net_if *iface);

/**
 * @brief Callback function type for iterating over FDB entries
 *
 * @param entry Pointer to the current FDB entry being processed
 * @param user_data User-defined data passed from the foreach function
 */
typedef void (*eth_bridge_fdb_entry_cb_t)(struct eth_bridge_fdb_entry *entry, void *user_data);

/**
 * @brief Iterate over all entries in the FDB table
 *
 * @param cb Callback function to be called for each FDB entry.
 * @param user_data User-defined data pointer passed to each callback invocation.
 */
void eth_bridge_fdb_foreach(eth_bridge_fdb_entry_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_NET_ETHERNET_BRIDGE_FDB_H_ */
