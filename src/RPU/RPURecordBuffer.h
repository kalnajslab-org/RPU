/*
 * RPURecordBuffer.h
 *
 * Fixed-capacity FIFO of encoded RPUReport records, held in RAM until they
 * are offloaded to RATCHuTS over the docking connector.
 *
 * Storage lives in RAM2 (DMAMEM / OCRAM2), which is DMA-accessible, unlike
 * the default DTCM (RAM1) data section. At RPU_MAX_RECORDS x RPU_RPT_BYTES
 * = 3000 x 134 = 402000 bytes (~393 KiB), this consumes most of the 512 KiB
 * RAM2 region, so keep that in mind before adding other DMAMEM buffers.
 */
#pragma once
#include <Arduino.h>
#include <etl/queue.h>
#include <etl/array.h>
#include "RPUComm.h"

#define RPU_MAX_RECORDS 3000

class RPURecordBuffer {
public:
    static constexpr size_t RECORD_SIZE = RPU_RPT_BYTES;
    using Record = etl::array<uint8_t, RECORD_SIZE>;

    // Encodes report and pushes it onto the FIFO. Returns false if full.
    bool push(const RPUReport& report);

    // Pops the oldest record into out[0..RECORD_SIZE). Returns false if empty.
    bool pop(uint8_t* out, size_t out_size);

    size_t size()      const { return records_.size(); }
    size_t available() const { return records_.available(); }
    bool   empty()     const { return records_.empty(); }
    bool   full()      const { return records_.full(); }
    void   clear()           { records_.clear(); }

private:
    etl::queue<Record, RPU_MAX_RECORDS> records_;
};

// Singleton instance, placed in RAM2 for DMA-capable transfer to RATCHuTS.
extern RPURecordBuffer rpu_records;
