#include <bd_cache.h>
#include <errno.h>
#include <string.h>
#include <sysmem.h>

//#define DEBUG  //comment out this line when not debugging
#include "module_debug.h"

#define SECTORS_PER_BLOCK    32 // 每块 32 扇区，共 16KiB
#define BLOCK_COUNT          16 // 共 16 块，总量 256KiB
#define BLOCK_WEIGHT_FACTOR 256 // Fixed point math (24.8)

struct bd_cache
{
    struct block_device *bd;
    int weight[BLOCK_COUNT];
    u64 sector[BLOCK_COUNT];
    u8 cache[BLOCK_COUNT][SECTORS_PER_BLOCK*512];
#ifdef DEBUG
    u32 sectors_read;
    u32 sectors_cache;
    u32 sectors_dev;
#endif
};

/* cache overlaps with requested area ? */
static int _overlaps(u64 csector, u64 sector, u16 count)
{
    if ((sector < (csector + SECTORS_PER_BLOCK)) && ((sector + count) > csector))
        return 1;
    else
        return 0;
}

/* cache contains requested area ? */
static int _contains(u64 csector, u64 sector, u16 count)
{
    if ((sector >= csector) && ((sector + count) <= (csector + SECTORS_PER_BLOCK)))
        return 1;
    else
        return 0;
}

static void _invalidate(struct bd_cache *c, u64 sector, u16 count)
{
    int blkidx;

    for (blkidx = 0; blkidx < BLOCK_COUNT; blkidx++) {
        if (_overlaps(c->sector[blkidx], sector, count)) {
            // Invalidate cache entry
            c->sector[blkidx] = 0xffffffffffffffff;
        }
    }
}

static int _read(struct block_device *bd, u64 sector, void *buffer, u16 count)
{
    struct bd_cache *c = bd->priv;
    u16 sectors_to_cache;
    int read_result;

    //M_DEBUG("%s(%d, %d)\n", __FUNCTION__, sector, count);

    if (count == 0)
        return 0;

    if (sector >= bd->sectorCount || count > bd->sectorCount - sector)
        return -EIO;

    /* 缓存按 512 字节扇区分配。若逻辑扇区大小不是 512 字节，则绕过缓存，
       避免缓存块发生越界。 */
    if (bd->sectorSize != 512 || count >= SECTORS_PER_BLOCK) {
        // 直接读取设备
        return c->bd->read(c->bd, sector, buffer, count);
    }

#ifdef DEBUG
    c->sectors_read += count;
#endif

    // Do a cached read
    int blkidx;
    for (blkidx = 0; blkidx < BLOCK_COUNT; blkidx++) {
        if (_contains(c->sector[blkidx], sector, count)) {
#ifdef DEBUG
            c->sectors_cache += count;
            //M_DEBUG("- CACHE HIT[%d] [block %d] [devread %ds, hit-ratio %d%%]\n", sector, blkidx, c->sectors_dev, (c->sectors_cache * 100) / c->sectors_read);
#endif
            // Minimum weight
            if (c->weight[blkidx] < 0)
                c->weight[blkidx] = 0;

            c->weight[blkidx] += count * BLOCK_WEIGHT_FACTOR;

            // Read from cache
            u64 offset = (sector - c->sector[blkidx]) * 512;
            memcpy(buffer, &c->cache[blkidx][offset], count * 512);
            return count;
        }
    }

    // Find block with the lowest weight
    int blkidx_best_weight = 0x7fffffff;
    int blkidx_best = 0;
    M_DEBUG("- list: ");
    for (blkidx = 0; blkidx < BLOCK_COUNT; blkidx++) {
#ifdef DEBUG
        printf("%*d ", 3, c->weight[blkidx] / BLOCK_WEIGHT_FACTOR);
#endif

        // Dynamic aging
        c->weight[blkidx] -= (SECTORS_PER_BLOCK * BLOCK_WEIGHT_FACTOR / BLOCK_COUNT) + (c->weight[blkidx] / 32);

        if (c->weight[blkidx] < blkidx_best_weight) {
            // Better block found
            blkidx_best_weight = c->weight[blkidx];
            blkidx_best = blkidx;
        }
    }
#ifdef DEBUG
    printf(" devread: %*d, evict %*d [%*d], add [%*d]\n", 4, c->sectors_dev, 2, blkidx_best, 8, c->sector[blkidx_best], 8, sector);
    c->sectors_dev += SECTORS_PER_BLOCK;
    //M_DEBUG("- CACHE READ[%d] -> [block %d] [devread %ds, hit-ratio %d%%]\n", sector, blkidx_best, c->sectors_dev, (c->sectors_cache * 100) / c->sectors_read);
#endif

    // 填充缓存块，预读范围不能超过设备末尾。
    sectors_to_cache = SECTORS_PER_BLOCK;
    if (sectors_to_cache > bd->sectorCount - sector)
        sectors_to_cache = (u16)(bd->sectorCount - sector);

    /* 发起 I/O 前先使缓存块失效。读取失败或短读时，不能让旧缓存数据在本次
       或后续调用中被误认为读取成功。 */
    c->sector[blkidx_best] = 0xffffffffffffffff;
    read_result = c->bd->read(c->bd, sector, c->cache[blkidx_best], sectors_to_cache);
    if (read_result != sectors_to_cache)
        return read_result < 0 ? read_result : -EIO;

    c->sector[blkidx_best] = sector;

    // Read from cache
    u64 offset = (sector - c->sector[blkidx_best]) * 512;
    c->weight[blkidx_best] = count * BLOCK_WEIGHT_FACTOR;
    memcpy(buffer, &c->cache[blkidx_best][offset], count * 512);
    return count;
}

static int _write(struct block_device *bd, u64 sector, const void *buffer, u16 count)
{
    struct bd_cache *c = bd->priv;

    M_DEBUG("%s(%d, %d)\n", __FUNCTION__, sector, count);

    _invalidate(c, sector, count);

    return c->bd->write(c->bd, sector, buffer, count);
}

static void _flush(struct block_device *bd)
{
    struct bd_cache *c = bd->priv;

    M_DEBUG("%s\n", __FUNCTION__);

    c->bd->flush(c->bd);
}

static int _stop(struct block_device *bd)
{
    struct bd_cache *c = bd->priv;

    M_DEBUG("%s\n", __FUNCTION__);

    return c->bd->stop(c->bd);
}

struct block_device *bd_cache_create(struct block_device *bd)
{
    int blkidx;

    // Create new block device
    struct block_device *cbd = AllocSysMemory(ALLOC_FIRST, sizeof(struct block_device), NULL);
    // Create new private data
    struct bd_cache *c = AllocSysMemory(ALLOC_FIRST, sizeof(struct bd_cache), NULL);

    M_DEBUG("%s\n", __FUNCTION__);

    if (bd == NULL || cbd == NULL || c == NULL) {
        if (c != NULL)
            FreeSysMemory(c);
        if (cbd != NULL)
            FreeSysMemory(cbd);
        return NULL;
    }

    c->bd = bd;
    for (blkidx = 0; blkidx < BLOCK_COUNT; blkidx++) {
        c->weight[blkidx] = 0;
        c->sector[blkidx] = 0xffffffffffffffff;
    }
#ifdef DEBUG
    c->sectors_read = 0;
    c->sectors_cache = 0;
    c->sectors_dev = 0;
#endif

    // copy all parameters becouse we are the same blocks device
    // only difference is we are cached.
    cbd->priv         = c;
    cbd->name         = bd->name;
    cbd->devNr        = bd->devNr;
    cbd->parNr        = bd->parNr;
    cbd->parId        = bd->parId;
    cbd->sectorSize   = bd->sectorSize;
    cbd->sectorOffset = bd->sectorOffset;
    cbd->sectorCount  = bd->sectorCount;

    cbd->read = _read;
    cbd->write = _write;
    cbd->flush = _flush;
    cbd->stop = _stop;

    return cbd;
}

void bd_cache_destroy(struct block_device *cbd)
{
    M_DEBUG("%s\n", __FUNCTION__);

    if (cbd == NULL)
        return;

    FreeSysMemory(cbd->priv);
    FreeSysMemory(cbd);
}
