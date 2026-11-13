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
	uint32_t forward;
	/** The frames are received by the host, on the interface of the port they came in by */
	bool receive;
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
 * @brief Port interface of a switch device
 *
 * A switch device has one interface for the host and one interface per physical port, all bound
 * to the same device. They are created by ETH_NET_SWITCH_DEVICE_DT_INST_DEFINE().
 */
struct ethernet_switch_port {
	/** Value of the reg property of the port node */
	uint8_t port;
	/** Name of the port node */
	const char *name;
	/** Interface of the port */
	struct net_if *iface;
};

/** Port interfaces of a switch device, created by ETH_NET_SWITCH_PORTS_DT_INST_DEFINE() */
struct ethernet_switch_ports {
	/** Number of port interfaces */
	size_t num;
	/** The port interfaces */
	const struct ethernet_switch_port *ports;
};

/**
 * @brief Get the port of a port interface
 *
 * @param ports Port interfaces of the device, can be NULL
 * @param iface Interface
 *
 * @return The reg value of the port node, or -1 if @p iface is not the interface of a port.
 */
static inline int ethernet_switch_iface_port(const struct ethernet_switch_ports *ports,
					     const struct net_if *iface)
{
	for (size_t i = 0; ports != NULL && i < ports->num; i++) {
		if (ports->ports[i].iface == iface) {
			return ports->ports[i].port;
		}
	}

	return -1;
}

/**
 * @brief Get the interface of a port
 *
 * @param ports Port interfaces of the device, can be NULL
 * @param port Value of the reg property of the port node
 *
 * @return The interface of the port, or NULL if there is none.
 */
static inline struct net_if *ethernet_switch_port_iface(const struct ethernet_switch_ports *ports,
							uint8_t port)
{
	for (size_t i = 0; ports != NULL && i < ports->num; i++) {
		if (ports->ports[i].port == port) {
			return ports->ports[i].iface;
		}
	}

	return NULL;
}

/**
 * @brief Get the name of a port
 *
 * @param ports Port interfaces of the device, can be NULL
 * @param port Value of the reg property of the port node
 *
 * @return The name of the port node, or NULL if there is no such port.
 */
static inline const char *ethernet_switch_port_name(const struct ethernet_switch_ports *ports,
						    uint8_t port)
{
	for (size_t i = 0; ports != NULL && i < ports->num; i++) {
		if (ports->ports[i].port == port) {
			return ports->ports[i].name;
		}
	}

	return NULL;
}

/** @cond INTERNAL_HIDDEN */

/* Instance of the interface of a port, 0 is the host interface */
#define Z_ETH_SWITCH_PORT_INSTANCE(child) UTIL_INC(DT_REG_ADDR(child))

#define Z_ETH_SWITCH_PORT_DECLARE(child, is_port, inst)				\
	IF_ENABLED(is_port(child, inst),					\
		   (NET_IF_DT_INST_DECLARE(inst, Z_ETH_SWITCH_PORT_INSTANCE(child));))

#define Z_ETH_SWITCH_PORT_ENTRY(child, is_port, inst)				\
	IF_ENABLED(is_port(child, inst),					\
		   ({								\
			.port = DT_REG_ADDR(child),				\
			.name = DEVICE_DT_NAME(child),				\
			.iface = NET_IF_DT_INST_GET(inst,			\
						    Z_ETH_SWITCH_PORT_INSTANCE(child)),	\
		   },))

#define Z_ETH_SWITCH_PORT_IFACE_DEFINE(child, is_port, inst, mtu)		\
	IF_ENABLED(is_port(child, inst),					\
		   (NET_DEVICE_DT_INST_ADD_IFACE(inst, ETHERNET_L2,		\
						 NET_L2_GET_CTX_TYPE(ETHERNET_L2), mtu,	\
						 Z_ETH_SWITCH_PORT_INSTANCE(child));))

/** @endcond */

/**
 * @brief Get the port interfaces created by ETH_NET_SWITCH_PORTS_DT_INST_DEFINE()
 *
 * @param inst Driver instance number
 */
#define ETH_NET_SWITCH_PORTS_DT_INST_GET(inst) (&_CONCAT(ethernet_switch_ports_, inst))

/**
 * @brief Create the table of the port interfaces of a switch device
 *
 * Put it before the config of the device, which refers to the table, and use it with
 * ETH_NET_SWITCH_DEVICE_DT_INST_DEFINE() with the same arguments. The ports are the children of
 * @p parent that @p is_port tells.
 *
 * @param inst Driver instance number
 * @param parent Node whose children are the ports
 * @param is_port Macro called as is_port(child, inst), 1 if the child is a port that has an
 *                interface and 0 if not
 */
#define ETH_NET_SWITCH_PORTS_DT_INST_DEFINE(inst, parent, is_port)			\
	DT_FOREACH_CHILD_VARGS(parent, Z_ETH_SWITCH_PORT_DECLARE, is_port, inst)	\
	static const struct ethernet_switch_port _CONCAT(ethernet_switch_port_array_, inst)[] = { \
		DT_FOREACH_CHILD_VARGS(parent, Z_ETH_SWITCH_PORT_ENTRY, is_port, inst)	\
	};										\
	static const struct ethernet_switch_ports _CONCAT(ethernet_switch_ports_, inst) = { \
		.num = ARRAY_SIZE(_CONCAT(ethernet_switch_port_array_, inst)),		\
		.ports = _CONCAT(ethernet_switch_port_array_, inst),			\
	}

/**
 * @brief Create an Ethernet switch device with an interface for the host and one per port
 *
 * Like ETH_NET_DEVICE_DT_INST_DEFINE(), which creates the device and the interface of the host
 * (instance 0), and then one more interface bound to the same device for each port. The table
 * of ETH_NET_SWITCH_PORTS_DT_INST_DEFINE() tells which interface is which.
 *
 * @param inst Driver instance number
 * @param parent Node whose children are the ports
 * @param is_port Macro called as is_port(child, inst), 1 if the child is a port that has an
 *                interface and 0 if not
 * @param mtu Maximum transfer unit of the interfaces
 * @param init_fn Init function of the device
 * @param pm Reference to struct pm_device of the device, or NULL
 * @param data Pointer to the data of the device
 * @param config Pointer to the config of the device
 * @param prio Init priority of the device
 * @param api Pointer to the Ethernet API of the device
 */
#define ETH_NET_SWITCH_DEVICE_DT_INST_DEFINE(inst, parent, is_port, mtu, init_fn, pm, data,	\
					     config, prio, api)				\
	ETH_NET_DEVICE_DT_INST_DEFINE(inst, init_fn, pm, data, config, prio, api, mtu);	\
	DT_FOREACH_CHILD_VARGS(parent, Z_ETH_SWITCH_PORT_IFACE_DEFINE, is_port, inst, mtu)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_ETHERNET_SWITCH_H_ */
