/*
 * Copyright (C) 2007 VMware, Inc. All rights reserved.
 *
 * The contents of this file are subject to the terms of the Common
 * Development and Distribution License (the "License") version 1.0
 * and no later version.  You may not use this file except in
 * compliance with the License.
 *
 * You can obtain a copy of the License at
 *         http://www.opensource.org/licenses/cddl1.php
 *
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 *
 */

#ifndef _VMXNET3_SOLARIS_H_
#define	_VMXNET3_SOLARIS_H_

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/vtrace.h>
#include <sys/dlpi.h>
#include <sys/strsun.h>
#include <sys/ethernet.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/pci.h>
#include <sys/strsubr.h>
#include <sys/pattr.h>
#include <sys/mac.h>
#include <sys/sockio.h>
#ifdef OPEN_SOLARIS
#include <sys/mac_provider.h>
#endif
#include <sys/mac_ether.h>
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/tcp.h>

#include "vm_basic_types.h"

#define	PCI_VENDOR_ID_VMWARE 0x15AD
#define	PCI_DEVICE_ID_VMWARE_VMXNET3 0x07B0

/*
 * Build numbers.
 */
#define	BUILD_NUMBER "build-420096"
#define	BUILD_NUMBER_NUMERIC 420096
#define	BUILD_NUMBER_NUMERIC_STRING "420096"
#define	PRODUCT_BUILD_NUMBER "product-build-6086"
#define	PRODUCT_BUILD_NUMBER_NUMERIC 6086
#define	PRODUCT_BUILD_NUMBER_NUMERIC_STRING "6086"

#include "vmxnet3_defs.h"

/*
 * In this file are stored all the definitions/helpers that the
 * DDI/DDK provides but that our toolchain currently lacks.
 * Nuke them the day the toolchain is refreshed.
 */
#define	DDI_INTR_PRI(pri) (void *)((uintptr_t)(pri))

#define	COMPAT_DDI_DEFINE_STREAM_OPS(XXname, XXidentify,	       \
				XXprobe, XXattach, XXdetach, XXreset, \
				XXgetinfo, XXflag, XXstream_tab)	\
	DDI_DEFINE_STREAM_OPS(XXname, XXidentify, XXprobe, XXattach, XXdetach, \
			XXreset, XXgetinfo, XXflag, XXstream_tab, \
			ddi_quiesce_not_supported)
#define	HW_LSO 0x10

#define	DB_LSOMSS(mp) ((mp)->b_datap->db_struioun.cksum.pad)

#define	ETHERTYPE_VLAN (0x8100)

/*
 * Vmxnet3s-specific data structures.
 */

typedef struct vmxnet3_dmabuf_t {
	caddr_t buf;
	uint64_t bufPA;
	size_t bufLen;
	ddi_dma_handle_t dmaHandle;
	ddi_acc_handle_t dataHandle;
} vmxnet3_dmabuf_t;

typedef struct vmxnet3_cmdring_t {
	vmxnet3_dmabuf_t dma;
	uint16_t size;
	uint16_t next2fill;
	uint16_t avail;
	uint8_t gen;
} vmxnet3_cmdring_t;

typedef struct vmxnet3_compring_t {
	vmxnet3_dmabuf_t dma;
	uint16_t size;
	uint16_t next2comp;
	uint8_t gen;
} vmxnet3_compring_t;

typedef struct vmxnet3_metatx_t {
	mblk_t *mp;
	uint16_t sopIdx;
	uint16_t frags;
} vmxnet3_metatx_t;

typedef struct vmxnet3_txqueue_t {
	vmxnet3_cmdring_t cmdRing;
	vmxnet3_compring_t compRing;
	vmxnet3_metatx_t *metaRing;
	Vmxnet3_TxQueueCtrl *sharedCtrl;
} vmxnet3_txqueue_t;

typedef struct vmxnet3_rxbuf_t {
	vmxnet3_dmabuf_t dma;
	mblk_t *mblk;
	frtn_t freeCB;
	struct vmxnet3_softc_t *dp;
	struct vmxnet3_rxbuf_t *next;
} vmxnet3_rxbuf_t;

typedef struct vmxnet3_bufdesc_t {
	vmxnet3_rxbuf_t *rxBuf;
} vmxnet3_bufdesc_t;

typedef struct vmxnet3_rxpool_t {
	vmxnet3_rxbuf_t *listHead;
	unsigned int nBufs;
	unsigned int nBufsLimit;
} vmxnet3_rxpool_t;

typedef struct vmxnet3_rxqueue_t {
	vmxnet3_cmdring_t cmdRing;
	vmxnet3_compring_t compRing;
	vmxnet3_bufdesc_t *bufRing;
	Vmxnet3_RxQueueCtrl *sharedCtrl;
} vmxnet3_rxqueue_t;

typedef struct vmxnet3_tx_cache_node {
	uint64_t pa;
	caddr_t  va;
} vmxnet3_tx_cache_node_t;

#define	VMXNET3_TX_CACHE_ITEMS_PER_PAGE		\
	(PAGESIZE / VMXNET3_HDR_COPY_SIZE)
#define	VMXNET3_TX_CACHE_MAX_PAGES				\
	(VMXNET3_TX_RING_MAX_SIZE / VMXNET3_TX_CACHE_ITEMS_PER_PAGE)

typedef struct vmxnet3_tx_cache {
	int num_pages;
	page_t **pages;
	caddr_t *page_maps;
	vmxnet3_tx_cache_node_t *nodes;
	int num_nodes;
	caddr_t vindow;
} vmxnet3_tx_cache_t;

typedef struct vmxnet3_softc_t {
	dev_info_t		*dip;
	int			instance;
	mac_handle_t		mac;

	ddi_acc_handle_t	pciHandle;
	ddi_acc_handle_t	bar0Handle, bar1Handle;
	caddr_t			bar0, bar1;

	boolean_t		devEnabled;
	uint8_t			macaddr[6];
	uint32_t		cur_mtu;
	link_state_t		linkState;
	uint64_t		linkSpeed;
	vmxnet3_dmabuf_t	sharedData;
	vmxnet3_dmabuf_t	queueDescs;

	kmutex_t		intrLock;
	int			intrType;
	int			intrMaskMode;
	int			intrCap;
	ddi_intr_handle_t	intrHandle;
	ddi_taskq_t		*resetTask;

	kmutex_t		txLock;
	vmxnet3_txqueue_t	txQueue;
	ddi_dma_handle_t	txDmaHandle;
	boolean_t		txMustResched;

	vmxnet3_rxqueue_t	rxQueue;
	kmutex_t		rxPoolLock;
	vmxnet3_rxpool_t	rxPool;
	volatile uint32_t	rxNumBufs;
	uint32_t		rxMode;

	vmxnet3_tx_cache_t	txCache;
	vmxnet3_dmabuf_t	mfTable;
} vmxnet3_softc_t;

int vmxnet3_alloc_dma_mem_1(vmxnet3_softc_t *dp, vmxnet3_dmabuf_t *dma,
			size_t size, boolean_t canSleep);
int vmxnet3_alloc_dma_mem_128(vmxnet3_softc_t *dp, vmxnet3_dmabuf_t *dma,
			size_t size, boolean_t canSleep);
int vmxnet3_alloc_dma_mem_512(vmxnet3_softc_t *dp, vmxnet3_dmabuf_t *dma,
			size_t size, boolean_t canSleep);
void vmxnet3_free_dma_mem(vmxnet3_dmabuf_t *dma);
int vmxnet3_getprop(vmxnet3_softc_t *dp, char *name,
		int min, int max, int def);

int vmxnet3_txqueue_init(vmxnet3_softc_t *dp, vmxnet3_txqueue_t *txq);
mblk_t *vmxnet3_tx(void *data, mblk_t *mps);
boolean_t vmxnet3_tx_complete(vmxnet3_softc_t *dp, vmxnet3_txqueue_t *txq);
void vmxnet3_txqueue_fini(vmxnet3_softc_t *dp, vmxnet3_txqueue_t *txq);

int vmxnet3_rxqueue_init(vmxnet3_softc_t *dp, vmxnet3_rxqueue_t *rxq);
mblk_t *vmxnet3_rx_intr(vmxnet3_softc_t *dp, vmxnet3_rxqueue_t *rxq);
void vmxnet3_rxqueue_fini(vmxnet3_softc_t *dp, vmxnet3_rxqueue_t *rxq);

void vmxnet3_debug(vmxnet3_softc_t *dip, int level, char *fmt, ...);
void vmxnet3_warn(vmxnet3_softc_t *dip, char *fmt, ...);

void mac_lso_get(mblk_t *mp, uint32_t *mss, uint32_t *flags);

extern ddi_device_acc_attr_t vmxnet3_dev_attr;

#define	VMXNET3_MODNAME "vmxnet3s"

#define	VMXNET3_DEBUG_LEVEL 1

#define	MACADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define	MACADDR_FMT_ARGS(mac) \
	mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]

/* Default ring size */
#define	VMXNET3_DEF_TX_RING_SIZE 256
#define	VMXNET3_DEF_RX_RING_SIZE 256

/* Register access helpers */
#define	VMXNET3_BAR0_GET32(Device, Reg) \
	ddi_get32((Device)->bar0Handle, (uint32_t *)((Device)->bar0 + (Reg)))
#define	VMXNET3_BAR0_PUT32(Device, Reg, Value) \
	ddi_put32((Device)->bar0Handle, (uint32_t *)((Device)->bar0 + (Reg)), \
		(Value))
#define	VMXNET3_BAR1_GET32(Device, Reg) \
	ddi_get32((Device)->bar1Handle, (uint32_t *)((Device)->bar1 + (Reg)))
#define	VMXNET3_BAR1_PUT32(Device, Reg, Value) \
	ddi_put32((Device)->bar1Handle, (uint32_t *)((Device)->bar1 + (Reg)), \
		(Value))

/* Misc helpers */
#define	VMXNET3_DS(Device) \
	((Vmxnet3_DriverShared *) (Device)->sharedData.buf)
#define	VMXNET3_TQDESC(Device)				\
	((Vmxnet3_TxQueueDesc *) (Device)->queueDescs.buf)
#define	VMXNET3_RQDESC(Device)						\
	((Vmxnet3_RxQueueDesc *) ((Device)->queueDescs.buf +		\
				sizeof (Vmxnet3_TxQueueDesc)))

#define	VMXNET3_ADDR_LO(addr) ((uint32_t)(addr))
#define	VMXNET3_ADDR_HI(addr) ((uint32_t)(((uint64_t)(addr)) >> 32))

#define	VMXNET3_GET_DESC(Ring, Idx) \
	(((Vmxnet3_GenericDesc *) (Ring)->dma.buf) + Idx)

/* Rings handling */
#define	VMXNET3_INC_RING_IDX(Ring, Idx) \
	do {				\
		(Idx)++;		\
		if ((Idx) == (Ring)->size) {	\
			(Idx) = 0;		\
			(Ring)->gen ^= 1;	\
		}				\
	} while (0)

#define	VMXNET3_DEC_RING_IDX(Ring, Idx) \
	do {				\
		if ((Idx) == 0) {	\
			(Idx) = (Ring)->size;	\
			(Ring)->gen ^= 1;	\
		}				\
		(Idx)--;			\
	} while (0)

#endif /* _VMXNET3_SOLARIS_H_ */
