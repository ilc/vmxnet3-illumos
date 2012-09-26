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
 */

#include "vmxnet3_solaris.h"
#include <sys/kmem.h>
#include <vm/seg_kmem.h>
#include <sys/mac_provider.h>
#include <sys/note.h>

/*
 * TODO:
 *    - Tx data ring
 *    - MAC_CAPAB_POLL support
 *    - JF support
 *    - Dynamic RX pool
 */

/*
 * Forward declarations
 */
static int vmxnet3_getstat(void *, uint_t, uint64_t *);
static int vmxnet3_start(void *);
static void vmxnet3_stop(void *);
static int vmxnet3_setpromisc(void *, boolean_t);
static int vmxnet3_multicst(void *, boolean_t, const uint8_t *);
static int vmxnet3_unicst(void *, const uint8_t *);
static boolean_t vmxnet3_getcapab(void *, mac_capab_t, void *);

static int vmxnet3_m_getprop(void *, const char *, mac_prop_id_t, uint_t,
    void *);
static int vmxnet3_m_setprop(void *, const char *, mac_prop_id_t, uint_t,
    const void *);
static void vmxnet3_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);

/* MAC callbacks */
static mac_callbacks_t vmxnet3_mac_callbacks = {
	.mc_callbacks = MC_GETCAPAB | MC_PROPERTIES,
	.mc_getstat = vmxnet3_getstat,
	.mc_start = vmxnet3_start,
	.mc_stop = vmxnet3_stop,
	.mc_setpromisc = vmxnet3_setpromisc,
	.mc_multicst = vmxnet3_multicst,
	.mc_unicst = vmxnet3_unicst,
	.mc_tx = vmxnet3_tx,
	.mc_getcapab = *vmxnet3_getcapab,
	.mc_setprop = vmxnet3_m_setprop,
	.mc_getprop = vmxnet3_m_getprop,
	.mc_propinfo = vmxnet3_m_propinfo,
};

/* Tx DMA engine description */
static ddi_dma_attr_t vmxnet3_dma_attrs_tx = {
	DMA_ATTR_V0, /* dma_attr_version */
	0x0000000000000000ull, /* dma_attr_addr_lo */
	0xFFFFFFFFFFFFFFFFull, /* dma_attr_addr_hi */
	0xFFFFFFFFFFFFFFFFull, /* dma_attr_count_max */
	0x0000000000000001ull, /* dma_attr_align */
	0x0000000000000001ull, /* dma_attr_burstsizes */
	0x00000001, /* dma_attr_minxfer */
	0x000000000000FFFFull, /* dma_attr_maxxfer */
	0xFFFFFFFFFFFFFFFFull, /* dma_attr_seg */
	-1, /* dma_attr_sgllen */
	0x00000001, /* dma_attr_granular */
	0  /* dma_attr_flags */
};

/*
 * vmxnet3_getstat --
 *
 *    Fetch the statistics of a vmxnet3 device.
 *
 * Results:
 *    DDI_FAILURE.
 *
 * Side effects:
 *    None.
 */
static int
vmxnet3_getstat(void *data, uint_t stat, uint64_t *val)
{
	vmxnet3_softc_t *dp = data;
	UPT1_TxStats *txStats;
	UPT1_RxStats *rxStats;

	vmxnet3_debug(dp, 3, "getstat(%u)\n", stat);

	if (!dp->devEnabled) {
		return (DDI_FAILURE);
	}

	txStats = &VMXNET3_TQDESC(dp)->stats;
	rxStats = &VMXNET3_RQDESC(dp)->stats;

	/*
	 * First touch the related register
	 */
	switch (stat) {
	case MAC_STAT_MULTIRCV:
	case MAC_STAT_BRDCSTRCV:
	case MAC_STAT_MULTIXMT:
	case MAC_STAT_BRDCSTXMT:
	case MAC_STAT_NORCVBUF:
	case MAC_STAT_IERRORS:
	case MAC_STAT_NOXMTBUF:
	case MAC_STAT_OERRORS:
	case MAC_STAT_RBYTES:
	case MAC_STAT_IPACKETS:
	case MAC_STAT_OBYTES:
	case MAC_STAT_OPACKETS:
		VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD, VMXNET3_CMD_GET_STATS);
		break;
	case MAC_STAT_IFSPEED:
	case MAC_STAT_COLLISIONS:
	case ETHER_STAT_LINK_DUPLEX:
		/* nothing */
		break;
	default:
		return (DDI_FAILURE);
	}

	/*
	 * Then fetch the corresponding stat
	 */
	switch (stat) {
	case MAC_STAT_IFSPEED:
		*val = dp->linkSpeed;
		break;
	case MAC_STAT_MULTIRCV:
		*val = rxStats->mcastPktsRxOK;
		break;
	case MAC_STAT_BRDCSTRCV:
		*val = rxStats->bcastPktsRxOK;
		break;
	case MAC_STAT_MULTIXMT:
		*val = txStats->mcastPktsTxOK;
		break;
	case MAC_STAT_BRDCSTXMT:
		*val = txStats->bcastPktsTxOK;
		break;
	case MAC_STAT_NORCVBUF:
		*val = rxStats->pktsRxOutOfBuf;
		break;
	case MAC_STAT_IERRORS:
		*val = rxStats->pktsRxError;
		break;
	case MAC_STAT_NOXMTBUF:
		*val = txStats->pktsTxDiscard;
		break;
	case MAC_STAT_OERRORS:
		*val = txStats->pktsTxError;
		break;
	case MAC_STAT_COLLISIONS:
		*val = 0;
		break;
	case MAC_STAT_RBYTES:
		*val = rxStats->LROBytesRxOK +
		    rxStats->ucastBytesRxOK +
		    rxStats->mcastBytesRxOK +
		    rxStats->bcastBytesRxOK;
		break;
	case MAC_STAT_IPACKETS:
		*val = rxStats->LROPktsRxOK +
		    rxStats->ucastPktsRxOK +
		    rxStats->mcastPktsRxOK +
		    rxStats->bcastPktsRxOK;
		break;
	case MAC_STAT_OBYTES:
		*val = txStats->TSOBytesTxOK +
		    txStats->ucastBytesTxOK +
		    txStats->mcastBytesTxOK +
		    txStats->bcastBytesTxOK;
		break;
	case MAC_STAT_OPACKETS:
		*val = txStats->TSOPktsTxOK +
		    txStats->ucastPktsTxOK +
		    txStats->mcastPktsTxOK +
		    txStats->bcastPktsTxOK;
		break;
	case ETHER_STAT_LINK_DUPLEX:
		*val = LINK_DUPLEX_FULL;
		break;
	default:
		ASSERT(B_FALSE);
	}

	return (DDI_SUCCESS);
}

/*
 *
 * vmxnet3_prepare_drivershared --
 *
 *    Allocate and initialize the shared data structures
 *    of a vmxnet3 device.
 *
 * Results:
 *    DDI_SUCCESS or DDI_FAILURE.
 *
 * Side effects:
 *    None.
 *
 */
static int
vmxnet3_prepare_drivershared(vmxnet3_softc_t *dp)
{
	Vmxnet3_DriverShared *ds;
	size_t allocSize = sizeof (Vmxnet3_DriverShared);

	if (vmxnet3_alloc_dma_mem_1(dp, &dp->sharedData, allocSize,
	    B_TRUE) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	ds = VMXNET3_DS(dp);
	(void) memset(ds, 0, allocSize);

	allocSize = sizeof (Vmxnet3_TxQueueDesc) + sizeof (Vmxnet3_RxQueueDesc);
	if (vmxnet3_alloc_dma_mem_128(dp, &dp->queueDescs, allocSize,
	    B_TRUE) != DDI_SUCCESS) {
		vmxnet3_free_dma_mem(&dp->sharedData);
		return (DDI_FAILURE);
	}
	(void) memset(dp->queueDescs.buf, 0, allocSize);

	ds->magic = VMXNET3_REV1_MAGIC;

	/* Take care of most of devRead */
	ds->devRead.misc.driverInfo.version = BUILD_NUMBER_NUMERIC;
	if (sizeof (void *) == 4) {
		ds->devRead.misc.driverInfo.gos.gosBits = VMXNET3_GOS_BITS_32;
	} else if (sizeof (void *) == 8) {
		ds->devRead.misc.driverInfo.gos.gosBits = VMXNET3_GOS_BITS_64;
	} else {
		ASSERT(B_FALSE);
	}
	ds->devRead.misc.driverInfo.gos.gosType = VMXNET3_GOS_TYPE_SOLARIS;
	ds->devRead.misc.driverInfo.gos.gosVer = 10;
	ds->devRead.misc.driverInfo.vmxnet3RevSpt = 1;
	ds->devRead.misc.driverInfo.uptVerSpt = 1;

	ds->devRead.misc.uptFeatures = UPT1_F_RXCSUM;
	ds->devRead.misc.mtu = dp->cur_mtu;

	// XXX: ds->devRead.misc.maxNumRxSG
	ds->devRead.misc.numTxQueues = 1;
	ds->devRead.misc.numRxQueues = 1;
	ds->devRead.misc.queueDescPA = dp->queueDescs.bufPA;
	ds->devRead.misc.queueDescLen = allocSize;

	/* TxQueue and RxQueue information is filled in other functions */

	ds->devRead.intrConf.autoMask = (dp->intrMaskMode == VMXNET3_IMM_AUTO);
	ds->devRead.intrConf.numIntrs = 1;
	// XXX: ds->intr.modLevels
	ds->devRead.intrConf.eventIntrIdx = 0;

	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_DSAL,
	    VMXNET3_ADDR_LO(dp->sharedData.bufPA));
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_DSAH,
	    VMXNET3_ADDR_HI(dp->sharedData.bufPA));

	return (DDI_SUCCESS);
}

/*
 * vmxnet3_destroy_drivershared --
 *
 *    Destroy the shared data structures of a vmxnet3 device.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 */
static void
vmxnet3_destroy_drivershared(vmxnet3_softc_t *dp)
{
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_DSAL, 0);
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_DSAH, 0);

	vmxnet3_free_dma_mem(&dp->queueDescs);
	vmxnet3_free_dma_mem(&dp->sharedData);
}

/*
 *
 * vmxnet3_alloc_cmdring --
 *
 *    Allocate and initialize the command ring of a queue.
 *
 * Results:
 *    DDI_SUCCESS or DDI_FAILURE.
 *
 * Side effects:
 *    None.
 *
 */
static int
vmxnet3_alloc_cmdring(vmxnet3_softc_t *dp, vmxnet3_cmdring_t *cmdRing)
{
	size_t ringSize = cmdRing->size * sizeof (Vmxnet3_TxDesc);

	if (vmxnet3_alloc_dma_mem_512(dp, &cmdRing->dma, ringSize,
	    B_TRUE) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	(void) memset(cmdRing->dma.buf, 0, ringSize);
	cmdRing->avail = cmdRing->size;
	cmdRing->next2fill = 0;
	cmdRing->gen = VMXNET3_INIT_GEN;

	return (DDI_SUCCESS);
}

/*
 * vmxnet3_alloc_compring --
 *
 *    Allocate and initialize the completion ring of a queue.
 *
 * Results:
 *    DDI_SUCCESS or DDI_FAILURE.
 *
 * Side effects:
 *    None.
 *
 */
static int
vmxnet3_alloc_compring(vmxnet3_softc_t *dp, vmxnet3_compring_t *compRing)
{
	size_t ringSize = compRing->size * sizeof (Vmxnet3_TxCompDesc);

	if (vmxnet3_alloc_dma_mem_512(dp, &compRing->dma, ringSize,
	    B_TRUE) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	(void) memset(compRing->dma.buf, 0, ringSize);
	compRing->next2comp = 0;
	compRing->gen = VMXNET3_INIT_GEN;

	return (DDI_SUCCESS);
}

void
vmxnet3_tx_cache_release(vmxnet3_softc_t *dp)
{
	int i, rc;
	vmxnet3_tx_cache_t *cache = &dp->txCache;

	/* Unmap pages. */
	hat_unload(kas.a_hat, cache->vindow, ptob(cache->num_pages),
	    HAT_UNLOAD_UNLOCK);
	vmem_free(heap_arena, cache->vindow, ptob(cache->num_pages));

	/* Free pages. */
	for (i = 0; i < cache->num_pages; i++) {
		rc = page_tryupgrade(cache->pages[i]);
		if (!rc) {
			page_unlock(cache->pages[i]);
			while (!page_lock(cache->pages[i], SE_EXCL, NULL,
			    P_RECLAIM))
				;
		}
		page_free(cache->pages[i], 0);
	}
	page_unresv(cache->num_pages);

	kmem_free(cache->pages, cache->num_pages * sizeof (page_t *));
	kmem_free(cache->page_maps, cache->num_pages * sizeof (page_t *));
	kmem_free(cache->nodes,
	    cache->num_nodes * sizeof (vmxnet3_tx_cache_node_t));
}

int
vmxnet3_tx_cache_init(vmxnet3_softc_t *dp, vmxnet3_txqueue_t *txq)
{
	int i;
	vmxnet3_tx_cache_t *cache = &dp->txCache;
	struct seg kseg;
	page_t *page;
	int ndescrs, node;

	cache->num_pages = ((txq->cmdRing.size*VMXNET3_HDR_COPY_SIZE)+
	    (PAGESIZE-1)) / PAGESIZE;

	/* Allocate pages. */
	if (!page_resv(cache->num_pages, KM_SLEEP)) {
		vmxnet3_warn(dp, "Failed to reserve %d pages.",
		    cache->num_pages);
		goto out;
	}

	if (!page_create_wait(cache->num_pages, 0)) {
		vmxnet3_warn(dp, "Failed to create %d pages.",
		    cache->num_pages);
		goto unresv_pages;
	}

	cache->pages = kmem_zalloc(cache->num_pages * sizeof (page_t *),
	    KM_SLEEP);
	ASSERT(cache->pages);

	cache->page_maps = kmem_zalloc(cache->num_pages * sizeof (page_t *),
	    KM_SLEEP);
	ASSERT(cache->page_maps);

	kseg.s_as = &kas;
	for (i = 0; i < cache->num_pages; i++) {
		page = page_get_freelist(&kvp, 0, &kseg, (caddr_t)(i*PAGESIZE),
		    PAGESIZE, 0, NULL);
		if (page == NULL) {
			page = page_get_cachelist(&kvp, 0, &kseg,
			    (caddr_t)(i * PAGESIZE), 0, NULL);
			if (page == NULL) {
				goto free_pages;
			}
			if (!PP_ISAGED(page)) {
				page_hashout(page, NULL);
			}
		}
		PP_CLRFREE(page);
		PP_CLRAGED(page);
		cache->pages[i] = page;
	}

	for (i = 0; i < cache->num_pages; i++) {
		page_downgrade(cache->pages[i]);
	}

	/* Allocate virtual address range for mapping pages. */
	cache->vindow = vmem_alloc(heap_arena, ptob(cache->num_pages),
	    VM_SLEEP);
	ASSERT(cache->vindow);

	cache->num_nodes = txq->cmdRing.size;

	/* Map pages. */
	for (i = 0; i < cache->num_pages; i++) {
		cache->page_maps[i] = cache->vindow + ptob(i);
		hat_devload(kas.a_hat, cache->page_maps[i], ptob(1),
		    cache->pages[i]->p_pagenum,
		    PROT_READ | PROT_WRITE | HAT_STRICTORDER,
		    HAT_LOAD_LOCK);
	}

	/* Now setup cache items. */
	cache->nodes = kmem_zalloc(txq->cmdRing.size *
	    sizeof (vmxnet3_tx_cache_node_t), KM_SLEEP);
	ASSERT(cache->nodes);

	ndescrs = txq->cmdRing.size;
	node = 0;
	for (i = 0; i < cache->num_pages; i++) {
		int j, lim;
		caddr_t va;
		uint64_t pa;

		lim = (ndescrs <= VMXNET3_TX_CACHE_ITEMS_PER_PAGE) ? ndescrs :
		    VMXNET3_TX_CACHE_ITEMS_PER_PAGE;
		va = cache->page_maps[i];
		pa = cache->pages[i]->p_pagenum << PAGESHIFT;

		for (j = 0; j < lim; j++) {
			cache->nodes[node].pa = pa;
			cache->nodes[node].va = va;

			pa += VMXNET3_HDR_COPY_SIZE;
			va += VMXNET3_HDR_COPY_SIZE;
			node++;
		}
		ndescrs -= lim;
	}
	return (DDI_SUCCESS);

free_pages:
	page_create_putback(cache->num_pages - i);
	while (--i >= 0) {
		if (!page_tryupgrade(cache->pages[i])) {
			page_unlock(cache->pages[i]);
			while (!page_lock(cache->pages[i], SE_EXCL, NULL,
			    P_RECLAIM))
				;
		}
		page_free(cache->pages[i], 0);
	}
	kmem_free(cache->pages, cache->num_pages * PAGESIZE);
unresv_pages:
	page_unresv(cache->num_pages);
out:
	cache->num_pages = cache->num_nodes = 0;
	return (DDI_FAILURE);
}


/*
 * vmxnet3_prepare_txqueue --
 *
 *    Initialize the tx queue of a vmxnet3 device.
 *
 * Results:
 *    DDI_SUCCESS or DDI_FAILURE.
 *
 * Side effects:
 *    None.
 *
 */
static int
vmxnet3_prepare_txqueue(vmxnet3_softc_t *dp)
{
	Vmxnet3_TxQueueDesc *tqdesc = VMXNET3_TQDESC(dp);
	vmxnet3_txqueue_t *txq = &dp->txQueue;

	ASSERT(!(txq->cmdRing.size & VMXNET3_RING_SIZE_MASK));
	ASSERT(!(txq->compRing.size & VMXNET3_RING_SIZE_MASK));
	ASSERT(!txq->cmdRing.dma.buf && !txq->compRing.dma.buf);

	if (vmxnet3_alloc_cmdring(dp, &txq->cmdRing) != DDI_SUCCESS) {
		goto error;
	}
	tqdesc->conf.txRingBasePA = txq->cmdRing.dma.bufPA;
	tqdesc->conf.txRingSize = txq->cmdRing.size;
	tqdesc->conf.dataRingBasePA = 0;
	tqdesc->conf.dataRingSize = 0;

	if (vmxnet3_alloc_compring(dp, &txq->compRing) != DDI_SUCCESS) {
		goto error_cmdring;
	}
	tqdesc->conf.compRingBasePA = txq->compRing.dma.bufPA;
	tqdesc->conf.compRingSize = txq->compRing.size;

	txq->metaRing = kmem_zalloc(txq->cmdRing.size *
	    sizeof (vmxnet3_metatx_t), KM_SLEEP);
	ASSERT(txq->metaRing);

	if (vmxnet3_tx_cache_init(dp, txq) != DDI_SUCCESS) {
		goto error_mpring;
	}

	if (vmxnet3_txqueue_init(dp, txq) != DDI_SUCCESS) {
		goto error_txcache;
	}

	return (DDI_SUCCESS);

error_txcache:
	vmxnet3_tx_cache_release(dp);
error_mpring:
	kmem_free(txq->metaRing, txq->cmdRing.size * sizeof (vmxnet3_metatx_t));
	vmxnet3_free_dma_mem(&txq->compRing.dma);
error_cmdring:
	vmxnet3_free_dma_mem(&txq->cmdRing.dma);
error:
	return (DDI_FAILURE);
}

/*
 * vmxnet3_prepare_rxqueue --
 *
 *    Initialize the rx queue of a vmxnet3 device.
 *
 * Results:
 *    DDI_SUCCESS or DDI_FAILURE.
 *
 * Side effects:
 *    None.
 */
static int
vmxnet3_prepare_rxqueue(vmxnet3_softc_t *dp)
{
	Vmxnet3_RxQueueDesc *rqdesc = VMXNET3_RQDESC(dp);
	vmxnet3_rxqueue_t *rxq = &dp->rxQueue;

	ASSERT(!(rxq->cmdRing.size & VMXNET3_RING_SIZE_MASK));
	ASSERT(!(rxq->compRing.size & VMXNET3_RING_SIZE_MASK));
	ASSERT(!rxq->cmdRing.dma.buf && !rxq->compRing.dma.buf);

	if (vmxnet3_alloc_cmdring(dp, &rxq->cmdRing) != DDI_SUCCESS) {
		goto error;
	}
	rqdesc->conf.rxRingBasePA[0] = rxq->cmdRing.dma.bufPA;
	rqdesc->conf.rxRingSize[0] = rxq->cmdRing.size;
	rqdesc->conf.rxRingBasePA[1] = 0;
	rqdesc->conf.rxRingSize[1] = 0;

	if (vmxnet3_alloc_compring(dp, &rxq->compRing) != DDI_SUCCESS) {
		goto error_cmdring;
	}
	rqdesc->conf.compRingBasePA = rxq->compRing.dma.bufPA;
	rqdesc->conf.compRingSize = rxq->compRing.size;

	rxq->bufRing = kmem_zalloc(rxq->cmdRing.size *
	    sizeof (vmxnet3_bufdesc_t), KM_SLEEP);
	ASSERT(rxq->bufRing);

	if (vmxnet3_rxqueue_init(dp, rxq) != DDI_SUCCESS) {
		goto error_bufring;
	}

	return (DDI_SUCCESS);

error_bufring:
	kmem_free(rxq->bufRing, rxq->cmdRing.size *
	    sizeof (vmxnet3_bufdesc_t));
	vmxnet3_free_dma_mem(&rxq->compRing.dma);
error_cmdring:
	vmxnet3_free_dma_mem(&rxq->cmdRing.dma);
error:
	return (DDI_FAILURE);
}

/*
 * vmxnet3_destroy_txqueue --
 *
 *    Destroy the tx queue of a vmxnet3 device.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 */
static void
vmxnet3_destroy_txqueue(vmxnet3_softc_t *dp)
{
	vmxnet3_txqueue_t *txq = &dp->txQueue;

	ASSERT(txq->metaRing);
	ASSERT(txq->cmdRing.dma.buf && txq->compRing.dma.buf);

	vmxnet3_txqueue_fini(dp, txq);

	kmem_free(txq->metaRing, txq->cmdRing.size*sizeof (vmxnet3_metatx_t));

	vmxnet3_free_dma_mem(&txq->cmdRing.dma);
	vmxnet3_free_dma_mem(&txq->compRing.dma);

	vmxnet3_tx_cache_release(dp);
}

/*
 * vmxnet3_destroy_rxqueue --
 *
 *    Destroy the rx queue of a vmxnet3 device.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 */
static void
vmxnet3_destroy_rxqueue(vmxnet3_softc_t *dp)
{
	vmxnet3_rxqueue_t *rxq = &dp->rxQueue;

	ASSERT(rxq->bufRing);
	ASSERT(rxq->cmdRing.dma.buf && rxq->compRing.dma.buf);

	vmxnet3_rxqueue_fini(dp, rxq);

	kmem_free(rxq->bufRing, rxq->cmdRing.size*sizeof (vmxnet3_bufdesc_t));

	vmxnet3_free_dma_mem(&rxq->cmdRing.dma);
	vmxnet3_free_dma_mem(&rxq->compRing.dma);
}

/*
 * vmxnet3_refresh_rxfilter --
 *
 *    Apply new RX filters settings to a vmxnet3 device.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 */
static void
vmxnet3_refresh_rxfilter(vmxnet3_softc_t *dp)
{
	Vmxnet3_DriverShared *ds = VMXNET3_DS(dp);

	ds->devRead.rxFilterConf.rxMode = dp->rxMode;
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD, VMXNET3_CMD_UPDATE_RX_MODE);
}

/*
 * vmxnet3_refresh_linkstate --
 *
 *    Fetch the link state of a vmxnet3 device.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 */
static void
vmxnet3_refresh_linkstate(vmxnet3_softc_t *dp)
{
	uint32_t ret32;

	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD, VMXNET3_CMD_GET_LINK);
	ret32 = VMXNET3_BAR1_GET32(dp, VMXNET3_REG_CMD);
	if (ret32 & 1) {
		dp->linkState = LINK_STATE_UP;
		dp->linkSpeed = (ret32 >> 16) * 1000000ULL;
	} else {
		dp->linkState = LINK_STATE_DOWN;
		dp->linkSpeed = 0;
	}
}

/*
 * vmxnet3_start --
 *
 *    Start a vmxnet3 device: allocate and initialize the shared data
 *    structures and send a start command to the device.
 *
 * Results:
 *    DDI_SUCCESS or DDI_FAILURE.
 *
 * Side effects:
 *    None.
 */
static int
vmxnet3_start(void *data)
{
	vmxnet3_softc_t *dp = data;
	Vmxnet3_TxQueueDesc *tqdesc;
	Vmxnet3_RxQueueDesc *rqdesc;
	int txQueueSize, rxQueueSize;
	uint32_t ret32;

	vmxnet3_debug(dp, 2, "start()\n");

	/*
	 * Allocate vmxnet3's shared data and advertise its PA
	 */
	if (vmxnet3_prepare_drivershared(dp) != DDI_SUCCESS) {
		vmxnet3_warn(dp, "vmxnet3_prepare_drivershared() failed\n");
		goto error;
	}
	tqdesc = VMXNET3_TQDESC(dp);
	rqdesc = VMXNET3_RQDESC(dp);

	/*
	 * Create and initialize the tx queue
	 */
	txQueueSize = vmxnet3_getprop(dp, "TxRingSize", 32, 4096,
	    VMXNET3_DEF_TX_RING_SIZE);
	if (!(txQueueSize & VMXNET3_RING_SIZE_MASK)) {
		dp->txQueue.cmdRing.size = txQueueSize;
		dp->txQueue.compRing.size = txQueueSize;
		dp->txQueue.sharedCtrl = &tqdesc->ctrl;
		if (vmxnet3_prepare_txqueue(dp) != DDI_SUCCESS) {
			vmxnet3_warn(dp, "vmxnet3_prepare_txqueue() failed\n");
			goto error_shared_data;
		}
	} else {
		vmxnet3_warn(dp, "invalid tx ring size (%d)\n", txQueueSize);
		goto error_shared_data;
	}

	/*
	 * Create and initialize the rx queue
	 */
	rxQueueSize = vmxnet3_getprop(dp, "RxRingSize", 32, 4096,
	    VMXNET3_DEF_RX_RING_SIZE);
	if (!(rxQueueSize & VMXNET3_RING_SIZE_MASK)) {
		dp->rxQueue.cmdRing.size = rxQueueSize;
		dp->rxQueue.compRing.size = rxQueueSize;
		dp->rxQueue.sharedCtrl = &rqdesc->ctrl;
		if (vmxnet3_prepare_rxqueue(dp) != DDI_SUCCESS) {
			vmxnet3_warn(dp, "vmxnet3_prepare_rxqueue() failed\n");
			goto error_tx_queue;
		}
	} else {
		vmxnet3_warn(dp, "invalid rx ring size (%d)\n", rxQueueSize);
		goto error_tx_queue;
	}

	/*
	 * Allocate the Tx DMA handle
	 */
	if (ddi_dma_alloc_handle(dp->dip, &vmxnet3_dma_attrs_tx, DDI_DMA_SLEEP,
	    NULL, &dp->txDmaHandle) != DDI_SUCCESS) {
		vmxnet3_warn(dp, "ddi_dma_alloc_handle() failed\n");
		goto error_rx_queue;
	}

	/*
	 * Activate the device
	 */
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD, VMXNET3_CMD_ACTIVATE_DEV);
	ret32 = VMXNET3_BAR1_GET32(dp, VMXNET3_REG_CMD);
	if (ret32) {
		vmxnet3_warn(dp, "ACTIVATE_DEV failed: 0x%x\n", ret32);
		goto error_txhandle;
	}
	dp->devEnabled = B_TRUE;

	VMXNET3_BAR0_PUT32(dp, VMXNET3_REG_RXPROD, dp->txQueue.cmdRing.size-1);

	/*
	 * Update the RX filters, must be done after ACTIVATE_DEV
	 */
	dp->rxMode = VMXNET3_RXM_UCAST | VMXNET3_RXM_BCAST;
	vmxnet3_refresh_rxfilter(dp);

	/*
	 * Get the link state now because no events will be generated
	 */
	vmxnet3_refresh_linkstate(dp);
	mac_link_update(dp->mac, dp->linkState);

	/*
	 * Finally, unmask the interrupt
	 */
	VMXNET3_BAR0_PUT32(dp, VMXNET3_REG_IMR, 0);

	return (DDI_SUCCESS);

error_txhandle:
	ddi_dma_free_handle(&dp->txDmaHandle);
error_rx_queue:
	vmxnet3_destroy_rxqueue(dp);
error_tx_queue:
	vmxnet3_destroy_txqueue(dp);
error_shared_data:
	vmxnet3_destroy_drivershared(dp);
error:
	return (DDI_FAILURE);
}

/*
 * vmxnet3_stop --
 *
 *    Stop a vmxnet3 device: send a stop command to the device and
 *    de-allocate the shared data structures.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 */
static void
vmxnet3_stop(void *data)
{
	vmxnet3_softc_t *dp = data;

	vmxnet3_debug(dp, 2, "stop()\n");

	/*
	 * Take the 2 locks related to asynchronous events.
	 * These events should always check dp->devEnabled before poking dp.
	 */
	mutex_enter(&dp->intrLock);
	mutex_enter(&dp->rxPoolLock);
	VMXNET3_BAR0_PUT32(dp, VMXNET3_REG_IMR, 1);
	dp->devEnabled = B_FALSE;
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD, VMXNET3_CMD_QUIESCE_DEV);
	mutex_exit(&dp->rxPoolLock);
	mutex_exit(&dp->intrLock);

	ddi_dma_free_handle(&dp->txDmaHandle);

	vmxnet3_destroy_rxqueue(dp);
	vmxnet3_destroy_txqueue(dp);

	vmxnet3_destroy_drivershared(dp);
}

/*
 * vmxnet3_setpromisc --
 *
 *    Set or unset promiscuous mode on a vmxnet3 device.
 *
 * Results:
 *    DDI_SUCCESS.
 *
 * Side effects:
 *    None.
 */
static int
vmxnet3_setpromisc(void *data, boolean_t promisc)
{
	vmxnet3_softc_t *dp = data;

	vmxnet3_debug(dp, 2, "setpromisc(%s)\n", promisc ? "TRUE" : "FALSE");

	if (promisc) {
		dp->rxMode |= VMXNET3_RXM_PROMISC;
	} else {
		dp->rxMode &= ~VMXNET3_RXM_PROMISC;
	}

	vmxnet3_refresh_rxfilter(dp);

	return (DDI_SUCCESS);
}

/*
 * vmxnet3_multicst --
 *
 *    Add or remove a multicast address from/to a vmxnet3 device.
 *
 * Results:
 *    DDI_FAILURE.
 *
 * Side effects:
 *    None.
 */
static int
vmxnet3_multicst(void *data, boolean_t add, const uint8_t *macaddr)
{
	vmxnet3_softc_t *dp = data;
	vmxnet3_dmabuf_t newMfTable;
	int ret = DDI_SUCCESS;
	uint16_t macIdx;

	vmxnet3_debug(dp, 2, "multicst(%s, "MACADDR_FMT")\n",
	    add ? "add" : "remove", MACADDR_FMT_ARGS(macaddr));

	/*
	 * First lookup the position of the given MAC to check if it is
	 * present in the existing MF table.
	 */
	for (macIdx = 0; macIdx < dp->mfTable.bufLen; macIdx += 6) {
		if (memcmp(&dp->mfTable.buf[macIdx], macaddr, 6) == 0) {
			break;
		}
	}

	/*
	 * Check for 2 situations we can handle gracefully by bailing out:
	 * Adding an already existing filter or removing a non-existing one.
	 */
	if (add && macIdx < dp->mfTable.bufLen) {
		vmxnet3_warn(dp, MACADDR_FMT " already in MC filter list @%u\n",
		    MACADDR_FMT_ARGS(macaddr), macIdx / 6);
		ASSERT(B_FALSE);
		goto done;
	}
	if (!add && macIdx == dp->mfTable.bufLen) {
		vmxnet3_warn(dp, MACADDR_FMT " not in MC filter list @ %u\n",
		    MACADDR_FMT_ARGS(macaddr), macIdx / 6);
		ASSERT(B_FALSE);
		goto done;
	}

	/*
	 * Create the new MF table
	 */
	{
		size_t allocSize = dp->mfTable.bufLen + (add ? 6 : -6);
		if (allocSize) {
			ret = vmxnet3_alloc_dma_mem_1(dp, &newMfTable,
			    allocSize, B_TRUE);
			ASSERT(ret == DDI_SUCCESS);
			if (add) {
				(void) memcpy(newMfTable.buf, dp->mfTable.buf,
				    dp->mfTable.bufLen);
				(void) memcpy(newMfTable.buf +
				    dp->mfTable.bufLen, macaddr, 6);
			} else {
				(void) memcpy(newMfTable.buf, dp->mfTable.buf,
				    macIdx);
				(void) memcpy(newMfTable.buf + macIdx,
				    dp->mfTable.buf + macIdx + 6,
				    dp->mfTable.bufLen - macIdx - 6);
			}
		} else {
			newMfTable.buf = NULL;
			newMfTable.bufPA = 0;
			newMfTable.bufLen = 0;
		}
	}

	/*
	 * Now handle 2 corner cases: if we're creating the first filter or
	 * removing the last one, we have to update rxMode accordingly.
	 */
	if (add && newMfTable.bufLen == 6) {
		ASSERT(!(dp->rxMode & VMXNET3_RXM_MCAST));
		dp->rxMode |= VMXNET3_RXM_MCAST;
		vmxnet3_refresh_rxfilter(dp);
	}
	if (!add && dp->mfTable.bufLen == 6) {
		ASSERT(newMfTable.buf == NULL);
		ASSERT(dp->rxMode & VMXNET3_RXM_MCAST);
		dp->rxMode &= ~VMXNET3_RXM_MCAST;
		vmxnet3_refresh_rxfilter(dp);
	}

	/*
	 * Now replace the old MF table with the new one
	 */
	if (dp->mfTable.buf) {
		vmxnet3_free_dma_mem(&dp->mfTable);
	}
	dp->mfTable = newMfTable;
	VMXNET3_DS(dp)->devRead.rxFilterConf.mfTablePA = newMfTable.bufPA;
	VMXNET3_DS(dp)->devRead.rxFilterConf.mfTableLen = newMfTable.bufLen;

done:
	/* Always update the filters */
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD, VMXNET3_CMD_UPDATE_MAC_FILTERS);

	return (ret);
}

/*
 * vmxnet3_unicst --
 *
 *    Set the mac address of a vmxnet3 device.
 *
 * Results:
 *    DDI_FAILURE.
 *
 * Side effects:
 *    None.
 */
static int
vmxnet3_unicst(void *data, const uint8_t *macaddr)
{
	vmxnet3_softc_t *dp = data;
	uint32_t val32;

	vmxnet3_debug(dp, 2, "unicst("MACADDR_FMT")\n",
	    MACADDR_FMT_ARGS(macaddr));

	val32 = *((uint32_t *)(macaddr + 0));
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_MACL, val32);
	val32 = *((uint16_t *)(macaddr + 4));
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_MACH, val32);

	(void) memcpy(dp->macaddr, macaddr, 6);

	return (DDI_SUCCESS);
}


/*
 * vmxnet3_change_mtu --
 *
 *    Change the MTU as seen by the driver. Reset the device and tx/rx queues
 *    so that buffers of right size are posted in rx queues.
 *
 * Results:
 *    EINVAL for invalid MTUs or other failures. 0 for success.
 *
 * Side effects:
 *    None.
 */

static int
vmxnet3_change_mtu(vmxnet3_softc_t *dp, uint32_t new_mtu)
{
	int ret = 0, do_reset = 0;
	ASSERT(dp);
	if (new_mtu == dp->cur_mtu) {
		return (0);
	}

	if (new_mtu < VMXNET3_MIN_MTU || new_mtu > VMXNET3_MAX_MTU) {
		vmxnet3_warn(dp, "New MTU not in valid range [%d, %d].\n",
		    VMXNET3_MIN_MTU, VMXNET3_MAX_MTU);
		return (EINVAL);
	}

	if (dp->devEnabled) {
		do_reset = 1;
		vmxnet3_stop(dp);
		VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD, VMXNET3_CMD_RESET_DEV);
	}

	dp->cur_mtu = new_mtu;

	if (do_reset)
		ret = vmxnet3_start(dp);

	return (ret);
}

static int
vmxnet3_m_getprop(void *arg, const char *name, mac_prop_id_t num, uint_t sz,
    void *val)
{
	int rc = 0;

	_NOTE(ARGUNUSED(arg));
	_NOTE(ARGUNUSED(name));
	_NOTE(ARGUNUSED(sz));

	switch (num) {
	case MAC_PROP_DUPLEX:
		*(link_duplex_t *)val = LINK_DUPLEX_FULL;
		break;
	case MAC_PROP_SPEED:
		*(uint64_t *)val = 10000000000; /* 10 Gb/sec */
		break;
	case MAC_PROP_STATUS:
		*(link_state_t *)val = LINK_STATE_UP;
		break;
	default:
		rc = ENOTSUP;
		break;
	}

	return (rc);
}

static void
vmxnet3_m_propinfo(void *arg, const char *name, mac_prop_id_t num,
    mac_prop_info_handle_t prh)
{
	_NOTE(ARGUNUSED(arg));
	_NOTE(ARGUNUSED(name));

	switch (num) {
	case MAC_PROP_DUPLEX:
	case MAC_PROP_SPEED:
	case MAC_PROP_STATUS:
		mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
		break;
	case MAC_PROP_MTU:
		mac_prop_info_set_range_uint32(prh, VMXNET3_MIN_MTU,
		    VMXNET3_MAX_MTU);
		break;
	default:
		break;
	}
}

static int
vmxnet3_m_setprop(void *arg, const char *name, mac_prop_id_t num, uint_t sz,
    const void *val)
{
	int rc = 0;
	vmxnet3_softc_t *dp = arg;
	uint32_t new_mtu;

	_NOTE(ARGUNUSED(sz));
	_NOTE(ARGUNUSED(name));

	switch (num) {
	case MAC_PROP_MTU:
		bcopy(val, &new_mtu, sizeof (new_mtu));
		if (new_mtu != dp->cur_mtu) {
			vmxnet3_debug(dp, 2, "Changing MTU from %d to %d\n",
			    dp->cur_mtu, new_mtu);
			rc = vmxnet3_change_mtu(dp, new_mtu);
		}
		break;
	default:
		rc = ENOTSUP;
		break;
	}

	return (rc);
}

/*
 * vmxnet3_getcapab --
 *
 *    Get the capabilities of a vmxnet3 device.
 *
 * Results:
 *    B_TRUE or B_FALSE.
 *
 * Side effects:
 *    None.
 */
static boolean_t
vmxnet3_getcapab(void *data, mac_capab_t capab, void *arg)
{
	vmxnet3_softc_t *dp = data;
	boolean_t ret;

	switch (capab) {
	case MAC_CAPAB_HCKSUM: {
		uint32_t *txflags = arg;
		*txflags = HCKSUM_INET_PARTIAL;
		ret = B_TRUE;
		break;
	}
	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *lso = arg;
		lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
		lso->lso_basic_tcp_ipv4.lso_max = IP_MAXPACKET;
		ret = vmxnet3_getprop(dp, "EnableLSO", 0, 1, 1);
		break;
	}
	default:
		ret = B_FALSE;
	}

	vmxnet3_debug(dp, 2, "getcapab(0x%x) -> %s\n",
	    capab, ret ? "yes" : "no");

	return (ret);
}

/*
 * vmxnet3_reset --
 *
 *    Reset a vmxnet3 device. Only to be used when the device is wedged.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The device is reset.
 */
static void
vmxnet3_reset(void *data)
{
	vmxnet3_softc_t *dp = data;

	vmxnet3_debug(dp, 1, "vmxnet3_reset()\n");

	vmxnet3_stop(dp);
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD, VMXNET3_CMD_RESET_DEV);
	(void) vmxnet3_start(dp);
}

/*
 * vmxnet3_intr_events --
 *
 *    Process pending events on a vmxnet3 device.
 *
 * Results:
 *    B_TRUE if the link state changed, B_FALSE otherwise.
 *
 * Side effects:
 *    None.
 */
static boolean_t
vmxnet3_intr_events(vmxnet3_softc_t *dp)
{
	Vmxnet3_DriverShared *ds = VMXNET3_DS(dp);
	boolean_t linkStateChanged = B_FALSE;
	uint32_t events = ds->ecr;

	if (events) {
		vmxnet3_debug(dp, 2, "events(0x%x)\n", events);
		if (events & (VMXNET3_ECR_RQERR | VMXNET3_ECR_TQERR)) {
			Vmxnet3_TxQueueDesc *tqdesc = VMXNET3_TQDESC(dp);
			Vmxnet3_RxQueueDesc *rqdesc = VMXNET3_RQDESC(dp);

			VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD,
			    VMXNET3_CMD_GET_QUEUE_STATUS);
			if (tqdesc->status.stopped) {
				vmxnet3_warn(dp, "tq error 0x%x\n",
				    tqdesc->status.error);
			}
			if (rqdesc->status.stopped) {
				vmxnet3_warn(dp, "rq error 0x%x\n",
				    rqdesc->status.error);
			}

			if (ddi_taskq_dispatch(dp->resetTask, vmxnet3_reset,
			    dp, DDI_NOSLEEP) == DDI_SUCCESS) {
				vmxnet3_warn(dp, "reset scheduled\n");
			} else {
				vmxnet3_warn(dp,
				    "ddi_taskq_dispatch() failed()\n");
			}
		}
		if (events & VMXNET3_ECR_LINK) {
			vmxnet3_refresh_linkstate(dp);
			linkStateChanged = B_TRUE;
		}
		if (events & VMXNET3_ECR_DIC) {
			vmxnet3_debug(dp, 1, "device implementation change\n");
		}
		VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_ECR, events);
	}

	return (linkStateChanged);
}

/*
 * vmxnet3_intr --
 *
 *    Interrupt handler of a vmxnet3 device.
 *
 * Results:
 *    DDI_INTR_CLAIMED or DDI_INTR_UNCLAIMED.
 *
 * Side effects:
 *    None.
 */
static uint_t
vmxnet3_intr(caddr_t data1, caddr_t data2)
{
	vmxnet3_softc_t *dp = (void *) data1;

	_NOTE(ARGUNUSED(data2));

	vmxnet3_debug(dp, 3, "intr()\n");

	mutex_enter(&dp->intrLock);

	if (dp->devEnabled) {
		boolean_t linkStateChanged;
		boolean_t mustUpdateTx;
		mblk_t *mps;

		if (dp->intrType == DDI_INTR_TYPE_FIXED &&
		    !VMXNET3_BAR1_GET32(dp, VMXNET3_REG_ICR)) {
			goto intr_unclaimed;
		}

		if (dp->intrMaskMode == VMXNET3_IMM_ACTIVE) {
			VMXNET3_BAR0_PUT32(dp, VMXNET3_REG_IMR, 1);
		}

		linkStateChanged = vmxnet3_intr_events(dp);
		mustUpdateTx = vmxnet3_tx_complete(dp, &dp->txQueue);
		mps = vmxnet3_rx_intr(dp, &dp->rxQueue);

		mutex_exit(&dp->intrLock);
		VMXNET3_BAR0_PUT32(dp, VMXNET3_REG_IMR, 0);

		if (linkStateChanged) {
			mac_link_update(dp->mac, dp->linkState);
		}
		if (mustUpdateTx) {
			mac_tx_update(dp->mac);
		}
		if (mps) {
			mac_rx(dp->mac, NULL, mps);
		}

		return (DDI_INTR_CLAIMED);
	}

intr_unclaimed:
	mutex_exit(&dp->intrLock);
	return (DDI_INTR_UNCLAIMED);
}


/*
 * vmxnet3_attach --
 *
 *    Probe and attach a vmxnet3 instance to the stack.
 *
 * Results:
 *    DDI_SUCCESS or DDI_FAILURE.
 *
 * Side effects:
 *    None.
 */
static int
vmxnet3_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	vmxnet3_softc_t *dp;
	mac_register_t *macr;
	uint16_t vendorId, devId, ret16;
	uint32_t ret32;
	int ret, err;
	uint_t uret;

	if (cmd != DDI_ATTACH) {
		goto error;
	}

	/*
	 * Allocate the soft state
	 */
	dp = kmem_zalloc(sizeof (vmxnet3_softc_t), KM_SLEEP);
	ASSERT(dp);

	dp->dip = dip;
	dp->instance = ddi_get_instance(dip);
	dp->cur_mtu = VMXNET3_MAX_MTU;

	vmxnet3_debug(dp, 1, "attach()\n");

	ddi_set_driver_private(dip, dp);

	/*
	 * Get access to the PCI bus configuration space
	 */
	if (pci_config_setup(dip, &dp->pciHandle) != DDI_SUCCESS) {
		vmxnet3_warn(dp, "pci_config_setup() failed\n");
		goto error_soft_state;
	}

	/*
	 * Make sure the chip is a vmxnet3 device
	 */
	vendorId = pci_config_get16(dp->pciHandle, PCI_CONF_VENID);
	devId = pci_config_get16(dp->pciHandle, PCI_CONF_DEVID);
	if (vendorId != PCI_VENDOR_ID_VMWARE ||
	    devId != PCI_DEVICE_ID_VMWARE_VMXNET3) {
		vmxnet3_warn(dp, "wrong PCI venid/devid (0x%x, 0x%x)\n",
		    vendorId, devId);
		goto error_pci_config;
	}

	/*
	 * Make sure we can access the registers through the I/O space
	 */
	ret16 = pci_config_get16(dp->pciHandle, PCI_CONF_COMM);
	ret16 |= PCI_COMM_IO | PCI_COMM_ME;
	pci_config_put16(dp->pciHandle, PCI_CONF_COMM, ret16);

	/*
	 * Map the I/O space in memory
	 */
	if (ddi_regs_map_setup(dip, 1, &dp->bar0, 0, 0, &vmxnet3_dev_attr,
	    &dp->bar0Handle) != DDI_SUCCESS) {
		vmxnet3_warn(dp, "ddi_regs_map_setup() for BAR0 failed\n");
		goto error_pci_config;
	}

	if (ddi_regs_map_setup(dip, 2, &dp->bar1, 0, 0, &vmxnet3_dev_attr,
	    &dp->bar1Handle) != DDI_SUCCESS) {
		vmxnet3_warn(dp, "ddi_regs_map_setup() for BAR1 failed\n");
		goto error_regs_map_0;
	}

	/*
	 * Check the version number of the virtual device
	 */
	if (VMXNET3_BAR1_GET32(dp, VMXNET3_REG_VRRS) & 1) {
		VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_VRRS, 1);
	} else {
		vmxnet3_warn(dp, "incompatible h/w version\n");
		goto error_regs_map_1;
	}

	if (VMXNET3_BAR1_GET32(dp, VMXNET3_REG_UVRS) & 1) {
		VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_UVRS, 1);
	} else {
		vmxnet3_warn(dp, "incompatible upt version\n");
		goto error_regs_map_1;
	}

	/*
	 * Read the MAC address from the device
	 */
	ret32 = VMXNET3_BAR1_GET32(dp, VMXNET3_REG_MACL);
	*((uint32_t *)(dp->macaddr + 0)) = ret32;
	ret32 = VMXNET3_BAR1_GET32(dp, VMXNET3_REG_MACH);
	*((uint16_t *)(dp->macaddr + 4)) = ret32;

	/*
	 * Register with the MAC framework
	 */
	if (!(macr = mac_alloc(MAC_VERSION))) {
		vmxnet3_warn(dp, "mac_alloc() failed.\n");
		goto error_regs_map_1;
	}

	macr->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	macr->m_driver = dp;
	macr->m_dip = dip;
	macr->m_instance = 0;
	macr->m_src_addr = dp->macaddr;
	macr->m_dst_addr = NULL;
	macr->m_callbacks = &vmxnet3_mac_callbacks;
	macr->m_min_sdu = VMXNET3_MIN_MTU;
	macr->m_max_sdu = VMXNET3_MAX_MTU;
	macr->m_pdata = NULL;
	macr->m_pdata_size = 0;

	ret = mac_register(macr, &dp->mac);
	mac_free(macr);
	if (ret != DDI_SUCCESS) {
		vmxnet3_warn(dp, "mac_register() failed\n");
		goto error_regs_map_1;
	}

	/*
	 * Register the interrupt(s) in this order of preference:
	 * MSI-X, MSI, INTx
	 */
	VMXNET3_BAR1_PUT32(dp, VMXNET3_REG_CMD, VMXNET3_CMD_GET_CONF_INTR);
	ret32 = VMXNET3_BAR1_GET32(dp, VMXNET3_REG_CMD);
	switch (ret32 & 0x3) {
	case VMXNET3_IT_AUTO:
	case VMXNET3_IT_MSIX:
		dp->intrType = DDI_INTR_TYPE_MSIX;
		err = ddi_intr_alloc(dip, &dp->intrHandle, dp->intrType, 0, 1,
		    &ret, DDI_INTR_ALLOC_STRICT);
		if (err == DDI_SUCCESS)
			break;
		vmxnet3_debug(dp, 2, "DDI_INTR_TYPE_MSIX failed, err:%d\n",
		    err);
		/* FALLTHROUGH */
	case VMXNET3_IT_MSI:
		dp->intrType = DDI_INTR_TYPE_MSI;
		if (ddi_intr_alloc(dip, &dp->intrHandle, dp->intrType, 0, 1,
		    &ret, DDI_INTR_ALLOC_STRICT) == DDI_SUCCESS)
		break;
		vmxnet3_debug(dp, 2, "DDI_INTR_TYPE_MSI failed\n");
		/* FALLTHROUGH */
	case VMXNET3_IT_INTX:
		dp->intrType = DDI_INTR_TYPE_FIXED;
		if (ddi_intr_alloc(dip, &dp->intrHandle, dp->intrType, 0, 1,
		    &ret, DDI_INTR_ALLOC_STRICT) == DDI_SUCCESS) {
			break;
		}
		vmxnet3_debug(dp, 2, "DDI_INTR_TYPE_INTX failed\n");
		/* FALLTHROUGH */
	default:
		vmxnet3_warn(dp, "ddi_intr_alloc() failed\n");
		goto error_mac;
	}
	dp->intrMaskMode = (ret32 >> 2) & 0x3;
	if (dp->intrMaskMode == VMXNET3_IMM_LAZY) {
		vmxnet3_warn(dp, "Lazy masking is not supported\n");
		goto error_intr;
	}

	if (ddi_intr_get_pri(dp->intrHandle, &uret) != DDI_SUCCESS) {
		vmxnet3_warn(dp, "ddi_intr_get_pri() failed\n");
		goto error_intr;
	}

	vmxnet3_debug(dp, 2, "intrType=0x%x, intrMaskMode=0x%x, intrPrio=%u\n",
	    dp->intrType, dp->intrMaskMode, uret);

	/*
	 * Create a task queue to reset the device if it wedges.
	 */
	dp->resetTask = ddi_taskq_create(dip, "vmxnet3_reset_task", 1,
	    TASKQ_DEFAULTPRI, 0);
	if (!dp->resetTask) {
		vmxnet3_warn(dp, "ddi_taskq_create() failed()\n");
		goto error_intr;
	}

	/*
	 * Initialize our mutexes now that we know the interrupt priority
	 * This _must_ be done before ddi_intr_enable()
	 */
	mutex_init(&dp->intrLock, NULL, MUTEX_DRIVER, DDI_INTR_PRI(uret));
	mutex_init(&dp->txLock, NULL, MUTEX_DRIVER, DDI_INTR_PRI(uret));
	mutex_init(&dp->rxPoolLock, NULL, MUTEX_DRIVER, DDI_INTR_PRI(uret));

	if (ddi_intr_add_handler(dp->intrHandle, vmxnet3_intr,
	    dp, NULL) != DDI_SUCCESS) {
		vmxnet3_warn(dp, "ddi_intr_add_handler() failed\n");
		goto error_mutexes;
	}

	err = ddi_intr_get_cap(dp->intrHandle, &dp->intrCap);
	if (err != DDI_SUCCESS) {
		vmxnet3_warn(dp, "ddi_intr_get_cap() failed %d", err);
		goto error_intr_handler;
	}

	if (dp->intrCap & DDI_INTR_FLAG_BLOCK) {
		err = ddi_intr_block_enable(&dp->intrHandle, 1);
		if (err != DDI_SUCCESS) {
			vmxnet3_warn(dp, "ddi_intr_block_enable() failed :%d\n",
			    err);
			goto error_intr_handler;
		}
	} else {
		err = ddi_intr_enable(dp->intrHandle);
		if ((err != DDI_SUCCESS)) {
			vmxnet3_warn(dp, "ddi_intr_enable() failed, err:%d\n",
			    err);
			goto error_intr_handler;
		}
	}

	return (DDI_SUCCESS);

error_intr_handler:
	(void) ddi_intr_remove_handler(dp->intrHandle);
error_mutexes:
	mutex_destroy(&dp->rxPoolLock);
	mutex_destroy(&dp->txLock);
	mutex_destroy(&dp->intrLock);
	ddi_taskq_destroy(dp->resetTask);
error_intr:
	(void) ddi_intr_free(dp->intrHandle);
error_mac:
	(void) mac_unregister(dp->mac);
error_regs_map_1:
	ddi_regs_map_free(&dp->bar1Handle);
error_regs_map_0:
	ddi_regs_map_free(&dp->bar0Handle);
error_pci_config:
	pci_config_teardown(&dp->pciHandle);
error_soft_state:
	kmem_free(dp, sizeof (vmxnet3_softc_t));
error:
	return (DDI_FAILURE);
}

/*
 * vmxnet3_detach --
 *
 *    Detach a vmxnet3 instance from the stack.
 *
 * Results:
 *    DDI_SUCCESS or DDI_FAILURE.
 *
 * Side effects:
 *    None.
 */
static int
vmxnet3_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	vmxnet3_softc_t *dp = ddi_get_driver_private(dip);
	unsigned int retries = 0;

	vmxnet3_debug(dp, 1, "detach()\n");

	if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	while (dp->rxNumBufs) {
		if (retries++ < 10) {
			vmxnet3_warn(dp, "rx pending (%u), waiting 1 sec ...\n",
			    dp->rxNumBufs);
			delay(drv_usectohz(1000000));
		} else {
			vmxnet3_warn(dp, "giving up...\n");
			return (DDI_FAILURE);
		}
	}

	if (dp->intrCap & DDI_INTR_FLAG_BLOCK) {
		(void) ddi_intr_block_disable(&dp->intrHandle, 1);
	} else {
		(void) ddi_intr_disable(dp->intrHandle);
	}
	(void) ddi_intr_remove_handler(dp->intrHandle);
	(void) ddi_intr_free(dp->intrHandle);

	(void) mac_unregister(dp->mac);

	if (dp->mfTable.buf) {
		vmxnet3_free_dma_mem(&dp->mfTable);
	}

	mutex_destroy(&dp->rxPoolLock);
	mutex_destroy(&dp->txLock);
	mutex_destroy(&dp->intrLock);
	ddi_taskq_destroy(dp->resetTask);

	ddi_regs_map_free(&dp->bar1Handle);
	ddi_regs_map_free(&dp->bar0Handle);
	pci_config_teardown(&dp->pciHandle);

	kmem_free(dp, sizeof (vmxnet3_softc_t));

	return (DDI_SUCCESS);
}

/* --- */

/*
 * Structures used by the Solaris module loader
 */

#define	VMXNET3_IDENT "VMware EtherAdapter v3 b" BUILD_NUMBER_NUMERIC_STRING

COMPAT_DDI_DEFINE_STREAM_OPS(vmxnet3_dev_ops,
			nulldev,
			nulldev,
			vmxnet3_attach,
			vmxnet3_detach,
			nodev,
			NULL,
			D_NEW | D_MP,
			NULL);

static struct modldrv vmxnet3_modldrv = {
	&mod_driverops, /* drv_modops */
	VMXNET3_IDENT, /* drv_linkinfo */
	&vmxnet3_dev_ops /* drv_dev_ops */
};

static struct modlinkage vmxnet3_modlinkage = {
	MODREV_1, /* ml_rev */
	{ &vmxnet3_modldrv, NULL } /* ml_linkage */
};

/* Module load entry point */
int
_init(void)
{
	int ret;

#ifdef DEBUG
	cmn_err(CE_CONT, "_init()\n");
#endif

	mac_init_ops(&vmxnet3_dev_ops, VMXNET3_MODNAME);
	ret = mod_install(&vmxnet3_modlinkage);
	if (ret != DDI_SUCCESS) {
		mac_fini_ops(&vmxnet3_dev_ops);
	}

	return (ret);
}

/* Module unload entry point */
int
_fini(void)
{
	int ret;

#ifdef DEBUG
	cmn_err(CE_CONT, "_fini()\n");
#endif

	ret = mod_remove(&vmxnet3_modlinkage);
	if (ret == DDI_SUCCESS) {
		mac_fini_ops(&vmxnet3_dev_ops);
	}

	return (ret);
}

/* Module info entry point */
int
_info(struct modinfo *modinfop)
{
#ifdef DEBUG
	cmn_err(CE_CONT, "_info()\n");
#endif

	return (mod_info(&vmxnet3_modlinkage, modinfop));
}
