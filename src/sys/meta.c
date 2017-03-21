/**
 * @file sys/meta.c
 *
 * @copyright 2015-2017 Bill Zissimopoulos
 */
/*
 * This file is part of WinFsp.
 *
 * You can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 3 as published by the Free Software
 * Foundation.
 *
 * Licensees holding a valid commercial license may use this file in
 * accordance with the commercial license agreement provided with the
 * software.
 */

#include <sys/driver.h>

typedef struct _FSP_META_CACHE_ITEM
{
    LIST_ENTRY ListEntry;
    struct _FSP_META_CACHE_ITEM *DictNext;
    PVOID ItemBuffer;
    UINT64 ItemIndex;
    UINT64 ExpirationTime;
    LONG RefCount;
} FSP_META_CACHE_ITEM;

typedef struct
{
    PVOID Item;
    ULONG Size;
    __declspec(align(MEMORY_ALLOCATION_ALIGNMENT)) UINT8 Buffer[];
} FSP_META_CACHE_ITEM_BUFFER;

static inline VOID FspMetaCacheDereferenceItem(FSP_META_CACHE_ITEM *Item)
{
    LONG RefCount = InterlockedDecrement(&Item->RefCount);
    if (0 == RefCount)
    {
        /* if we ever need to add a finalizer for meta items it should go here */
        FspFree(Item->ItemBuffer);
        FspFree(Item);
    }
}

static inline FSP_META_CACHE_ITEM *FspMetaCacheLookupIndexedItemAtDpcLevel(FSP_META_CACHE *MetaCache,
    UINT64 ItemIndex)
{
    FSP_META_CACHE_ITEM *Item = 0;
    ULONG HashIndex = ItemIndex % MetaCache->ItemBucketCount;
    for (FSP_META_CACHE_ITEM *ItemX = MetaCache->ItemBuckets[HashIndex]; ItemX; ItemX = ItemX->DictNext)
        if (ItemX->ItemIndex == ItemIndex)
        {
            Item = ItemX;
            break;
        }
    return Item;
}

static inline VOID FspMetaCacheAddItemAtDpcLevel(FSP_META_CACHE *MetaCache, FSP_META_CACHE_ITEM *Item)
{
    ULONG HashIndex = Item->ItemIndex % MetaCache->ItemBucketCount;
#if DBG
    for (FSP_META_CACHE_ITEM *ItemX = MetaCache->ItemBuckets[HashIndex]; ItemX; ItemX = ItemX->DictNext)
        ASSERT(ItemX->ItemIndex != Item->ItemIndex);
#endif
    Item->DictNext = MetaCache->ItemBuckets[HashIndex];
    MetaCache->ItemBuckets[HashIndex] = Item;
    InsertTailList(&MetaCache->ItemList, &Item->ListEntry);
    MetaCache->ItemCount++;
}

static inline FSP_META_CACHE_ITEM *FspMetaCacheRemoveIndexedItemAtDpcLevel(FSP_META_CACHE *MetaCache,
    UINT64 ItemIndex)
{
    FSP_META_CACHE_ITEM *Item = 0;
    ULONG HashIndex = ItemIndex % MetaCache->ItemBucketCount;
    for (FSP_META_CACHE_ITEM **P = (PVOID)&MetaCache->ItemBuckets[HashIndex]; *P; P = &(*P)->DictNext)
        if ((*P)->ItemIndex == ItemIndex)
        {
            Item = *P;
            *P = (*P)->DictNext;
            RemoveEntryList(&Item->ListEntry);
            MetaCache->ItemCount--;
            break;
        }
    return Item;
}

static inline FSP_META_CACHE_ITEM *FspMetaCacheRemoveExpiredItemAtDpcLevel(FSP_META_CACHE *MetaCache,
    UINT64 ExpirationTime)
{
    PLIST_ENTRY Head = &MetaCache->ItemList;
    PLIST_ENTRY Entry = Head->Flink;
    if (Head == Entry)
        return 0;
    FSP_META_CACHE_ITEM *Item = CONTAINING_RECORD(Entry, FSP_META_CACHE_ITEM, ListEntry);
    if (!FspExpirationTimeValid2(Item->ExpirationTime, ExpirationTime))
        return 0;
    ULONG HashIndex = Item->ItemIndex % MetaCache->ItemBucketCount;
    for (FSP_META_CACHE_ITEM **P = (PVOID)&MetaCache->ItemBuckets[HashIndex]; *P; P = &(*P)->DictNext)
        if (*P == Item)
        {
            *P = (*P)->DictNext;
            break;
        }
    RemoveEntryList(&Item->ListEntry);
    MetaCache->ItemCount--;
    return Item;
}

NTSTATUS FspMetaCacheCreate(
    ULONG MetaCapacity, ULONG ItemSizeMax, PLARGE_INTEGER MetaTimeout,
    FSP_META_CACHE **PMetaCache)
{
    *PMetaCache = 0;
    if (0 == MetaCapacity || 0 == ItemSizeMax || 0 == MetaTimeout->QuadPart)
        return STATUS_SUCCESS;
    FSP_META_CACHE *MetaCache;
    ULONG BucketCount = (PAGE_SIZE - sizeof *MetaCache) / sizeof MetaCache->ItemBuckets[0];
    MetaCache = FspAllocNonPaged(PAGE_SIZE);
    if (0 == MetaCache)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(MetaCache, PAGE_SIZE);
    KeInitializeSpinLock(&MetaCache->SpinLock);
    InitializeListHead(&MetaCache->ItemList);
    MetaCache->MetaCapacity = MetaCapacity;
    MetaCache->ItemSizeMax = ItemSizeMax;
    MetaCache->MetaTimeout = MetaTimeout->QuadPart;
    MetaCache->ItemBucketCount = BucketCount;
    *PMetaCache = MetaCache;
    return STATUS_SUCCESS;
}

VOID FspMetaCacheDelete(FSP_META_CACHE *MetaCache)
{
    if (0 == MetaCache)
        return;
    FspMetaCacheInvalidateExpired(MetaCache, (UINT64)-1LL);
    FspFree(MetaCache);
}

VOID FspMetaCacheInvalidateExpired(FSP_META_CACHE *MetaCache, UINT64 ExpirationTime)
{
    if (0 == MetaCache)
        return;
    FSP_META_CACHE_ITEM *Item;
    KIRQL Irql;
    for (;;)
    {
        KeAcquireSpinLock(&MetaCache->SpinLock, &Irql);
        Item = FspMetaCacheRemoveExpiredItemAtDpcLevel(MetaCache, ExpirationTime);
        KeReleaseSpinLock(&MetaCache->SpinLock, Irql);
        if (0 == Item)
            break;
        FspMetaCacheDereferenceItem(Item);
    }
}

BOOLEAN FspMetaCacheReferenceItemBuffer(FSP_META_CACHE *MetaCache, UINT64 ItemIndex,
    PCVOID *PBuffer, PULONG PSize)
{
    *PBuffer = 0;
    if (0 != PSize)
        *PSize = 0;
    if (0 == MetaCache || 0 == ItemIndex)
        return FALSE;
    FSP_META_CACHE_ITEM *Item = 0;
    FSP_META_CACHE_ITEM_BUFFER *ItemBuffer;
    KIRQL Irql;
    KeAcquireSpinLock(&MetaCache->SpinLock, &Irql);
    Item = FspMetaCacheLookupIndexedItemAtDpcLevel(MetaCache, ItemIndex);
    if (0 == Item)
    {
        KeReleaseSpinLock(&MetaCache->SpinLock, Irql);
        return FALSE;
    }
    InterlockedIncrement(&Item->RefCount);
    KeReleaseSpinLock(&MetaCache->SpinLock, Irql);
    ItemBuffer = Item->ItemBuffer;
    *PBuffer = ItemBuffer->Buffer;
    if (0 != PSize)
        *PSize = ItemBuffer->Size;
    return TRUE;
}

VOID FspMetaCacheDereferenceItemBuffer(PCVOID Buffer)
{
    FSP_META_CACHE_ITEM_BUFFER *ItemBuffer = (PVOID)((PUINT8)Buffer - sizeof *ItemBuffer);
    FspMetaCacheDereferenceItem(ItemBuffer->Item);
}

UINT64 FspMetaCacheAddItem(FSP_META_CACHE *MetaCache, PCVOID Buffer, ULONG Size)
{
    if (0 == MetaCache)
        return 0;
    FSP_META_CACHE_ITEM *Item;
    FSP_META_CACHE_ITEM_BUFFER *ItemBuffer;
    UINT64 ItemIndex = 0;
    KIRQL Irql;
    if (sizeof *ItemBuffer + Size > MetaCache->ItemSizeMax)
        return 0;
    Item = FspAllocNonPaged(sizeof *Item);
    if (0 == Item)
        return 0;
    ItemBuffer = FspAlloc(sizeof *ItemBuffer + Size);
    if (0 == ItemBuffer)
    {
        FspFree(Item);
        return 0;
    }
    RtlZeroMemory(Item, sizeof *Item);
    RtlZeroMemory(ItemBuffer, sizeof *ItemBuffer);
    Item->ItemBuffer = ItemBuffer;
    Item->ExpirationTime = FspExpirationTimeFromTimeout(MetaCache->MetaTimeout);
    Item->RefCount = 1;
    ItemBuffer->Item = Item;
    ItemBuffer->Size = Size;
    RtlCopyMemory(ItemBuffer->Buffer, Buffer, Size);
    KeAcquireSpinLock(&MetaCache->SpinLock, &Irql);
    if (MetaCache->ItemCount >= MetaCache->MetaCapacity)
        FspMetaCacheRemoveExpiredItemAtDpcLevel(MetaCache, (UINT64)-1LL);
    ItemIndex = MetaCache->ItemIndex;
    ItemIndex = (UINT64)-1LL == ItemIndex ? 1 : ItemIndex + 1;
    MetaCache->ItemIndex = Item->ItemIndex = ItemIndex;
    FspMetaCacheAddItemAtDpcLevel(MetaCache, Item);
    KeReleaseSpinLock(&MetaCache->SpinLock, Irql);
    return ItemIndex;
}

VOID FspMetaCacheInvalidateItem(FSP_META_CACHE *MetaCache, UINT64 ItemIndex)
{
    if (0 == MetaCache || 0 == ItemIndex)
        return;
    FSP_META_CACHE_ITEM *Item;
    KIRQL Irql;
    KeAcquireSpinLock(&MetaCache->SpinLock, &Irql);
    Item = FspMetaCacheRemoveIndexedItemAtDpcLevel(MetaCache, ItemIndex);
    KeReleaseSpinLock(&MetaCache->SpinLock, Irql);
    if (0 != Item)
        FspMetaCacheDereferenceItem(Item);
}
