/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Ethernet switch API
 * @ingroup ethernet_switch
 */

#ifndef ZEPHYR_INCLUDE_NET_ETHERNET_SWITCH_H_
#define ZEPHYR_INCLUDE_NET_ETHERNET_SWITCH_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ethernet switch API
 * @defgroup ethernet_switch Ethernet switch API
 * @ingroup ethernet
 * @{
 */

/**
 * @brief Deliver the matching frames to the host
 *
 * The frames arrive on the Ethernet interface of the port they were received on.
 */
#define ETHERNET_SWITCH_ACTION_HOST		BIT(0)

/** Forward the matching frames to the other ports, in hardware */
#define ETHERNET_SWITCH_ACTION_FORWARD		BIT(1)

/**
 * @brief Route of a switch
 *
 * A route is applied by the hardware to the frames received on some ports, for the destination
 * address it names. The ports keep their own Ethernet interface, with their own address, so
 * the host reaches each of them separately and the routes only decide what the hardware does with
 * the frames on their way. When a frame matches several routes, which one applies is up to
 * the device.
 */
struct ethernet_switch_route {
	/** Destination address, matched exactly */
	struct net_eth_addr mac_address;
	/** Ports the route applies to, for the frames received on them: a bit per port index */
	uint32_t ports;
	/**
	 * What happens to the frames: ETHERNET_SWITCH_ACTION_HOST and
	 * ETHERNET_SWITCH_ACTION_FORWARD, or 0 to drop them.
	 */
	uint8_t action;
};

/**
 * @brief Callback to walk the routes of a switch
 *
 * @param route A route of the switch
 * @param user_data User data given to ethernet_switch_route_foreach()
 */
typedef void (*ethernet_switch_route_cb_t)(const struct ethernet_switch_route *route,
					   void *user_data);

/**
 * @brief API of a device that switches frames between its ports
 *
 * The device of a switch chip sets this struct as its API. It is the chip, not an interface: each
 * port has its own Ethernet interface, for the IP traffic and for the protocols that run on
 * a link such as LLDP, gPTP and STP. Every operation is optional.
 */
struct ethernet_switch_api {
	/** Add (set) or remove (not set) a route */
	int (*route_set)(const struct device *dev, const struct ethernet_switch_route *route,
			 bool set);

	/** Walk the routes the device holds */
	int (*route_foreach)(const struct device *dev, ethernet_switch_route_cb_t cb,
			     void *user_data);

	/** Set what happens to the frames that no route matches */
	int (*default_route_set)(const struct device *dev, uint32_t ports, uint8_t action);

	/** Get the Ethernet interface of a port */
	struct net_if *(*get_port_iface)(const struct device *dev, uint8_t port_idx);
};

/**
 * @brief Add or remove a route
 *
 * Adding a route for an address and ports that already has one replaces it.
 *
 * @param dev Switch device
 * @param route The route
 * @param set true to add the route, false to remove it
 *
 * @retval 0 The route is added or removed.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOSPC The device has no room left for the route.
 * @retval -ENOENT The route to remove does not exist.
 * @retval -ENOTSUP The device cannot do it.
 */
static inline int ethernet_switch_route_set(const struct device *dev,
					    const struct ethernet_switch_route *route, bool set)
{
	const struct ethernet_switch_api *api = (const struct ethernet_switch_api *)dev->api;

	if (api == NULL || api->route_set == NULL) {
		return -ENOTSUP;
	}

	return api->route_set(dev, route, set);
}

/**
 * @brief Walk the routes of a switch
 *
 * @param dev Switch device
 * @param cb Function called for each route, it may call the other functions of this API
 * @param user_data User data given to @p cb
 *
 * @retval 0 All the routes were given to @p cb.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOTSUP The device cannot do it.
 */
static inline int ethernet_switch_route_foreach(const struct device *dev,
						ethernet_switch_route_cb_t cb, void *user_data)
{
	const struct ethernet_switch_api *api = (const struct ethernet_switch_api *)dev->api;

	if (api == NULL || api->route_foreach == NULL) {
		return -ENOTSUP;
	}

	return api->route_foreach(dev, cb, user_data);
}

/**
 * @brief Set what happens to the frames that no route matches
 *
 * @param dev Switch device
 * @param ports Ports the setting applies to, for the frames received on them: a bit per port index
 * @param action ETHERNET_SWITCH_ACTION_HOST and ETHERNET_SWITCH_ACTION_FORWARD, or 0 to drop them
 *
 * @retval 0 The default is set.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOTSUP The device cannot do it.
 */
static inline int ethernet_switch_default_route_set(const struct device *dev, uint32_t ports,
						    uint8_t action)
{
	const struct ethernet_switch_api *api = (const struct ethernet_switch_api *)dev->api;

	if (api == NULL || api->default_route_set == NULL) {
		return -ENOTSUP;
	}

	return api->default_route_set(dev, ports, action);
}

/**
 * @brief Get the Ethernet interface of a port
 *
 * @param dev Switch device
 * @param port_idx Port index
 *
 * @return The interface of the port, or NULL if the device has no such port.
 */
static inline struct net_if *ethernet_switch_get_port_iface(const struct device *dev,
							    uint8_t port_idx)
{
	const struct ethernet_switch_api *api = (const struct ethernet_switch_api *)dev->api;

	if (api == NULL || api->get_port_iface == NULL) {
		return NULL;
	}

	return api->get_port_iface(dev, port_idx);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_ETHERNET_SWITCH_H_ */
