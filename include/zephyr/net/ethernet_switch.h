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

/**
 * @brief port-ref of the host port
 *
 * The filtering entries of IEEE 802.1Q name the ports of the bridge. The port of the switch
 * that faces the host, the one the IP stack and the applications are behind, is named with this
 * value. The other port-ref values are the port indexes of the device.
 */
#define ETHERNET_SWITCH_PORT_HOST		0xFFU

/** vids value of an entry that matches the frames whatever their VLAN, tagged or not */
#define ETHERNET_SWITCH_VID_ANY			0xFFFFU

/** Number of bytes an extended lookup matches on, after the ethertype */
#define ETHERNET_SWITCH_EXT_LOOKUP_DATA_LEN	14U

/**
 * @brief control-element of a port-map entry
 *
 * See the port-map of the filtering-entry list of ieee802-dot1q-bridge.
 */
enum ethernet_switch_control_element {
	/** The frames that match are forwarded to the port */
	ETHERNET_SWITCH_CONTROL_ELEMENT_FORWARD,
	/** The frames that match are not forwarded to the port */
	ETHERNET_SWITCH_CONTROL_ELEMENT_FILTER,
	/** The frames that match are forwarded to the port, unless the port filters them */
	ETHERNET_SWITCH_CONTROL_ELEMENT_FORWARD_FILTER,
};

/**
 * @brief vlan-tag-identification-type of ieee802-dot1cb-stream-identification
 */
enum ethernet_switch_vlan_tag_identification {
	/** The frames with a VLAN tag whose VID is not 0 */
	ETHERNET_SWITCH_VLAN_TAG_TAGGED,
	/** The frames with a priority tag, or none */
	ETHERNET_SWITCH_VLAN_TAG_PRIORITY,
	/** All the frames */
	ETHERNET_SWITCH_VLAN_TAG_ALL,
};

/**
 * @brief port-map entry of a filtering entry
 *
 * A port that the port-map does not name filters the frames.
 */
struct ethernet_switch_port_map {
	/** port-ref: the port index, or ETHERNET_SWITCH_PORT_HOST */
	uint8_t port_ref;
	/** control-element: what happens to the frames for @p port_ref */
	enum ethernet_switch_control_element control_element;
};

/**
 * @brief Extended lookup of a filtering entry
 *
 * The device looks the frames up a second time after the address, on the ethertype and on the
 * bytes that follow it. A bit of the mask that is 0 is a bit that does not matter.
 */
struct ethernet_switch_ext_lookup {
	/** Ethertype to match */
	uint16_t ethertype;
	/** Bytes that follow the ethertype */
	uint8_t data[ETHERNET_SWITCH_EXT_LOOKUP_DATA_LEN];
	/** Mask applied to @p data */
	uint8_t mask[ETHERNET_SWITCH_EXT_LOOKUP_DATA_LEN];
};

/**
 * @brief Stream identification of a filtering entry
 *
 * The frames the entry matches are the frames of the stream @p handle, as the null stream
 * identification of ieee802-dot1cb-stream-identification does it: the address and the vids of the
 * entry are the destination-address and the vlan of the identification. The tables that work on
 * streams (stream filters and gates, FRER) refer to the stream with the handle.
 */
struct ethernet_switch_stream_identification {
	/** handle: stream handle */
	uint32_t handle;
	/** tagged: which of the frames the vids applies to */
	enum ethernet_switch_vlan_tag_identification tagged;
	/** input-port-list: ports the stream is identified on, a bit per port index, 0 for all */
	uint32_t input_port_list;
};

/**
 * @brief Filtering entry of a filtering database
 *
 * A filtering entry of the static kind in the filtering-database of ieee802-dot1q-bridge. It
 * tells what the switch does with the frames for the address @p address: the ports they are
 * forwarded to are the ones of the port-map with the control-element forward, the host port
 * included. An entry with no port-map entry, or with only filter ones, drops the frames.
 *
 * @p ext_lookup makes the device look up more than the address, and @p stream_identification
 * identifies the frames as a stream. Both are optional, an entry with only an address and a
 * port-map is the simple one. A device that has no stream identification of its own does it with
 * the extended lookup entry, so the two can be used together on it.
 */
struct ethernet_switch_filtering_entry {
	/** database-id: the filtering database the entry belongs to */
	uint32_t database_id;
	/** address: destination address, matched exactly */
	struct net_eth_addr address;
	/** vids: VLAN of the frames, or ETHERNET_SWITCH_VID_ANY */
	uint16_t vids;
	/** port-map: the control-element of each port, can be NULL if @p port_map_len is 0 */
	const struct ethernet_switch_port_map *port_map;
	/** Number of port-map entries */
	size_t port_map_len;
	/** Extended lookup of the entry, NULL for none */
	const struct ethernet_switch_ext_lookup *ext_lookup;
	/** Stream identification of the entry, NULL for none */
	const struct ethernet_switch_stream_identification *stream_identification;
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

	/** Set the port-map of the frames that no filtering entry matches */
	int (*default_port_map_set)(const struct device *dev, uint32_t input_port_list,
				    const struct ethernet_switch_port_map *port_map,
				    size_t port_map_len);

	/** Get the Ethernet interface of a port */
	struct net_if *(*get_port_iface)(const struct device *dev, uint8_t port_idx);
};

/**
 * @brief Add or remove a filtering entry
 *
 * Adding an entry for a database, an address and vids that already has one replaces it. To remove
 * an entry only the database-id, the address and the vids matter.
 *
 * @param dev Switch device
 * @param entry The filtering entry
 * @param set true to add the entry, false to remove it
 *
 * @retval 0 The entry is added or removed.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOSPC The device has no room left for the entry.
 * @retval -ENOENT The entry to remove does not exist.
 * @retval -ENOTSUP The device cannot do it, for instance it has no extended lookup or no stream
 *                  identification.
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
 * @param port_map The control-element of each port, as in a filtering entry
 * @param port_map_len Number of port-map entries
 *
 * @retval 0 The default is set.
 * @retval -EINVAL An argument is invalid.
 * @retval -ENOTSUP The device cannot do it.
 */
static inline int ethernet_switch_default_port_map_set(
	const struct device *dev, uint32_t input_port_list,
	const struct ethernet_switch_port_map *port_map, size_t port_map_len)
{
	const struct ethernet_switch_api *api = (const struct ethernet_switch_api *)dev->api;

	if (api == NULL || api->default_port_map_set == NULL) {
		return -ENOTSUP;
	}

	return api->default_port_map_set(dev, input_port_list, port_map, port_map_len);
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
