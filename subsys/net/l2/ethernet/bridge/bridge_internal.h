/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Ethernet bridge internal functions
 *
 * Shared between the bridge sources. Not part of the public API.
 */

#ifndef ZEPHYR_SUBSYS_NET_L2_ETHERNET_BRIDGE_BRIDGE_INTERNAL_H_
#define ZEPHYR_SUBSYS_NET_L2_ETHERNET_BRIDGE_BRIDGE_INTERNAL_H_

#include <stdbool.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/ethernet_bridge.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the port state of a bridged interface
 *
 * @param iface Bridged Ethernet interface
 *
 * @return The state of the port of @p iface, or
 *         ETH_BRIDGE_PORT_STATE_DISCARDING if @p iface is not bridged.
 */
enum eth_bridge_port_state eth_bridge_port_get_state(struct net_if *iface);

/**
 * @brief Set the port state of a bridged interface
 *
 * @param br Bridge interface
 * @param iface Ethernet interface bridged by @p br
 * @param state New port state
 *
 * @retval 0 The state was set.
 * @retval -EINVAL The state is not a valid port state.
 * @retval -ENOENT @p iface is not a port of @p br.
 */
int eth_bridge_port_set_state(struct net_if *br, struct net_if *iface,
			      enum eth_bridge_port_state state);

/**
 * @brief Check whether a bridged interface forwards frames
 *
 * @param iface Bridged Ethernet interface
 *
 * @return true if the port of @p iface is forwarding, false otherwise.
 */
static inline bool eth_bridge_port_is_forwarding(struct net_if *iface)
{
	return eth_bridge_port_get_state(iface) == ETH_BRIDGE_PORT_STATE_FORWARDING;
}

/**
 * @brief Check whether a bridged interface is on a bridge that forwards in hardware
 *
 * @param iface Ethernet interface
 *
 * @return true if the bridge of @p iface was set with eth_bridge_set_hw_forwarding().
 */
bool eth_bridge_port_hw_forwarding(struct net_if *iface);

/**
 * @brief Get the bit of a bridge port in a port map
 *
 * @param iface Ethernet interface bridged by a bridge
 *
 * @return The bit of the port of @p iface.
 */
uint32_t eth_bridge_port_bit(struct net_if *iface);

/**
 * @brief Check that a bridge can hold an FDB entry in hardware
 *
 * @param br Bridge interface
 * @param portmap Port map of the entry, 0 to not check it
 *
 * @retval 0 @p br forwards in hardware and has a port.
 * @retval -ENOTSUP @p br forwards in software, or has no port.
 * @retval -EINVAL @p portmap has a bit of a port that does not exist.
 */
int eth_bridge_hw_fdb_check(struct net_if *br, uint32_t portmap);

/**
 * @brief Give an FDB entry to the hooks of a bridge
 *
 * @param br Bridge interface
 * @param set true to add the entry, false to remove the one of its address
 * @param entry The entry
 *
 * @return 0 on success, or the negative errno of the hook.
 */
int eth_bridge_hw_fdb_config(struct net_if *br, bool set,
			     const struct eth_bridge_hw_fdb_entry *entry);

/**
 * @brief Learn the port of a source MAC address
 *
 * Adds a dynamic FDB entry for @p mac on @p iface, or moves an existing
 * dynamic entry of the bridge to @p iface. Static entries are left as they
 * are, and nothing is learned when the table is full or @p mac is not a
 * unicast address.
 *
 * @param bridge Bridge interface
 * @param mac Source MAC address of a received frame
 * @param iface Bridged interface the frame was received on
 */
void eth_bridge_fdb_learn(struct net_if *bridge, struct net_eth_addr *mac, struct net_if *iface);

/**
 * @brief Find the ports of a destination MAC address
 *
 * @param bridge Bridge interface
 * @param mac Destination MAC address
 * @param ports Array filled with the interfaces of the matching entries
 * @param max_ports Number of elements in @p ports
 *
 * @return Number of matching FDB entries of @p bridge. At most
 *         @p max_ports of them are stored in @p ports.
 */
size_t eth_bridge_fdb_lookup(struct net_if *bridge, const struct net_eth_addr *mac,
			     struct net_if *ports[], size_t max_ports);

/**
 * @brief Find the entry with a port map of a destination MAC address
 *
 * @param bridge Bridge interface
 * @param mac Destination MAC address
 * @param portmap Set to the port map of the entry
 * @param host_rx Set to whether the host also gets the frames
 *
 * @return true if @p bridge has such an entry, false otherwise.
 */
bool eth_bridge_fdb_hw_lookup(struct net_if *bridge, const struct net_eth_addr *mac,
			      uint32_t *portmap, bool *host_rx);

/**
 * @brief Remove FDB entries of a bridge
 *
 * Used when the topology changes, so that frames are flooded until the
 * stations are learned again.
 *
 * @param bridge Bridge interface
 * @param iface Remove only the entries of this port, or NULL for all ports
 * @param dynamic_only Keep the static entries
 */
void eth_bridge_fdb_flush(struct net_if *bridge, struct net_if *iface, bool dynamic_only);

/**
 * @brief Set up the spanning tree state of a bridge
 *
 * @param ctx Bridge context, with its interface and link address set
 */
void eth_bridge_stp_init(struct eth_bridge_iface_context *ctx);

/**
 * @brief Check whether the spanning tree protocol runs on a bridge
 *
 * @param ctx Bridge context
 *
 * @return true if the protocol runs, false otherwise.
 */
bool eth_bridge_stp_is_running(struct eth_bridge_iface_context *ctx);

/**
 * @brief Check that the spanning tree protocol can block a port
 *
 * @param iface Ethernet interface
 *
 * @return false if the device of @p iface forwards to other ports in hardware.
 */
bool eth_bridge_stp_port_supported(struct net_if *iface);

/**
 * @brief Tell the spanning tree protocol that a port was added
 *
 * Called with the bridge lock held, after the port slot was filled.
 *
 * @param ctx Bridge context
 * @param index Index of the port in the ports array of @p ctx
 */
void eth_bridge_stp_port_added(struct eth_bridge_iface_context *ctx, size_t index);

/**
 * @brief Tell the spanning tree protocol that a port is being removed
 *
 * Called with the bridge lock held, before the port slot is cleared.
 *
 * @param ctx Bridge context
 * @param index Index of the port in the ports array of @p ctx
 */
void eth_bridge_stp_port_removed(struct eth_bridge_iface_context *ctx, size_t index);

/**
 * @brief Pass a BPDU received on a bridge port to the spanning tree protocol
 *
 * @param bridge Bridge interface
 * @param iface Bridged interface the frame was received on
 * @param pkt Received frame
 *
 * @return true if the frame was for the Bridge Group Address and is consumed,
 *         false if the bridge processes it as usual.
 */
bool eth_bridge_stp_input(struct net_if *bridge, struct net_if *iface, struct net_pkt *pkt);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_NET_L2_ETHERNET_BRIDGE_BRIDGE_INTERNAL_H_ */
