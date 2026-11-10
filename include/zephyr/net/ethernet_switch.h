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
 * @brief Ethernet switch API
 * @defgroup ethernet_switch Ethernet switch API
 * @ingroup ethernet
 * @{
 */

/** vids value of an entry that matches the frames whatever their VLAN, tagged or not */
#define ETHERNET_SWITCH_VID_ANY			0xFFFFU

/** Number of bytes an extended lookup matches on, after the ethertype */
#define ETHERNET_SWITCH_EXT_LOOKUP_DATA_LEN	14U

/**
 * @brief Port map of a filtering entry
 *
 * Where the frames that match go. A port that the map does not name filters the frames.
 */
struct ethernet_switch_port_map {
	/** Ports the frames are forwarded to: a bit per port index */
	uint32_t forward: 31;
	/** The frames are received by the host, on the interface of the port they came in by */
	bool receive : 1;
};

/**
 * @brief Extended lookup rule
 *
 * After the address, the device can look the frames up a second time, on the ethertype and on the
 * bytes that follow it. A bit of the mask that is 0 is a bit that does not matter. The rule is
 * created with ethernet_switch_ext_lookup_set() and is reached by the frames of the filtering
 * entries that have @c extended_lookup set.
 */
struct ethernet_switch_ext_lookup {
	/** Ethertype to match */
	uint16_t ethertype;
	/** Bytes that follow the ethertype */
	uint8_t data[ETHERNET_SWITCH_EXT_LOOKUP_DATA_LEN];
	/** Mask applied to @p data */
	uint8_t mask[ETHERNET_SWITCH_EXT_LOOKUP_DATA_LEN];
	/** port-map: where the frames go */
	struct ethernet_switch_port_map port_map;
};

/**
 * @brief Filtering entry of a filtering database
 *
 * A filtering entry of the static kind in the filtering-database of ieee802-dot1q-bridge. It
 * tells what the switch does with the frames for the address @p address: the ports they are
 * forwarded to and whether the host receives them are the ones of the port-map. An entry that
 * sends them nowhere drops the frames.
 */
struct ethernet_switch_filtering_entry {
	/** address: destination address, matched exactly */
	struct net_eth_addr address;
	/** vids: VLAN of the frames, or ETHERNET_SWITCH_VID_ANY */
	uint16_t vids;
	/** port-map: where the frames go */
	struct ethernet_switch_port_map port_map;
	/**
	 * Go on with the extended lookup rules after the address, they decide the port-map of
	 * the frames they match instead of @p port_map
	 */
	bool extended_lookup;
};

/**
 * @brief Callback to walk the filtering entries of a switch
 *
 * The entry and what its pointers refer to are only valid during the call.
 *
 * @param entry A filtering entry of the switch
 * @param user_data User data given to ethernet_switch_filtering_entry_foreach()
 */
typedef void (*ethernet_switch_filtering_entry_cb_t)(
	const struct ethernet_switch_filtering_entry *entry, void *user_data);

/**
 * @brief API of a device that switches frames between its ports
 *
 * The device of a switch chip sets this struct as its API. It is the chip, not an interface: each
 * port has its own Ethernet interface, for the IP traffic and for the protocols that run on
 * a link such as LLDP, gPTP and STP. Every operation is optional.
 */
struct ethernet_switch_api {
	/** Add (set) or remove (not set) a filtering entry */
	int (*filtering_entry_set)(const struct device *dev,
				   const struct ethernet_switch_filtering_entry *entry, bool set);

	/** Walk the filtering entries the device holds */
	int (*filtering_entry_foreach)(const struct device *dev,
				       ethernet_switch_filtering_entry_cb_t cb, void *user_data);

	/** Add (set) or remove (not set) an extended lookup rule */
	int (*ext_lookup_set)(const struct device *dev,
			      const struct ethernet_switch_ext_lookup *ext_lookup, bool set);

	/** Set the port-map of the frames that no filtering entry matches */
	int (*default_port_map_set)(const struct device *dev, uint32_t input_port_list,
				    const struct ethernet_switch_port_map *port_map);

	/** Get the Ethernet interface of a port */
	struct net_if *(*get_port_iface)(const struct device *dev, uint8_t port_idx);
};

/**
 * @brief Add or remove a filtering entry
 *
 * Adding an entry for an address and vids that already has one replaces it. To remove an entry only
 * the address and the vids matter.
 *
 * @param dev Switch device
 * @param entry The filtering entry
 * @param set true to add the entry, false to remove it
 *
 * @retval 0 The entry is added or removed.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOSPC The device has no room left for the entry.
 * @retval -ENOENT The entry to remove does not exist.
 * @retval -ENOTSUP The device cannot do it, for instance it has no extended lookup.
 */
static inline int ethernet_switch_filtering_entry_set(
	const struct device *dev, const struct ethernet_switch_filtering_entry *entry, bool set)
{
	const struct ethernet_switch_api *api = (const struct ethernet_switch_api *)dev->api;

	if (api == NULL || api->filtering_entry_set == NULL) {
		return -ENOTSUP;
	}

	return api->filtering_entry_set(dev, entry, set);
}

/**
 * @brief Add or remove an extended lookup rule
 *
 * Adding a rule for an ethertype, data and mask that already has one replaces it. To remove a rule
 * only the ethertype, the data and the mask matter.
 *
 * @param dev Switch device
 * @param ext_lookup The rule
 * @param set true to add the rule, false to remove it
 *
 * @retval 0 The rule is added or removed.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOSPC The device has no room left for the rule.
 * @retval -ENOENT The rule to remove does not exist.
 * @retval -ENOTSUP The device has no extended lookup.
 */
static inline int ethernet_switch_ext_lookup_set(
	const struct device *dev, const struct ethernet_switch_ext_lookup *ext_lookup, bool set)
{
	const struct ethernet_switch_api *api = (const struct ethernet_switch_api *)dev->api;

	if (api == NULL || api->ext_lookup_set == NULL) {
		return -ENOTSUP;
	}

	return api->ext_lookup_set(dev, ext_lookup, set);
}

/**
 * @brief Walk the filtering entries of a switch
 *
 * A device that keeps the entries per input port can give the same address more than once, each
 * time with the port-map of the frames of some of the ports.
 *
 * @param dev Switch device
 * @param cb Function called for each entry, it may call the other functions of this API
 * @param user_data User data given to @p cb
 *
 * @retval 0 All the entries were given to @p cb.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOTSUP The device cannot do it.
 */
static inline int ethernet_switch_filtering_entry_foreach(
	const struct device *dev, ethernet_switch_filtering_entry_cb_t cb, void *user_data)
{
	const struct ethernet_switch_api *api = (const struct ethernet_switch_api *)dev->api;

	if (api == NULL || api->filtering_entry_foreach == NULL) {
		return -ENOTSUP;
	}

	return api->filtering_entry_foreach(dev, cb, user_data);
}

/**
 * @brief Set the port-map of the frames that no filtering entry matches
 *
 * @param dev Switch device
 * @param input_port_list Ports the setting applies to, for the frames received on them: a bit per
 *                        port index
 * @param port_map Where the frames go, as in a filtering entry
 *
 * @retval 0 The default is set.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOTSUP The device cannot do it.
 */
static inline int ethernet_switch_default_port_map_set(
	const struct device *dev, uint32_t input_port_list,
	const struct ethernet_switch_port_map *port_map)
{
	const struct ethernet_switch_api *api = (const struct ethernet_switch_api *)dev->api;

	if (api == NULL || api->default_port_map_set == NULL) {
		return -ENOTSUP;
	}

	return api->default_port_map_set(dev, input_port_list, port_map);
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
