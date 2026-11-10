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
 *  Always static.
 */
#define ETHERNET_BRIDGE_FDB_FLAG_HW      BIT(2)

/**
 * @brief FDB entry structure
 */
struct eth_bridge_fdb_entry {
	/** MAC address */
	struct net_eth_addr mac;
	/** Bridge interface the entry belongs to */
	struct net_if *bridge;
	/** Bridge ports the frames for @ref mac go to, bit n for the bridge port n,
	 *  whose interface eth_bridge_get_port_iface() returns. An entry without
	 *  @ref ETHERNET_BRIDGE_FDB_FLAG_HW has exactly one bit set, the port of the station.
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
 * @brief Parameters of a static FDB entry, see eth_bridge_fdb_add() and eth_bridge_fdb_del()
 */
struct eth_bridge_fdb_params {
	/** MAC address */
	struct net_eth_addr *mac;
	/** Bridge ports the frames for @ref mac go to, bit n for the
	 *  bridge port n, which eth_bridge_get_port_iface() gives for n.
	 *  0 for the interface given to eth_bridge_fdb_add() alone. Without a bridge defined
	 *  with ETH_BRIDGE_HW_DEFINE() it must be 0 or that interface alone.
	 */
	uint32_t portmap;
	/** The frames are also passed to the host. Needs
	 *  a bridge defined with ETH_BRIDGE_HW_DEFINE().
	 */
	bool host_rx;
};

/**
 * @brief Add a static FDB entry
 *
 * On a port whose device reports a bridge defined with ETH_BRIDGE_HW_DEFINE() the entry is
 * also given to the hooks of the bridge, for the device to forward the frames for
 * @ref eth_bridge_fdb_params.mac to the ports of @ref eth_bridge_fdb_params.portmap and
 * to the host if @ref eth_bridge_fdb_params.host_rx is set. All the ports of the
 * bridge must then be of such devices. The entry replaces the one the bridge
 * has for the MAC address. The bridge does not learn into or forward the frames of those
 * ports itself, and a frame the host sends to the MAC address only goes out of the
 * ports of the port map.
 *
 * @param iface Interface, a port of the bridge
 * @param params Parameters of the entry
 * @retval 0 on success
 * @retval -EINVAL A bad argument, or the port map has a bit of no port
 * @retval -ENOTSUP A port map or host receive flag without a bridge defined with
 *         ETH_BRIDGE_HW_DEFINE() on every port of the bridge
 * @retval -ENOMEM The table is full
 * @return Another negative errno the device gave, the entry is then unchanged
 */
int eth_bridge_fdb_add(struct net_if *iface, const struct eth_bridge_fdb_params *params);

/**
 * @brief Delete an FDB entry
 *
 * @param iface Interface (NULL to delete from any interface). With
 *              a bridge defined with ETH_BRIDGE_HW_DEFINE() the entry of the bridge of @p iface is
 *              deleted, whatever its ports.
 * @param params Parameters of the entry, only @ref eth_bridge_fdb_params.mac is used
 * @return 0 on success, negative errno on failure
 */
int eth_bridge_fdb_del(struct net_if *iface, const struct eth_bridge_fdb_params *params);

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
