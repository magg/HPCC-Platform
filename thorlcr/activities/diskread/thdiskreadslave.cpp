/*##############################################################################

    Copyright (C) 2011 HPCC Systems.

    All rights reserved. This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
############################################################################## */

#include "platform.h"

#include "jfile.hpp"
#include "jio.hpp"
#include "jlzw.hpp"
#include "jsocket.hpp"
#include "jsort.hpp"
#include "jtime.hpp"

#include "dafdesc.hpp"
#include "rtlkey.hpp"
#include "eclhelper.hpp" // tmp for IHThor..Arg interfaces.

#include "thormisc.hpp"
#include "thmfilemanager.hpp"
#include "thorport.hpp"
#include "thcrc.hpp"
#include "thsortu.hpp"
#include "thexception.hpp"
#include "thactivityutil.ipp"

#include "../hashdistrib/thhashdistribslave.ipp"
#include "thdiskreadslave.ipp"

#define ASYNC_BUFFER_SIZE   64 * 1024       // 64k

#define RECORD_BUFFER_SIZE  64 * 1024       // 64k

class CDiskReadSlaveActivityRecord;
class CDiskRecordPartHandler : public CDiskPartHandlerBase
{
    Owned<IExtRowStream> in;
protected:
    offset_t localRowOffset;
    CDiskReadSlaveActivityRecord &activity;
public:
    CDiskRecordPartHandler(CDiskReadSlaveActivityRecord &activity);
    ~CDiskRecordPartHandler();
    virtual void getMetaInfo(ThorDataLinkMetaInfo &info, IPartDescriptor *partDesc);
    virtual void open();
    virtual void close(CRC32 &fileCRC);
    offset_t getLocalOffset()
    {
        return localRowOffset;
    }
    inline const void *nextRow()
    {
        localRowOffset = in->getOffset();       // shame this needed as a bit inefficient
        const void *ret = in->nextRow();
        if (ret)
            ++progress;
        return ret;
    }
    inline const void *prefetchRow()
    {
        localRowOffset = in->getOffset();       // shame this needed as a bit inefficient
        const void *ret = in->prefetchRow();
        if (ret)
            ++progress;
        return ret;
    }
    inline void prefetchDone()
    {
        in->prefetchDone();
    }
};

//////////////////////////////////////////////

class CDiskReadSlaveActivityRecord : public CDiskReadSlaveActivityBase, implements IIndexReadContext
{
protected:
    IArrayOf<IKeySegmentMonitor> segMonitors;
    bool grouped;
    bool isFixedDiskWidth;
    size32_t diskRowMinSz;

    inline bool segMonitorsMatch(const void *buffer)
    {
        ForEachItemIn(idx, segMonitors)
        {
            if (!segMonitors.item(idx).matches(buffer))
                return false;
        }
        return true;
    }

public:
    CDiskReadSlaveActivityRecord(CGraphElementBase *_container, IHThorArg *_helper=NULL) 
        : CDiskReadSlaveActivityBase(_container)
    {
        if (_helper)
            baseHelper.set(_helper);
        helper = (IHThorDiskReadArg *)queryHelper();
        IOutputMetaData *diskRowMeta = queryDiskRowInterfaces()->queryRowMetaData()->querySerializedMeta();
        isFixedDiskWidth = diskRowMeta->isFixedSize();
        diskRowMinSz = diskRowMeta->getMinRecordSize();
        helper->createSegmentMonitors(this);
        grouped = false;
    }

// IIndexReadContext impl.
    void append(IKeySegmentMonitor *segment)
    {
        if (segment->isWild())
            segment->Release();
        else
            segMonitors.append(*segment);
    }

    unsigned ordinality() const
    {
        return segMonitors.length();
    }

    IKeySegmentMonitor *item(unsigned idx) const
    {
        if (segMonitors.isItem(idx))
            return &segMonitors.item(idx);
        else
            return NULL;
    }
    
    virtual void setMergeBarrier(unsigned barrierOffset)
    {
        // We don't merge them so no issue... afaik
    }

friend class CDiskRecordPartHandler;
};

/////////////////////////////////////////////////

CDiskRecordPartHandler::CDiskRecordPartHandler(CDiskReadSlaveActivityRecord &_activity) 
: CDiskPartHandlerBase(_activity), activity(_activity)
{
    localRowOffset = 0;
}

CDiskRecordPartHandler::~CDiskRecordPartHandler()
{
}



void CDiskRecordPartHandler::getMetaInfo(ThorDataLinkMetaInfo &info, IPartDescriptor *partDesc)
{
    if (!partDesc->queryProperties().hasProp("@size"))
    {
        info.totalRowsMin = 0;          
        info.totalRowsMax = -1;
        info.unknownRowsOutput = true;
        return;
    }
    offset_t filesize = partDesc->queryProperties().getPropInt64("@size");
    if (activity.isFixedDiskWidth) 
    {
        if (compressed && !blockCompressed)
        {
            info.totalRowsMin = filesize/(activity.diskRowMinSz*2);
            info.totalRowsMax = filesize * 2; // ~ extreme case
        }
        else
        {
            info.totalRowsMin = filesize/activity.diskRowMinSz;
            info.totalRowsMax = info.totalRowsMin;
        }
    }
    else
    {
        info.totalRowsMin = filesize?1:0;  
        info.totalRowsMax = activity.diskRowMinSz?(filesize/activity.diskRowMinSz):filesize;
    }
}

void CDiskRecordPartHandler::open()
{
    CDiskPartHandlerBase::open();
    in.clear();
    if (compressed)
    {
        in.setown(createCompressedRowStream(iFile, activity.queryDiskRowInterfaces(), 0, (offset_t)-1, RCUNBOUND, checkFileCrc, activity.grouped, activity.eexp));
        if (!in.get())
        {
            if (!blockCompressed)
                throw MakeStringException(-1,"Unsupported compressed file format: %s", filename.get());
            else 
                throw MakeActivityException(&activity, 0, "Failed to open block compressed file '%s'", filename.get());
        }
    }
    else 
        in.setown(createRowStream(iFile, activity.queryDiskRowInterfaces(), 0, (offset_t)-1, RCUNBOUND, checkFileCrc, activity.grouped));
    if (!in)
        throw MakeActivityException(&activity, 0, "Failed to open file '%s'", filename.get());
    ActPrintLog(&activity, "%s[part=%d]: %s (%s)", kindStr, which, activity.isFixedDiskWidth ? "fixed" : "variable", filename.get());
    if (activity.isFixedDiskWidth) 
    {
        if (!compressed || blockCompressed)
        {
            unsigned fixedSize = activity.diskRowMinSz;
            if (partDesc->queryProperties().hasProp("@size"))
            {
                offset_t lsize = partDesc->queryProperties().getPropInt64("@size");
                if (0 != lsize % fixedSize)
                    throw MakeActivityException(&activity, TE_BadFileLength, "Fixed length file %s [DFS size=%"I64F"d] is not a multiple of fixed record size : %d", filename.get(), lsize, fixedSize);
            }
        }
    }
}

void CDiskRecordPartHandler::close(CRC32 &fileCRC)
{
    if (in) 
        in->stop(&fileCRC);
    in.clear();
}

/////////////////////////////////////////////////

class CDiskReadSlaveActivity : public CDiskReadSlaveActivityRecord, public CThorDataLink
{
    class CDiskPartHandler : public CDiskRecordPartHandler
    {
        CDiskReadSlaveActivity &activity;
        RtlDynamicRowBuilder outBuilder;

public:
        CDiskPartHandler(CDiskReadSlaveActivity &_activity) 
            : CDiskRecordPartHandler(_activity), activity(_activity), outBuilder(NULL)

        {
            if (activity.needTransform)
                outBuilder.setAllocator(activity.queryRowAllocator()); // NB this doesn't link but hopefully OK during activity lifetime
        }

        virtual const void *nextRow()
        {
            if (!eoi && !activity.queryAbortSoon()) {
                try {
                    if (activity.needTransform) {
                        loop {
                            const void * row = CDiskRecordPartHandler::prefetchRow();
                            if (!row) {
                                if (!activity.grouped)
                                    break;
                                if (!firstInGroup) {
                                    firstInGroup = false;
                                    return NULL;
                                }
                                row = CDiskRecordPartHandler::prefetchRow();
                                if (!row)
                                    break;
                            }
                            size32_t sz;
                            if (activity.segMonitorsMatch(row)) 
                                sz = activity.helper->transform(outBuilder.ensureRow(), row);
                            else
                                sz = 0;
                            CDiskRecordPartHandler::prefetchDone();
                            if (sz) {
                                firstInGroup = false;
                                return outBuilder.finalizeRowClear(sz);  
                            }
                        }
                    }
                    else {
                        loop {
                            OwnedConstThorRow row = CDiskRecordPartHandler::nextRow();
                            if (!row) {
                                if (!activity.grouped)
                                    break;
                                if (!firstInGroup) {
                                    firstInGroup = false;
                                    return NULL;
                                }
                                row.setown(CDiskRecordPartHandler::nextRow());
                                if (!row)
                                    break;
                            }
                            firstInGroup = false;
                            return row.getClear();
                        }
                    }
                }
                catch (IException *e)
                {
                    eoi = true;
                    StringBuffer s;
                    e->errorMessage(s);
                    s.append(" - handling file: ").append(filename.get());
                    IException *e2 = MakeActivityException(&activity, e, "%s", s.str());
                    e->Release();
                    throw e2;
                }
            }
            eoi = true;
            return NULL;
        }

        virtual void getMetaInfo(ThorDataLinkMetaInfo &info, IPartDescriptor *partDesc)
        {
            CDiskRecordPartHandler::getMetaInfo(info, partDesc);
            if (activity.helper->transformMayFilter() || activity.segMonitors.length())
            {
                info.totalRowsMin = 0; // all bets off! 
                info.unknownRowsOutput = info.canReduceNumRows = true;
                info.byteTotal = (offset_t)-1;
            }
        }
    };

    class PgRecordSize : public CSimpleInterface, implements IRecordSize
    {
    public:
        IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

        PgRecordSize(IRecordSize *_rcSz)
        {
            rcSz = LINK(_rcSz);
        }
        ~PgRecordSize()
        {
            rcSz->Release();
        }
        virtual size32_t getRecordSize(const void *rec)
        {
            return rcSz->getRecordSize(rec) + 1;
        }
        virtual size32_t getFixedSize() const 
        {
            return rcSz->getFixedSize()?(rcSz->getFixedSize()+1):0;
        }
    private:
        IRecordSize *rcSz;
    };

public:
    bool needTransform, unsorted, countSent;
    rowcount_t limit;
    rowcount_t stopAfter;
    IRowStream *out;
    size32_t maxrecsize;

    IHThorDiskReadArg *helper;

    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

    CDiskReadSlaveActivity(CGraphElementBase *_container, IHThorArg *_helper) : CDiskReadSlaveActivityRecord(_container, _helper), CThorDataLink(this)
    {
        helper = (IHThorDiskReadArg *)queryHelper();
        unsorted = 0 != (TDRunsorted & helper->getFlags());
        grouped = 0 != (TDXgrouped & helper->getFlags());
        if (isFixedDiskWidth && grouped) diskRowMinSz++;
        needTransform = segMonitors.length() || helper->needTransform();
        out = NULL;
        countSent = false;
        if (helper->getFlags() & TDRlimitskips)
            limit = RCMAX;
        else
            limit = (rowcount_t)helper->getRowLimit();
        stopAfter = (rowcount_t)helper->getChooseNLimit();
    }
    ~CDiskReadSlaveActivity()
    {
        ::Release(out);
    }

// IThorSlaveActivity
    virtual void init(MemoryBuffer &data, MemoryBuffer &slaveData)
    {
        CDiskReadSlaveActivityRecord::init(data, slaveData);
        partHandler.setown(new CDiskPartHandler(*this));

        if (partDescs.ordinality())
        {
            IPartDescriptor &firstPart = partDescs.item(0);
            bool dfsGrouped = firstPart.queryOwner().isGrouped(); // must be consistent across parts.
            if (dfsGrouped != grouped) // NB: will already have warned in master->wuid.
                ActPrintLog("Dfsgrouping vs codegen grouping differ, DFS grouping takes precedence. DfsGrouping=%s", dfsGrouped?"true":"false");
            grouped = dfsGrouped;
        }
        if (grouped)
        {
            needTransform = helper->needTransform();
            if (unsorted)
            {
                Owned<IException> e = MakeActivityWarning(this, 0, "Diskread - ignoring 'unsorted' because marked 'grouped'");
                fireException(e);
                unsorted = false;
            }
        }

        appendOutputLinked(this);

    }
    virtual void kill()
    {
        if (out)
        {
            out->Release();
            out = NULL;
        }
        CDiskReadSlaveActivityRecord::kill();
    }

// IThorDataLink
    virtual void getMetaInfo(ThorDataLinkMetaInfo &info)
    {
        if (!gotMeta)
        {
            gotMeta = true;
            initMetaInfo(cachedMetaInfo);
            cachedMetaInfo.isSource = true;
            getPartsMetaInfo(cachedMetaInfo, *this, partDescs.ordinality(), partDescs.getArray(), partHandler);
        }
        info = cachedMetaInfo;
        if (info.totalRowsMin==info.totalRowsMax)
            ActPrintLog("DISKREAD: Number of rows to read: %"I64F"d", info.totalRowsMin);
        else
            ActPrintLog("DISKREAD: Number of rows to read: %"I64F"d (min), %"I64F"d (max)", info.totalRowsMin, info.totalRowsMax);
    }
    virtual void start()
    {
        ActivityTimer s(totalCycles, timeActivities, NULL);
        out = createSequentialPartHandler(partHandler, partDescs, grouped); // **
        dataLinkStart("DISKREAD", container.queryId());
    }
    virtual bool isGrouped() { return grouped; }

// IRowStream
    virtual void stop()
    {
        if (out)
        {
            out->stop();
            out->Release();
            out = NULL;
        }
        dataLinkStop();
    }
    CATCH_NEXTROW()
    {
        ActivityTimer t(totalCycles, timeActivities, NULL);
        if (NULL == out) // guard against, but shouldn't happen
            return NULL;
        OwnedConstThorRow ret = out->nextRow();
        if (!ret)
            return NULL;
        rowcount_t c = getDataLinkCount();
        if (stopAfter && (c >= stopAfter))  // NB: only slave limiter, global performed in chained choosen activity
            return NULL;
        if (c >= limit) { // NB: only slave limiter, global performed in chained limit activity
            helper->onLimitExceeded();
            return NULL;
        }
        dataLinkIncrement();
        return ret.getClear();
    }

friend class CDiskPartHandler;
};


CActivityBase *createDiskReadSlave(CGraphElementBase *container, IHThorArg *helper)
{
    return new CDiskReadSlaveActivity(container, helper);
}


//
// CDiskNormalizeSlave
//

class CDiskNormalizeSlave : public CDiskReadSlaveActivityRecord, public CThorDataLink
{
    class CNormalizePartHandler : public CDiskRecordPartHandler
    {
        RtlDynamicRowBuilder outBuilder;
        CDiskNormalizeSlave &activity;
        const void * nextrow;

    public:
        CNormalizePartHandler(CDiskNormalizeSlave &_activity) 
            : CDiskRecordPartHandler(_activity), activity(_activity), 
              outBuilder(_activity.queryRowAllocator()) // NB this doesn't link but hopefully OK during activity lifetime
        {
            nextrow = NULL;
        }

        ~CNormalizePartHandler()
        {
            if (nextrow)
                CDiskRecordPartHandler::prefetchDone();
        }

        const void *nextRow()
        {
            // logic here is a bit obscure
            if (eoi || activity.queryAbortSoon()) {
                eoi = true;
                return NULL;
            }
            loop {
                if (nextrow) {
                    while (activity.helper->next()) {
                        size32_t sz = activity.helper->transform(outBuilder.ensureRow());
                        if (sz) 
                            return outBuilder.finalizeRowClear(sz);
                    }
                    CDiskRecordPartHandler::prefetchDone();
                }
                nextrow = CDiskRecordPartHandler::prefetchRow();
                if (!nextrow)
                    break;
                if (activity.segMonitorsMatch(nextrow)) {
                    if (activity.helper->first(nextrow)) {
                        size32_t sz = activity.helper->transform(outBuilder.ensureRow());
                        if (sz) 
                            return outBuilder.finalizeRowClear(sz);
                        continue; // into next loop above
                    }
                }
                nextrow = NULL;
                CDiskRecordPartHandler::prefetchDone();
            }
            eoi = true;
            return NULL;
        }
  
    };

    IHThorDiskNormalizeArg *helper;
    rowcount_t limit;
    rowcount_t stopAfter;
    IRowStream *out;

public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

    CDiskNormalizeSlave(CGraphElementBase *_container) 
        : CDiskReadSlaveActivityRecord(_container), CThorDataLink(this)
    {
        helper = (IHThorDiskNormalizeArg *)queryHelper();
        if (helper->getFlags() & TDRlimitskips)
            limit = RCMAX;
        else
            limit = (rowcount_t)helper->getRowLimit();
        stopAfter = (rowcount_t)helper->getChooseNLimit();
        out = NULL;
    }
    ~CDiskNormalizeSlave()
    {
        ::Release(out);
    }

// IThorSlaveActivity
    virtual void init(MemoryBuffer &data, MemoryBuffer &slaveData)
    {
        CDiskReadSlaveActivityRecord::init(data, slaveData);
        appendOutputLinked(this);
        partHandler.setown(new CNormalizePartHandler(*this));
    }

// IThorDataLink
    virtual void getMetaInfo(ThorDataLinkMetaInfo &info)
    {
        if (!gotMeta)
        {
            gotMeta = true;
            initMetaInfo(cachedMetaInfo);
            cachedMetaInfo.isSource = true;
            getPartsMetaInfo(cachedMetaInfo, *this, partDescs.ordinality(), partDescs.getArray(), partHandler);
            cachedMetaInfo.unknownRowsOutput = true; // JCSMORE
        }
        info = cachedMetaInfo;
    }
    virtual void start()
    {
        ActivityTimer s(totalCycles, timeActivities, NULL);
        out = createSequentialPartHandler(partHandler, partDescs, false);
        dataLinkStart("DISKNORMALIZE", container.queryId());
    }
    virtual bool isGrouped() { return false; }

// IRowStream
    virtual void stop()
    {
        if (out)
        {
            out->Release();
            out = NULL;
        }
        dataLinkStop();
    }
    CATCH_NEXTROW()
    {
        ActivityTimer t(totalCycles, timeActivities, NULL);
        if (!out)
            return NULL;
        OwnedConstThorRow ret = out->nextRow();
        if (!ret)
            return NULL;
        rowcount_t c = getDataLinkCount();
        if (stopAfter && (c >= stopAfter)) // NB: only slave limiter, global performed in chained choosen activity
            return NULL;
        if (c >= limit) { // NB: only slave limiter, global performed in chained limit activity
            helper->onLimitExceeded();
            return NULL;
        }
        dataLinkIncrement();
        return ret.getClear();
    }

friend class CNormalizePartHandler;
};


CActivityBase *createDiskNormalizeSlave(CGraphElementBase *container)
{
    return new CDiskNormalizeSlave(container);
}

class CDiskSimplePartHandler : public CDiskRecordPartHandler
{
public:
    CDiskSimplePartHandler(CDiskReadSlaveActivityRecord &activity) : CDiskRecordPartHandler(activity)
    {
        eoi = false;
    }

    const void *nextRow()
    {
        if (!eoi) {
            try {
                if (!activity.queryAbortSoon()) {  
                    OwnedConstThorRow row = CDiskRecordPartHandler::nextRow();
                    if (row) 
                        return row.getClear();
                }
            }
            catch (IException *e) {
                StringBuffer s;
                e->errorMessage(s);
                s.append(" - handling file: ").append(filename.get());
                IException *e2 = MakeActivityException(&activity, e, "%s", s.str());
                e->Release();
                eoi = true;
                throw e2;
            }
        }
        eoi = true;
        return NULL;
    }
};

class CDiskAggregateSlave : public CDiskReadSlaveActivityRecord, public CThorDataLink
{
    IHThorDiskAggregateArg *helper;
    Owned<IEngineRowAllocator> allocator;
    bool eoi;
    CPartialResultAggregator aggregator;

public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

    CDiskAggregateSlave(CGraphElementBase *_container) 
        : CDiskReadSlaveActivityRecord(_container), aggregator(*this), CThorDataLink(this)
    {
        helper = (IHThorDiskAggregateArg *)queryHelper();
        eoi = false;
        allocator.set(queryRowAllocator());
    }

// IThorSlaveActivity
    virtual void init(MemoryBuffer &data, MemoryBuffer &slaveData)
    {
        CDiskReadSlaveActivityRecord::init(data, slaveData);
        if (!container.queryLocalOrGrouped())
            mpTag = container.queryJob().deserializeMPTag(data);
        appendOutputLinked(this);
        partHandler.setown(new CDiskSimplePartHandler(*this));
    }
    virtual void abort()
    {
        CDiskReadSlaveActivityRecord::abort();
        if (!container.queryLocalOrGrouped() && (1 == container.queryJob().queryMyRank()))
            aggregator.cancelGetResult();
    }

// IThorDataLink
    virtual void getMetaInfo(ThorDataLinkMetaInfo &info)
    {
        initMetaInfo(info);
        info.isSource = true;
        info.totalRowsMin = 0;
        info.totalRowsMax = 1;
        // MORE TBD
    }
    virtual bool isGrouped() { return false; }
    virtual void start()
    {
        ActivityTimer s(totalCycles, timeActivities, NULL);
        eoi = false;
        dataLinkStart("DISKAGGREGATE", container.queryId());
    }

// IRowStream
    virtual void stop()
    {
        dataLinkStop();
    }
    CATCH_NEXTROW()
    {
        ActivityTimer t(totalCycles, timeActivities, NULL);
        if (eoi)
            return NULL;
        RtlDynamicRowBuilder row(allocator);
        size32_t sz = helper->clearAggregate(row);
        unsigned part = 0;
        while (!abortSoon && part<partDescs.ordinality())
        {
            partHandler->setPart(&partDescs.item(part), part);
            ++part;
            loop {
                OwnedConstThorRow nextrow =  partHandler->nextRow();
                if (!nextrow)
                    break;
                if (segMonitorsMatch(nextrow)) {
                    helper->processRow(row, nextrow); // can change row size TBD
                    sz = allocator->queryOutputMeta()->getRecordSize(row.getSelf()); // kludge
                }
            }
        }
        eoi = true;
        OwnedConstThorRow ret = row.finalizeRowClear(sz);
        if (container.queryLocalOrGrouped())
        {
            dataLinkIncrement();
            return ret.getClear();
        }
        else
        {
            aggregator.sendResult(ret.get());
            if (1 == container.queryJob().queryMyRank())
            {
                ret.setown(aggregator.getResult());
                if (ret)
                {
                    dataLinkIncrement();
                    return ret.getClear();
                }
            }
            return NULL;
        }
    }
};

CActivityBase *createDiskAggregateSlave(CGraphElementBase *container)
{
    return new CDiskAggregateSlave(container);
}


class CDiskCountSlave : public CDiskReadSlaveActivityRecord, public CThorDataLink
{
    IHThorDiskCountArg *helper;
    rowcount_t stopAfter, preknownTotalCount;
    bool eoi, totalCountKnown;

public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

    CDiskCountSlave(CGraphElementBase *_container) : CDiskReadSlaveActivityRecord(_container), CThorDataLink(this)
    {
        helper = (IHThorDiskCountArg *)queryHelper();
        totalCountKnown = eoi = false;
        preknownTotalCount = 0;
        mpTag = TAG_NULL;
        stopAfter = (rowcount_t)helper->getChooseNLimit();
        totalCountKnown = false;
    }

// IThorSlaveActivity
    virtual void init(MemoryBuffer &data, MemoryBuffer &slaveData)
    {
        CDiskReadSlaveActivityRecord::init(data, slaveData);
        if (!container.queryLocalOrGrouped())
            mpTag = container.queryJob().deserializeMPTag(data);
        data.read(totalCountKnown);
        data.read(preknownTotalCount);
        appendOutputLinked(this);
        partHandler.setown(new CDiskSimplePartHandler(*this));
    }
    virtual void abort()
    {
        CDiskReadSlaveActivityRecord::abort();
        if (!container.queryLocalOrGrouped() && (1 == container.queryJob().queryMyRank()))
            cancelReceiveMsg(0, mpTag);
    }

// IThorDataLink
    virtual void getMetaInfo(ThorDataLinkMetaInfo &info)
    {
        initMetaInfo(info);
        info.isSource = true;
        // MORE TBD
    }
    virtual bool isGrouped() { return false; }
    virtual void start()
    {
        ActivityTimer s(totalCycles, timeActivities, NULL);
        eoi = false;
        if (!helper->canMatchAny())
        {
            totalCountKnown = true;
            preknownTotalCount = 0;
        }
        dataLinkStart("DISKCOUNT", container.queryId());
    }

// IRowStream
    virtual void stop()
    {
        dataLinkStop();
    }
    CATCH_NEXTROW()
    {
        ActivityTimer t(totalCycles, timeActivities, NULL);
        if (eoi)
            return NULL;
        unsigned __int64 totalCount = 0;
        eoi = true;
        if (totalCountKnown)
        {
            totalCount = preknownTotalCount;
            if (!container.queryLocalOrGrouped() && (1 != container.queryJob().queryMyRank()))
                return NULL;
        }
        else
        {
            unsigned part = 0;
            while (!abortSoon && part<partDescs.ordinality())
            {
                partHandler->setPart(&partDescs.item(part), part);
                ++part;
                loop {
                    OwnedConstThorRow nextrow = partHandler->nextRow();
                    if (!nextrow)
                        break;
                    if (segMonitorsMatch(nextrow))
                        totalCount += helper->numValid(nextrow);
                    if (totalCount > stopAfter)
                        break;
                }
                if (totalCount > stopAfter)
                    break;
            }
            if (!container.queryLocalOrGrouped())
            {
                sendPartialCount(*this, (rowcount_t)totalCount);
                if (1 != container.queryJob().queryMyRank())
                    return NULL;
                totalCount = getFinalCount(*this);
            }
            if (totalCount > stopAfter)
                totalCount = stopAfter;
        }
        RtlDynamicRowBuilder rowBuilder(queryRowAllocator());
        CThorStreamDeserializerSource ssz(sizeof(totalCount),&totalCount);
        size32_t sz = queryRowDeserializer()->deserialize(rowBuilder,ssz);
        dataLinkIncrement();
        return rowBuilder.finalizeRowClear(sz);
    }
};


CActivityBase *createDiskCountSlave(CGraphElementBase *container)
{
    return new CDiskCountSlave(container);
}

class CDiskGroupAggregateSlave 
  : public CDiskReadSlaveActivityRecord, public CThorDataLink, implements IHThorGroupAggregateCallback
{
    IHThorDiskGroupAggregateArg *helper;
    bool gathered, eoi;
    Owned<CThorRowAggregator> localAggTable;
    Owned<IEngineRowAllocator> allocator;
    bool merging;
    
public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);

    CDiskGroupAggregateSlave(CGraphElementBase *_container) 
        : CDiskReadSlaveActivityRecord(_container), CThorDataLink(this)
    {
        helper = (IHThorDiskGroupAggregateArg *)queryHelper();
        merging = false;
    }

// IHThorGroupAggregateCallback
    virtual void processRow(const void *next)
    {
        localAggTable->addRow(next);
    }
// IThorSlaveActivity
    virtual void init(MemoryBuffer &data, MemoryBuffer &slaveData)
    {
        CDiskReadSlaveActivityRecord::init(data, slaveData);
        mpTag = container.queryJob().deserializeMPTag(data);
        appendOutputLinked(this);
        partHandler.setown(new CDiskSimplePartHandler(*this));
        allocator.set(queryRowAllocator());
    }
    virtual void abort()
    {
        CDiskReadSlaveActivityRecord::abort();
        if (merging)
            container.queryJob().queryJobComm().cancel(0, mpTag);
    }
// IThorDataLink
    virtual void start()
    {
        ActivityTimer s(totalCycles, timeActivities, NULL);
        gathered = eoi = false;
        localAggTable.setown(new CThorRowAggregator(*this, *helper, *helper, queryLargeMemSize()/10, container.queryOwnerId()==0));
        localAggTable->start(queryRowAllocator());
        dataLinkStart("DISKGROUPAGGREGATE", container.queryId());
    }
    virtual void getMetaInfo(ThorDataLinkMetaInfo &info)
    {
        initMetaInfo(info);
        info.isSource = true;
        // MORE TBD
    }
    virtual bool isGrouped() { return false; }
// IRowStream
    virtual void stop()
    {
        dataLinkStop();
    }
    CATCH_NEXTROW()
    {
        ActivityTimer t(totalCycles, timeActivities, NULL);
        if (eoi)
            return NULL;
        if (!gathered)
        {
            unsigned part = 0;
            while (!abortSoon && part<partDescs.ordinality())
            {
                partHandler->setPart(&partDescs.item(part), part);
                ++part;
                loop
                {
                    OwnedConstThorRow nextrow = partHandler->nextRow();
                    if (!nextrow)
                        break;
                    helper->processRow(nextrow, this);
                }
            }
            gathered = true;
            ActPrintLog("DISKGROUPAGGREGATE: Local aggregate table contains %d entries", localAggTable->elementCount());

            if (!container.queryLocalOrGrouped() && container.queryJob().querySlaves()>1)
            {
                BooleanOnOff onOff(merging);
                bool ordered = 0 != (TDRorderedmerge & helper->getFlags());
                localAggTable.setown(mergeLocalAggs(*this, *helper, *helper, localAggTable, mpTag, queryLargeMemSize()/10, container.queryOwnerId()==0, ordered));
            }
        }
        Owned<AggregateRowBuilder> next = localAggTable->nextResult();
        if (next)
        {
            dataLinkIncrement();
            return next->finalizeRowClear();
        }
        eoi = true;
        return NULL;
    }
};

CActivityBase *createDiskGroupAggregateSlave(CGraphElementBase *container)
{
    return new CDiskGroupAggregateSlave(container);
}
