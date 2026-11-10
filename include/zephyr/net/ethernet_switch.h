/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Ethernet switch interface API
 * @ingroup ethernet_switch
 */

#ifndef ZEPHYR_INCLUDE_NET_ETHERNET_SWITCH_H_
#define ZEPHYR_INCLUDE_NET_ETHERNET_SWITCH_H_

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ethernet switch interface
 * @defgroup ethernet_switch Ethernet switch interface
 * @ingroup ethernet
 * @{
 */

/**
 * @brief Deliver the matching frames to the host
 *
 * The frames arrive on the switch interface.
 */
#define ETHERNET_SWITCH_FILTER_TO_HOST		BIT(0)

/** Forward the matching frames to the other ports, in hardware */
#define ETHERNET_SWITCH_FILTER_TO_OTHER_PORT	BIT(1)

/**
 * @brief Receive filter rule of a switch
 *
 * The rule is applied by the hardware. A frame that matches none of the rules is handled by the
 * default forwarding of the device. When a frame matches several rules, which one applies is up
 * to the device.
 */
struct ethernet_switch_filter {
	/** Destination address, matched exactly */
	struct net_eth_addr mac_address;
	/** Ports the rule applies to, frames received on them: a bit per port index */
	uint32_t ports;
	/**
	 * What happens to the frames, ETHERNET_SWITCH_FILTER_TO_HOST and
	 * ETHERNET_SWITCH_FILTER_TO_OTHER_PORT, or 0 to drop them.
	 */
	uint8_t action;
};

/** An entry of the forwarding database of a switch interface */
struct ethernet_switch_fdb_entry {
	/** Address of the station */
	struct net_eth_addr mac_address;
	/** Port index the station is reached by */
	uint8_t port_idx;
	/** Set by the user, the entry neither ages nor is replaced by a learned one */
	bool is_static;
	/** Seconds since a frame of the station was last seen, 0 for a static entry */
	uint32_t age;
};

/**
 * @brief Callback to walk the forwarding database
 *
 * @param entry An entry of the database
 * @param user_data User data given to ethernet_switch_fdb_foreach()
 */
typedef void (*ethernet_switch_fdb_cb_t)(const struct ethernet_switch_fdb_entry *entry,
					 void *user_data);

/**
 * @brief API of the device behind the switch interface of a multi-port device
 *
 * A device with several ports gives each port an Ethernet interface, for the protocols that run
 * on a link such as LLDP, gPTP and STP, and one switch interface, for the IP traffic of all
 * the ports. The device of the switch interface sets this struct as its API and reports the
 * ETHERNET_SWITCH capability. Every operation is optional.
 */
struct ethernet_switch_api {
	/**
	 * The Ethernet API must be placed in first position, so that the device is also an
	 * Ethernet device.
	 */
	struct ethernet_api eth_api;

	/** Pin a station to a port */
	int (*fdb_add)(const struct device *dev, struct net_if *iface,
		       const struct net_eth_addr *mac_address, uint8_t port_idx);

	/** Remove a static entry */
	int (*fdb_del)(const struct device *dev, struct net_if *iface,
		       const struct net_eth_addr *mac_address);

	/** Forget the learned stations, and the static ones if asked to */
	int (*fdb_flush)(const struct device *dev, struct net_if *iface, bool include_static);

	/** Walk the entries of the forwarding database */
	int (*fdb_foreach)(const struct device *dev, struct net_if *iface,
			   ethernet_switch_fdb_cb_t cb, void *user_data);

	/** Set the ageing time of learned stations in seconds, 0 for no ageing */
	int (*set_ageing)(const struct device *dev, struct net_if *iface, uint32_t seconds);

	/** Get the ageing time of learned stations in seconds */
	int (*get_ageing)(const struct device *dev, struct net_if *iface, uint32_t *seconds);

	/** Add (set) or remove (not set) a receive filter rule */
	int (*filter_set)(const struct device *dev, struct net_if *iface,
			  const struct ethernet_switch_filter *filter, bool set);

	/** Get the Ethernet interface of a port */
	struct net_if *(*get_port_iface)(const struct device *dev, struct net_if *iface,
					 uint8_t port_idx);
};

/** @cond INTERNAL_HIDDEN */

/* The Ethernet API is the first one of the switch API */
BUILD_ASSERT(offsetof(struct ethernet_switch_api, eth_api) == 0);

/** @endcond */

/**
 * @brief Get the switch API of an interface
 *
 * @param iface Network interface
 *
 * @return The API, or NULL if @p iface is not a switch interface.
 */
static inline const struct ethernet_switch_api *net_eth_switch_get_api(struct net_if *iface)
{
	const struct device *dev;

	if (iface == NULL || net_if_l2(iface) != &NET_L2_GET_NAME(ETHERNET) ||
	    (net_eth_get_hw_capabilities(iface) & ETHERNET_SWITCH) == 0) {
		return NULL;
	}

	dev = net_if_get_device(iface);

	return (dev != NULL) ? (const struct ethernet_switch_api *)dev->api : NULL;
}

/**
 * @brief Pin a station to a port
 *
 * The switch interface sends the frames for @p mac_address by @p port_idx only. The entry does
 * not age and is not moved by learning. Adding an address that is already there pins it to the
 * new port.
 *
 * @param iface Switch interface
 * @param mac_address Unicast address of the station
 * @param port_idx Port index
 *
 * @retval 0 The entry is added.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOSPC The database has no room left.
 * @retval -ENOTSUP @p iface is not a switch interface, or the device cannot do it.
 */
static inline int ethernet_switch_fdb_add(struct net_if *iface,
					  const struct net_eth_addr *mac_address,
					  uint8_t port_idx)
{
	const struct ethernet_switch_api *api = net_eth_switch_get_api(iface);

	if (api == NULL || api->fdb_add == NULL) {
		return -ENOTSUP;
	}

	return api->fdb_add(net_if_get_device(iface), iface, mac_address, port_idx);
}

/**
 * @brief Remove a static entry
 *
 * @param iface Switch interface
 * @param mac_address Address of the station
 *
 * @retval 0 The entry is removed.
 * @retval -ENOENT There is no static entry for @p mac_address.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOTSUP @p iface is not a switch interface, or the device cannot do it.
 */
static inline int ethernet_switch_fdb_del(struct net_if *iface,
					  const struct net_eth_addr *mac_address)
{
	const struct ethernet_switch_api *api = net_eth_switch_get_api(iface);

	if (api == NULL || api->fdb_del == NULL) {
		return -ENOTSUP;
	}

	return api->fdb_del(net_if_get_device(iface), iface, mac_address);
}

/**
 * @brief Forget the stations of the forwarding database
 *
 * Frames for a forgotten station are sent on all the ports until it is learned again.
 *
 * @param iface Switch interface
 * @param include_static Also remove the static entries
 *
 * @retval 0 The database is flushed.
 * @retval -ENOTSUP @p iface is not a switch interface, or the device cannot do it.
 */
static inline int ethernet_switch_fdb_flush(struct net_if *iface, bool include_static)
{
	const struct ethernet_switch_api *api = net_eth_switch_get_api(iface);

	if (api == NULL || api->fdb_flush == NULL) {
		return -ENOTSUP;
	}

	return api->fdb_flush(net_if_get_device(iface), iface, include_static);
}

/**
 * @brief Walk the entries of the forwarding database
 *
 * @param iface Switch interface
 * @param cb Function called for each entry, it may call the other functions of this API
 * @param user_data User data given to @p cb
 *
 * @retval 0 All the entries were given to @p cb.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOTSUP @p iface is not a switch interface, or the device cannot do it.
 */
static inline int ethernet_switch_fdb_foreach(struct net_if *iface, ethernet_switch_fdb_cb_t cb,
					      void *user_data)
{
	const struct ethernet_switch_api *api = net_eth_switch_get_api(iface);

	if (api == NULL || api->fdb_foreach == NULL) {
		return -ENOTSUP;
	}

	return api->fdb_foreach(net_if_get_device(iface), iface, cb, user_data);
}

/**
 * @brief Set the ageing time of the learned stations
 *
 * @param iface Switch interface
 * @param seconds Seconds without a frame from a station before it is forgotten, 0 to keep it
 *                until it is replaced. Static entries do not age.
 *
 * @retval 0 The ageing time is set.
 * @retval -ENOTSUP @p iface is not a switch interface, or the device cannot do it.
 */
static inline int ethernet_switch_set_ageing(struct net_if *iface, uint32_t seconds)
{
	const struct ethernet_switch_api *api = net_eth_switch_get_api(iface);

	if (api == NULL || api->set_ageing == NULL) {
		return -ENOTSUP;
	}

	return api->set_ageing(net_if_get_device(iface), iface, seconds);
}

/**
 * @brief Get the ageing time of the learned stations
 *
 * @param iface Switch interface
 * @param seconds The ageing time in seconds, 0 meaning no ageing
 *
 * @retval 0 The ageing time is returned.
 * @retval -EINVAL @p seconds is NULL.
 * @retval -ENOTSUP @p iface is not a switch interface, or the device cannot do it.
 */
static inline int ethernet_switch_get_ageing(struct net_if *iface, uint32_t *seconds)
{
	const struct ethernet_switch_api *api = net_eth_switch_get_api(iface);

	if (api == NULL || api->get_ageing == NULL) {
		return -ENOTSUP;
	}

	return api->get_ageing(net_if_get_device(iface), iface, seconds);
}

/**
 * @brief Add or remove a receive filter rule
 *
 * Adding a rule for an address and ports that already has one replaces it.
 *
 * @param iface Switch interface
 * @param filter The rule
 * @param set true to add the rule, false to remove it
 *
 * @retval 0 The rule is added or removed.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOSPC The device has no room left for the rule.
 * @retval -ENOENT The rule to remove does not exist.
 * @retval -ENOTSUP @p iface is not a switch interface, or the device cannot do it.
 */
static inline int ethernet_switch_filter_set(struct net_if *iface,
					     const struct ethernet_switch_filter *filter, bool set)
{
	const struct ethernet_switch_api *api = net_eth_switch_get_api(iface);

	if (api == NULL || api->filter_set == NULL) {
		return -ENOTSUP;
	}

	return api->filter_set(net_if_get_device(iface), iface, filter, set);
}

/**
 * @brief Get the Ethernet interface of a port
 *
 * @param iface Switch interface
 * @param port_idx Port index
 *
 * @return The interface of the port, or NULL if @p iface is not a switch interface or has no
 *         such port.
 */
static inline struct net_if *ethernet_switch_get_port_iface(struct net_if *iface,
							    uint8_t port_idx)
{
	const struct ethernet_switch_api *api = net_eth_switch_get_api(iface);

	if (api == NULL || api->get_port_iface == NULL) {
		return NULL;
	}

	return api->get_port_iface(net_if_get_device(iface), iface, port_idx);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_ETHERNET_SWITCH_H_ */
