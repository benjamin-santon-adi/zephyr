/** @file
 * @brief Ethernet Bridge public header file
 *
 * Ethernet Bridges connect two or more Ethernet networks together and
 * transparently forward packets from one network to the others as if
 * they were part of the same network.
 */

/*
 * Copyright (c) 2021 BayLibre SAS
 * Copyright (c) 2024 Nordic Semiconductor
 * SPDX-FileCopyrightText: Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_ETHERNET_BRIDGE_H_
#define ZEPHYR_INCLUDE_NET_ETHERNET_BRIDGE_H_

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/net/virtual.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/net/ethernet.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ethernet Bridging API
 * @defgroup eth_bridge Ethernet Bridging API
 * @since 2.7
 * @version 0.8.0
 * @ingroup networking
 * @{
 */

/** @cond INTERNAL_HIDDEN */

#if defined(CONFIG_NET_ETHERNET_BRIDGE)
#define NET_ETHERNET_BRIDGE_ETH_INTERFACE_COUNT CONFIG_NET_ETHERNET_BRIDGE_ETH_INTERFACE_COUNT
#else
#define NET_ETHERNET_BRIDGE_ETH_INTERFACE_COUNT 1
#endif

#if defined(CONFIG_NET_ETHERNET_BRIDGE_HW)
#define NET_ETHERNET_BRIDGE_HW_INTERFACE_COUNT CONFIG_NET_ETHERNET_BRIDGE_HW_COUNT
#else
#define NET_ETHERNET_BRIDGE_HW_INTERFACE_COUNT 0
#endif

/* Forwarding state of a bridge port, as in IEEE 802.1D-2004 7.4 */
enum eth_bridge_port_state {
	/* Frames are neither forwarded nor learned from */
	ETH_BRIDGE_PORT_STATE_DISCARDING = 0,
	/* Source addresses are learned, frames are not forwarded */
	ETH_BRIDGE_PORT_STATE_LEARNING,
	/* Frames are learned from and forwarded */
	ETH_BRIDGE_PORT_STATE_FORWARDING,
};

/* One Ethernet interface bridged by a bridge interface */
struct eth_bridge_port {
	/* The bridged Ethernet interface, NULL if the slot is free */
	struct net_if *iface;

	/* Port state, an enum eth_bridge_port_state. Read without the
	 * bridge lock in the data path.
	 */
	atomic_t state;
};

/** @endcond */

/**
 * @brief FDB entry of a bridge that forwards in hardware
 */
struct eth_bridge_hw_fdb_entry {
	/** Destination MAC address, unicast or multicast */
	struct net_eth_addr mac;
	/** Bridge ports the frames for @ref mac are sent out of, bit n for the
	 *  port n, whose interface eth_bridge_get_port_iface() returns
	 */
	uint32_t portmap;
	/** The frames for @ref mac are also passed to the host */
	bool host_rx;
};

/**
 * @brief Configuration types of a device that forwards between the ports of a bridge
 */
enum eth_bridge_hw_config_type {
	/** Set or get whether the device forwards between the ports, bool forwarding */
	ETH_BRIDGE_HW_CONFIG_TYPE_FORWARDING,
	/** Set the FDB entry, or replace the one for its address, struct fdb */
	ETH_BRIDGE_HW_CONFIG_TYPE_FDB_SET,
	/** Remove the FDB entry of the address, only the address of fdb is set */
	ETH_BRIDGE_HW_CONFIG_TYPE_FDB_DEL,
};

/**
 * @brief Configuration of a device that forwards between the ports of a bridge
 */
struct eth_bridge_hw_config {
	/** The bridge of the ports, set for the FDB types */
	struct net_if *br;
	union {
		/** Forwarding between the ports is enabled */
		bool forwarding;
		/** FDB entry */
		struct eth_bridge_hw_fdb_entry fdb;
	};
};

/**
 * @brief Driver API of a device that forwards between the ports of a bridge
 *
 * Like the configuration of an Ethernet device, see @ref ethernet_api. The FDB
 * types are called with the FDB locked, so they must not call the FDB
 * functions.
 */
struct eth_bridge_hw_api {
	/** Set a configuration. Returns 0 or a negative errno, -ENOTSUP if the type is not
	 *  supported.
	 */
	int (*set_config)(const struct device *dev, enum eth_bridge_hw_config_type type,
			  const struct eth_bridge_hw_config *config);
	/** Get a configuration, NULL if no type can be got. Returns 0 or a negative errno,
	 *  -ENOTSUP if the type is not supported.
	 */
	int (*get_config)(const struct device *dev, enum eth_bridge_hw_config_type type,
			  struct eth_bridge_hw_config *config);
	/** Get the number of ports of the device, 32 at most */
	size_t (*get_number_iface)(const struct device *dev);
	/** Get the interface of the port @p port, NULL if there is none */
	struct net_if *(*get_port_iface)(const struct device *dev, size_t port);
};

/**
 * @brief Configuration of the bridge of a device that forwards between its ports in hardware
 *
 * The configuration of the device of the bridge interface, see ETH_BRIDGE_HW_DEFINE(). It is
 * NULL for a bridge that forwards in software.
 */
struct eth_bridge_hw_dev_config {
	/** The device that forwards between the ports, the first argument of the API */
	const struct device *dev;
	/** API of the device */
	const struct eth_bridge_hw_api *api;
};

/** @cond INTERNAL_HIDDEN */

struct eth_bridge_iface_context {
	/* Lock to protect access to the port array below */
	struct k_mutex lock;

	/* The actual bridge virtual interface  */
	struct net_if *iface;

	/* What Ethernet interfaces are bridged together */
	struct eth_bridge_port ports[NET_ETHERNET_BRIDGE_ETH_INTERFACE_COUNT];

	/* How many interfaces are bridged atm */
	size_t count;

	/* The device forwards between the ports. If not, the bridge forwards in
	 * software, as a bridge of any ports.
	 */
	bool hw_forwarding;

	/* The ports are those of the device, no other interface is added */
	bool hw_locked;

	/* Link state of the ports of the device, bit n for the port n */
	uint32_t hw_link;

	/* Bridge instance id, the ones of devices that forward in hardware follow
	 * the CONFIG_NET_ETHERNET_BRIDGE_COUNT others, and are given at the
	 * initialization of the interface when it is -1
	 */
	int id;

	/* Has user configured the bridge */
	bool is_setup : 1;

	/* Is the interface enabled or not */
	bool status : 1;
};

/** @endcond */

/**
 * @brief Add an Ethernet network interface to a bridge
 *
 * This adds a network interface to a bridge. The interface is then put
 * into promiscuous mode. After more than one Ethernet interfaces are
 * added to the bridge interface, the bridge interface is setup.
 * After the setup is done, the bridge interface can be brought up so
 * that it can start bridging L2 traffic.
 *
 * @param br A pointer to a bridge interface
 * @param iface Interface to add
 *
 * @return 0 if OK, negative error code otherwise. -ENOTSUP if the spanning
 *         tree protocol runs on @p br and @p iface forwards frames in hardware
 *         that its device cannot block, see eth_bridge_stp_enable().
 */
int eth_bridge_iface_add(struct net_if *br, struct net_if *iface);

/**
 * @brief Remove an Ethernet network interface from a bridge.
 *
 * If the bridge interface setup has only one Ethernet interface left
 * after this function call, the bridge is disabled as it cannot bridge
 * the L2 traffic any more. The bridge interface is left in UP state
 * if this case.
 *
 * @param br A pointer to a bridge interface
 * @param iface Interface to remove
 *
 * @return 0 if OK, negative error code otherwise.
 */
int eth_bridge_iface_remove(struct net_if *br, struct net_if *iface);

/**
 * @brief Get a bridge interface that is not in use
 *
 * Returns a bridge without ports that does not forward in hardware, for a
 * driver to bridge its ports at start up.
 *
 * @return The bridge interface, or NULL if there is none.
 */
struct net_if *eth_bridge_get_free(void);

/**
 * @brief Enable or disable the forwarding of the device of a bridge
 *
 * With the forwarding disabled the device passes every frame of its ports to the
 * host, and the bridge forwards in software, as a bridge of any ports does: it
 * learns, floods and can run the spanning tree protocol, see
 * eth_bridge_stp_enable(). The FDB entries of the bridge are removed when the
 * mode changes, as they are for the other mode.
 *
 * @param br A pointer to a bridge interface defined with ETH_BRIDGE_HW_DEFINE()
 * @param enable The device forwards between the ports
 *
 * @retval 0 on success, or if the device already is in that mode.
 * @retval -ENOTSUP @p br is not defined with ETH_BRIDGE_HW_DEFINE(), or the device
 *                  cannot stop forwarding.
 * @retval -EBUSY The spanning tree protocol runs, and @p enable is true.
 * @retval <0 Other error of the device.
 */
int eth_bridge_hw_forwarding_enable(struct net_if *br, bool enable);

/** @cond INTERNAL_HIDDEN */
extern const struct virtual_interface_api eth_bridge_virtual_api;
/** @endcond */

/**
 * @brief Bridge the ports of a device that forwards between them in hardware
 *
 * The init function of a bridge of ETH_BRIDGE_HW_DEFINE(). Adds the ports of the device to
 * the bridge, enables forwarding in the device with ETH_BRIDGE_HW_CONFIG_TYPE_FORWARDING
 * and takes the bridge up, without carrier until a port link is up, see
 * eth_bridge_hw_link_set(). The ports must be initialized.
 *
 * @param br The bridge interface
 *
 * @retval 0 on success.
 * @retval -EINVAL The API of the device is incomplete, or it has more than 32 ports.
 * @retval -ENODEV A port has no interface.
 * @retval <0 Other error of the bridge or of the device.
 */
int eth_bridge_hw_init(struct net_if *br);

/**
 * @brief Report the link state of a port to the bridge of its device
 *
 * The carrier of the bridge is set from the link state of its ports. Use the
 * link state of the PHY, as the carrier of an Ethernet interface is applied
 * later. It can be called before eth_bridge_hw_init(), the state is kept.
 *
 * @param br The bridge interface
 * @param port Index of the port
 * @param up The link of the port is up
 */
void eth_bridge_hw_link_set(struct net_if *br, size_t port, bool up);

/**
 * @brief Get the bridge interface defined by ETH_BRIDGE_HW_DEFINE()
 *
 * @param _name The name given to ETH_BRIDGE_HW_DEFINE()
 */
#define ETH_BRIDGE_HW_GET(_name) NET_IF_GET(_name, 0)

/**
 * @brief Define the bridge of the ports of a device that forwards between them in hardware
 *
 * Defines a bridge interface of its own, with the configuration (device and API) and the
 * data of the bridge, and an init function that bridges the ports
 * at boot with eth_bridge_hw_init(), in the APPLICATION level, once the network interfaces
 * are initialized. The interface is not auto started: it is up when a port link is, see
 * eth_bridge_hw_link_set().
 *
 * @param _name Name of the bridge, a C identifier
 * @param _hw_dev The device that forwards between its ports, a constant expression. It is
 *                passed to the API.
 * @param _api Pointer to the struct eth_bridge_hw_api of the device.
 * @param _prio The initialization priority in the APPLICATION level.
 */
#define ETH_BRIDGE_HW_DEFINE(_name, _hw_dev, _api, _prio)                    \
	static const struct eth_bridge_hw_dev_config _name##_config = {					\
		.dev = _hw_dev,									\
		.api = _api,										\
	};										\
	static struct eth_bridge_iface_context _name##_ctx;					\
												\
	NET_VIRTUAL_INTERFACE_INIT_INSTANCE(_name, #_name, 0, NULL, NULL, &_name##_ctx,	\
					    &_name##_config, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,		\
					    &eth_bridge_virtual_api, NET_ETH_MTU)			\
												\
	static struct eth_bridge_iface_context _name##_ctx = {					\
		.iface = ETH_BRIDGE_HW_GET(_name),						\
		.id = -1,										\
	};										\
												\
	static int _name##_init(void)								\
	{											\
		return eth_bridge_hw_init(ETH_BRIDGE_HW_GET(_name));				\
	}											\
												\
	SYS_INIT(_name##_init, APPLICATION, _prio)

/**
 * @brief Get the Ethernet interface of a bridge port
 *
 * @param br A pointer to a bridge interface
 * @param port Index of the port, the bit of a port map
 *
 * @return The interface of the port, or NULL if @p port is not in use.
 */
struct net_if *eth_bridge_get_port_iface(struct net_if *br, size_t port);

/**
 * @brief Get bridge index according to pointer
 *
 * @param br Pointer to bridge instance
 *
 * @return Bridge index
 */
int eth_bridge_get_index(struct net_if *br);

/**
 * @brief Get bridge instance according to index
 *
 * @param index Bridge instance index
 *
 * @return Pointer to bridge interface or NULL if not found.
 */
struct net_if *eth_bridge_get_by_index(int index);

/**
 * @typedef eth_bridge_cb_t
 * @brief Callback used while iterating over bridge instances
 *
 * @param br Pointer to bridge context instance
 * @param user_data User supplied data
 */
typedef void (*eth_bridge_cb_t)(struct eth_bridge_iface_context *br, void *user_data);

/**
 * @brief Go through all the bridge context instances in order to get
 *        information about them. This is mainly useful in
 *        net-shell to print data about currently active bridges.
 *
 * @param cb Callback to call for each bridge instance
 * @param user_data User supplied data
 */
void net_eth_bridge_foreach(eth_bridge_cb_t cb, void *user_data);

/**
 * @brief Check if the iface is bridged.
 *
 * @param ctx Pointer to ethernet_context
 *
 * @return true if bridged, or false.
 */
static inline bool net_eth_iface_is_bridged(struct ethernet_context *ctx)
{
#if defined(CONFIG_NET_ETHERNET_BRIDGE)
	struct eth_bridge_iface_context *br_ctx;
	const struct device *br_dev;

	NET_ASSERT(ctx != NULL);

	if (ctx->bridge == NULL) {
		return false;
	}

	br_dev = net_if_get_device(ctx->bridge);

	br_ctx = (struct eth_bridge_iface_context *)br_dev->data;
	if (br_ctx->is_setup) {
		return true;
	}
#endif
	return false;
}

/**
 * @brief Get bridge iface.
 *
 * @param ctx Pointer to ethernet_context
 *
 * @return Pointer to bridge iface, or NULL.
 */
static inline struct net_if *net_eth_get_bridge(struct ethernet_context *ctx)
{
#if defined(CONFIG_NET_ETHERNET_BRIDGE)
	NET_ASSERT(ctx != NULL);

	return ctx->bridge;
#else
	return NULL;
#endif
}

/**
 * @brief Process pkt received on bridged interface.
 *
 * @param iface Pointer to bridged iface
 * @param pkt Pointer to pkt
 * @param dst_iface Pointer to pointer to destination iface
 *
 * @return net_verdict.
 */
enum net_verdict eth_bridge_input_process(struct net_if *iface, struct net_pkt *pkt,
					  struct net_if **dst_iface);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_ETHERNET_BRIDGE_H_ */
