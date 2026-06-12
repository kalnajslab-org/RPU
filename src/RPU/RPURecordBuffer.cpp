#include "RPURecordBuffer.h"

DMAMEM RPURecordBuffer rpu_records;

bool RPURecordBuffer::push(const RPUReport& report)
{
  if (records_.full()) { return false; }

  Record record;
  if (!report.encode(record.data(), record.size())) { return false; }

  records_.push(record);
  return true;
}

bool RPURecordBuffer::pop(uint8_t* out, size_t out_size)
{
  if (records_.empty() || out_size < RECORD_SIZE) { return false; }

  memcpy(out, records_.front().data(), RECORD_SIZE);
  records_.pop();
  return true;
}
